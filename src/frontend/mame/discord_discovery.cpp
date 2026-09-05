// license:BSD-3-Clause

#include "discord_discovery.h"

#include "nlohmann/json.hpp"

namespace mamehub {

namespace {

constexpr char const *DIRECTORY_SECRET = "mamehub-directory-v1";
constexpr int PROTOCOL_VERSION = 1;
using json = nlohmann::json;

} // anonymous namespace

discord_discovery &discord_discovery::instance()
{
	static discord_discovery discovery;
	return discovery;
}

discord_discovery::discord_discovery()
{
}

bool discord_discovery::ensure_connected()
{
	std::lock_guard<std::mutex> guard(m_mutex);
	if (m_connected)
		return true;

	std::string error;
	if (!discord_service::instance().create_or_join_lobby(DIRECTORY_SECRET, m_directory_lobby_id, error))
		return false;

	std::vector<discord_message> history;
	if (discord_service::instance().get_lobby_messages(m_directory_lobby_id, history, error))
	{
		auto const now = std::chrono::steady_clock::now();
		for (auto const &msg : history)
		{
			if (msg.lobby_id != m_directory_lobby_id)
				continue;
			try
			{
				auto const data = json::parse(msg.content);
				if (data.value("v", 0) != PROTOCOL_VERSION)
					continue;
				auto const type = data.value("type", "");
				if (type == "announce")
				{
					std::string secret = data.value("secret", "");
					if (!secret.empty())
					{
						bool is_open = data.value("open", true);
						if (is_open)
						{
							open_lobby_info info;
							info.secret = secret;
							info.system_name = data.value("game", "");
							info.software_name = data.value("software", "");
							info.game_title = data.value("title", "");
							info.host_name = data.value("host_name", "");
							info.host_id = data.value("host_id", "");
							info.players = data.value("players", 1);
							info.is_open = true;
							info.last_seen = now;
							m_lobbies[secret] = std::move(info);
						}
						else
						{
							m_lobbies.erase(secret);
						}
					}
				}
				else if (type == "close")
				{
					std::string secret = data.value("secret", "");
					if (!secret.empty())
						m_lobbies.erase(secret);
				}
			}
			catch (...)
			{
			}
		}
	}

	m_connected = true;
	return true;
}

void discord_discovery::announce_lobby(std::string const &secret, std::string const &system_name, std::string const &software_name, std::string const &game_title, std::string const &host_name, int players)
{
	std::lock_guard<std::mutex> guard(m_mutex);
	if (!m_connected)
		return;

	json msg = {
		{ "v", PROTOCOL_VERSION },
		{ "type", "announce" },
		{ "secret", secret },
		{ "game", system_name },
		{ "software", software_name },
		{ "title", game_title },
		{ "host_name", host_name },
		{ "host_id", std::to_string(discord_service::instance().current_identity().id) },
		{ "players", players },
		{ "open", true }
	};

	std::string error;
	discord_service::instance().send_lobby_message(m_directory_lobby_id, msg.dump(), error);
}

void discord_discovery::close_lobby(std::string const &secret)
{
	std::lock_guard<std::mutex> guard(m_mutex);
	m_lobbies.erase(secret);

	if (!m_connected)
		return;

	json msg = {
		{ "v", PROTOCOL_VERSION },
		{ "type", "close" },
		{ "secret", secret },
		{ "open", false }
	};

	std::string error;
	discord_service::instance().send_lobby_message(m_directory_lobby_id, msg.dump(), error);
}

void discord_discovery::query_lobbies()
{
	std::lock_guard<std::mutex> guard(m_mutex);
	if (!m_connected)
		return;

	json msg = {
		{ "v", PROTOCOL_VERSION },
		{ "type", "query" }
	};

	std::string error;
	discord_service::instance().send_lobby_message(m_directory_lobby_id, msg.dump(), error);
}

void discord_discovery::set_my_hosted_lobby(std::string secret, std::string system_name, std::string software_name, std::string game_title, std::string host_name, int players)
{
	std::lock_guard<std::mutex> guard(m_mutex);
	m_hosting = true;
	m_my_lobby.secret = std::move(secret);
	m_my_lobby.system_name = std::move(system_name);
	m_my_lobby.software_name = std::move(software_name);
	m_my_lobby.game_title = std::move(game_title);
	m_my_lobby.host_name = std::move(host_name);
	m_my_lobby.host_id = std::to_string(discord_service::instance().current_identity().id);
	m_my_lobby.players = players;
	m_my_lobby.is_open = true;
	m_my_lobby.last_seen = std::chrono::steady_clock::now();
	m_last_heartbeat = m_my_lobby.last_seen;

	if (m_connected)
	{
		json msg = {
			{ "v", PROTOCOL_VERSION },
			{ "type", "announce" },
			{ "secret", m_my_lobby.secret },
			{ "game", m_my_lobby.system_name },
			{ "software", m_my_lobby.software_name },
			{ "title", m_my_lobby.game_title },
			{ "host_name", m_my_lobby.host_name },
			{ "host_id", m_my_lobby.host_id },
			{ "players", m_my_lobby.players },
			{ "open", true }
		};
		std::string error;
		discord_service::instance().send_lobby_message(m_directory_lobby_id, msg.dump(), error);
	}
}

void discord_discovery::clear_my_hosted_lobby()
{
	std::lock_guard<std::mutex> guard(m_mutex);
	if (m_hosting)
	{
		std::string secret = m_my_lobby.secret;
		m_hosting = false;
		m_my_lobby = open_lobby_info();

		if (m_connected && !secret.empty())
		{
			json msg = {
				{ "v", PROTOCOL_VERSION },
				{ "type", "close" },
				{ "secret", secret },
				{ "open", false }
			};
			std::string error;
			discord_service::instance().send_lobby_message(m_directory_lobby_id, msg.dump(), error);
		}
	}
}

void discord_discovery::update()
{
	std::lock_guard<std::mutex> guard(m_mutex);
	if (!m_connected)
		return;

	auto const now = std::chrono::steady_clock::now();

	// Heartbeat if hosting
	if (m_hosting && m_my_lobby.is_open)
	{
		if (std::chrono::duration_cast<std::chrono::seconds>(now - m_last_heartbeat).count() >= 5)
		{
			m_last_heartbeat = now;
			json msg = {
				{ "v", PROTOCOL_VERSION },
				{ "type", "announce" },
				{ "secret", m_my_lobby.secret },
				{ "game", m_my_lobby.system_name },
				{ "software", m_my_lobby.software_name },
				{ "title", m_my_lobby.game_title },
				{ "host_name", m_my_lobby.host_name },
				{ "host_id", m_my_lobby.host_id },
				{ "players", m_my_lobby.players },
				{ "open", true }
			};
			std::string error;
			discord_service::instance().send_lobby_message(m_directory_lobby_id, msg.dump(), error);
		}
	}

	// Drain messages
	for (auto const &msg : discord_service::instance().drain_messages())
	{
		if (msg.lobby_id != m_directory_lobby_id)
			continue;
		try
		{
			auto const data = json::parse(msg.content);
			if (data.value("v", 0) != PROTOCOL_VERSION)
				continue;
			auto const type = data.value("type", "");
			if (type == "announce")
			{
				std::string secret = data.value("secret", "");
				if (!secret.empty())
				{
					bool is_open = data.value("open", true);
					if (is_open)
					{
						open_lobby_info info;
						info.secret = secret;
						info.system_name = data.value("game", "");
						info.software_name = data.value("software", "");
						info.game_title = data.value("title", "");
						info.host_name = data.value("host_name", "");
						info.host_id = data.value("host_id", "");
						info.players = data.value("players", 1);
						info.is_open = true;
						info.last_seen = now;
						m_lobbies[secret] = std::move(info);
					}
					else
					{
						m_lobbies.erase(secret);
					}
				}
			}
			else if (type == "close")
			{
				std::string secret = data.value("secret", "");
				if (!secret.empty())
					m_lobbies.erase(secret);
			}
			else if (type == "query")
			{
				if (m_hosting && m_my_lobby.is_open)
				{
					json reply = {
						{ "v", PROTOCOL_VERSION },
						{ "type", "announce" },
						{ "secret", m_my_lobby.secret },
						{ "game", m_my_lobby.system_name },
						{ "software", m_my_lobby.software_name },
						{ "title", m_my_lobby.game_title },
						{ "host_name", m_my_lobby.host_name },
						{ "host_id", m_my_lobby.host_id },
						{ "players", m_my_lobby.players },
						{ "open", true }
					};
					std::string error;
					discord_service::instance().send_lobby_message(m_directory_lobby_id, reply.dump(), error);
				}
			}
		}
		catch (...)
		{
		}
	}

	// Prune lobbies not seen for > 45 seconds
	for (auto it = m_lobbies.begin(); it != m_lobbies.end(); )
	{
		if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_seen).count() > 45)
			it = m_lobbies.erase(it);
		else
			++it;
	}
}

std::vector<open_lobby_info> discord_discovery::get_open_lobbies()
{
	update();
	std::lock_guard<std::mutex> guard(m_mutex);
	std::vector<open_lobby_info> result;
	result.reserve(m_lobbies.size());
	for (auto const &entry : m_lobbies)
	{
		if (entry.second.is_open)
			result.push_back(entry.second);
	}
	return result;
}

void discord_discovery::reset()
{
	std::lock_guard<std::mutex> guard(m_mutex);
	m_connected = false;
	m_directory_lobby_id = 0;
	m_lobbies.clear();
	m_hosting = false;
	m_my_lobby = open_lobby_info{};
}

} // namespace mamehub
