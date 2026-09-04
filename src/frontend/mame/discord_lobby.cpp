// license:BSD-3-Clause

#include "discord_lobby.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <exception>

namespace mamehub {
namespace {

constexpr int PROTOCOL_VERSION = 1;
using json = nlohmann::json;

json envelope(std::string_view type, std::string const &lobby_id, std::string const &sender_id)
{
	return { { "v", PROTOCOL_VERSION }, { "type", type }, { "lobby", lobby_id }, { "sender", sender_id } };
}

bool valid_endpoint(std::string const &endpoint)
{
	auto const separator = endpoint.rfind(':');
	if ((separator == std::string::npos) || !separator || ((separator + 1) == endpoint.size()))
		return false;
	return std::all_of(endpoint.begin() + separator + 1, endpoint.end(), [] (char ch) { return (ch >= '0') && (ch <= '9'); });
}

} // anonymous namespace

discord_lobby::discord_lobby(std::string lobby_id, std::string local_discord_id, std::string host_discord_id, std::string game_name) :
	m_lobby_id(std::move(lobby_id)),
	m_local_discord_id(std::move(local_discord_id)),
	m_host_discord_id(std::move(host_discord_id)),
	m_game_name(std::move(game_name))
{
}

std::string discord_lobby::make_join_message(std::string_view display_name, std::string_view public_key) const
{
	auto result = envelope("join", m_lobby_id, m_local_discord_id);
	result["name"] = display_name;
	result["key"] = public_key;
	result["game"] = m_game_name;
	return result.dump();
}

std::string discord_lobby::make_host_message() const
{
	auto result = envelope("host", m_lobby_id, m_local_discord_id);
	result["game"] = m_game_name;
	return result.dump();
}

std::string discord_lobby::make_discovery_message(std::string_view public_key, std::vector<std::string> const &endpoints) const
{
	auto result = envelope("discovery", m_lobby_id, m_local_discord_id);
	result["key"] = public_key;
	result["endpoints"] = endpoints;
	return result.dump();
}

std::string discord_lobby::make_start_message() const
{
	auto result = envelope("start", m_lobby_id, m_local_discord_id);
	result["game"] = m_game_name;
	return result.dump();
}

bool discord_lobby::validate_envelope(std::string_view authenticated_sender_id, std::string const &sender, std::string const &lobby, int protocol)
{
	if (protocol != PROTOCOL_VERSION)
		m_last_error = "incompatible MAMEHub lobby protocol";
	else if (lobby != m_lobby_id)
		m_last_error = "message belongs to another lobby";
	else if (sender != authenticated_sender_id)
		m_last_error = "Discord sender does not match message identity";
	else
		return true;
	return false;
}

bool discord_lobby::receive(std::string_view authenticated_sender_id, std::string_view message)
{
	m_last_error.clear();
	try
	{
		auto const data = json::parse(message);
		auto const sender = data.at("sender").get<std::string>();
		if (!validate_envelope(authenticated_sender_id, sender, data.at("lobby").get<std::string>(), data.at("v").get<int>()))
			return false;

		auto const type = data.at("type").get<std::string>();
		if (type == "host")
		{
			auto const announced_game = data.at("game").get<std::string>();
			if (m_game_name.empty())
				m_game_name = announced_game;
			else if (announced_game != m_game_name)
			{
				m_last_error = "host selected a different game";
				return false;
			}
			if (!m_host_discord_id.empty() && (m_host_discord_id != sender))
			{
				m_last_error = "lobby already has another host";
				return false;
			}
			m_host_discord_id = sender;
		}
		else if (type == "join")
		{
			if (data.at("game").get<std::string>() != m_game_name)
			{
				m_last_error = "player selected a different game";
				return false;
			}
			auto &member = m_members[sender];
			member = { sender, data.at("name").get<std::string>(), data.at("key").get<std::string>(), { }, false };
		}
		else if (type == "discovery")
		{
			auto found = m_members.find(sender);
			if (found == m_members.end())
			{
				m_last_error = "discovery arrived before the player joined";
				return false;
			}
			auto const key = data.at("key").get<std::string>();
			auto const endpoints = data.at("endpoints").get<std::vector<std::string>>();
			if ((key != found->second.public_key) || endpoints.empty() || !std::all_of(endpoints.begin(), endpoints.end(), valid_endpoint))
			{
				m_last_error = "invalid discovery information";
				return false;
			}
			found->second.endpoints = endpoints;
			found->second.ready = true;
		}
		else if (type == "start")
		{
			if ((sender != m_host_discord_id) || (data.at("game").get<std::string>() != m_game_name) || !can_start())
			{
				m_last_error = "host tried to start before every player was ready";
				return false;
			}
			m_started = true;
		}
		else
		{
			m_last_error = "unknown lobby message";
			return false;
		}
	}
	catch (std::exception const &error)
	{
		m_last_error = std::string("malformed lobby message: ") + error.what();
		return false;
	}
	return true;
}

bool discord_lobby::can_start() const
{
	return (m_members.size() >= 2) && std::all_of(m_members.begin(), m_members.end(), [] (auto const &entry) { return entry.second.ready; });
}

} // namespace mamehub
