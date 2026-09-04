// license:BSD-3-Clause

#include "discord_directory_server.h"

#include <stdexcept>

namespace mamehub {

using json = nlohmann::json;

discord_directory_server::discord_directory_server(discord_identity identity, std::string secret, std::string game_name, bool hosting, int expected_players, unsigned short port) :
	m_identity(std::move(identity)),
	m_secret(std::move(secret)),
	m_hosting(hosting),
	m_expected_players(expected_players),
	m_port(port),
	m_lobby(m_secret, std::to_string(m_identity.id), m_hosting ? std::to_string(m_identity.id) : std::string(), std::move(game_name))
{
	std::string error;
	if (!discord_service::instance().create_or_join_lobby(m_secret, m_discord_lobby_id, error))
		throw std::runtime_error("Could not join Discord lobby: " + error);

	std::vector<discord_message> history;
	if (!discord_service::instance().get_lobby_messages(m_discord_lobby_id, history, error))
		throw std::runtime_error("Could not read Discord lobby: " + error);
	for (auto const &message : history)
		if (message.lobby_id == m_discord_lobby_id)
			m_lobby.receive(std::to_string(message.author_id), message.content);

	if (m_hosting && !publish(m_lobby.make_host_message()))
		throw std::runtime_error("Could not announce Discord lobby host");

	m_server.config.port = m_port;
	m_server.resource["^/api/get_current_game_id/(.+)$"]["GET"] = [this] (auto response, auto)
	{
		response->write(SimpleWeb::StatusCode::success_ok, json({ { "gameId", std::to_string(m_discord_lobby_id) } }).dump());
	};
	m_server.resource["^/api/get_game_info/(.+)$"]["GET"] = [this] (auto response, auto)
	{
		response->write(SimpleWeb::StatusCode::success_ok, game_info().dump());
	};
	m_server.resource["^/api/host$"]["POST"] = [this] (auto response, auto request)
	{
		json const data = json::parse(request->content.string());
		if (!m_hosting || (data.at("hostId").get<std::string>() != std::to_string(m_identity.id)))
			throw std::runtime_error("Discord host identity mismatch");
		if (!publish(m_lobby.make_host_message()) || !publish(m_lobby.make_join_message(data.at("name").get<std::string>(), data.at("peerKey").get<std::string>())))
			throw std::runtime_error("Could not publish Discord host information");
		response->write(SimpleWeb::StatusCode::success_ok, json({ { "status", "OK" } }).dump());
	};
	m_server.resource["^/api/join$"]["POST"] = [this] (auto response, auto request)
	{
		json const data = json::parse(request->content.string());
		if (data.at("peerId").get<std::string>() != std::to_string(m_identity.id))
			throw std::runtime_error("Discord peer identity mismatch");
		if (!publish(m_lobby.make_join_message(data.at("name").get<std::string>(), data.at("peerKey").get<std::string>())))
			throw std::runtime_error("Could not publish Discord peer information");
		response->write(SimpleWeb::StatusCode::success_ok, json({ { "status", "OK" } }).dump());
	};
	m_server.resource["^/api/update_endpoints$"]["POST"] = [this] (auto response, auto request)
	{
		json const data = json::parse(request->content.string());
		if (data.at("peerId").get<std::string>() != std::to_string(m_identity.id))
			throw std::runtime_error("Discord endpoint identity mismatch");
		auto found = m_lobby.members().find(std::to_string(m_identity.id));
		if (found == m_lobby.members().end())
			throw std::runtime_error("Local Discord member has not joined");
		if (!publish(m_lobby.make_discovery_message(found->second.public_key, data.at("endpoints").get<std::vector<std::string>>())))
			throw std::runtime_error("Could not publish Discord endpoint information");
		response->write(SimpleWeb::StatusCode::success_ok, json({ { "status", "OK" } }).dump());
	};

	m_server_thread = std::thread([this] { m_server.start(); });
}

discord_directory_server::~discord_directory_server()
{
	m_server.stop();
	if (m_server_thread.joinable())
		m_server_thread.join();
}

bool discord_directory_server::publish(std::string const &message)
{
	std::string error;
	if (!discord_service::instance().send_lobby_message(m_discord_lobby_id, message, error))
		return false;
	return m_lobby.receive(std::to_string(m_identity.id), message);
}

void discord_directory_server::sync_messages()
{
	for (auto const &message : discord_service::instance().drain_messages())
		if (message.lobby_id == m_discord_lobby_id)
			m_lobby.receive(std::to_string(message.author_id), message.content);
}

json discord_directory_server::game_info()
{
	std::unique_lock<std::mutex> guard(m_mutex);
	sync_messages();
	auto const host_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
	while (m_lobby.host_id().empty())
	{
		if (std::chrono::steady_clock::now() >= host_deadline)
			throw std::runtime_error("Timed out waiting for the Discord lobby host");
		guard.unlock();
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		guard.lock();
		sync_messages();
	}

	json peers = json::object();
	for (auto const &entry : m_lobby.members())
	{
		peers[entry.first] = {
			{ "id", entry.first },
			{ "key", entry.second.public_key },
			{ "name", entry.second.display_name },
			{ "endpoints", entry.second.endpoints }
		};
	}
	return {
		{ "gameName", m_lobby.game_name() },
		{ "ready", m_lobby.started() },
		{ "peerData", peers },
		{ "hostId", m_lobby.host_id() }
	};
}

discord_waiting_room discord_directory_server::waiting_room()
{
	std::lock_guard<std::mutex> guard(m_mutex);
	sync_messages();
	discord_waiting_room result;
	for (auto const &entry : m_lobby.members())
		result.members.push_back(entry.second);
	result.can_start = m_hosting && (int(result.members.size()) >= m_expected_players) && m_lobby.can_start();
	result.started = m_lobby.started();
	return result;
}

bool discord_directory_server::start_game(std::string &error)
{
	std::lock_guard<std::mutex> guard(m_mutex);
	sync_messages();
	if (!m_hosting)
	{
		error = "Only the Discord lobby host can start the game";
		return false;
	}
	if ((int(m_lobby.members().size()) < m_expected_players) || !m_lobby.can_start())
	{
		error = "Every expected player must be ready before starting";
		return false;
	}
	if (!publish(m_lobby.make_start_message()))
	{
		error = "Could not publish the Discord start message";
		return false;
	}
	return true;
}

} // namespace mamehub
