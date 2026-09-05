// license:BSD-3-Clause

#ifndef MAME_FRONTEND_MAME_DISCORD_LOBBY_H
#define MAME_FRONTEND_MAME_DISCORD_LOBBY_H

#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mamehub {

struct lobby_member
{
	std::string discord_id;
	std::string display_name;
	std::string public_key;
	std::vector<std::string> endpoints;
	bool ready = false;
};

// State and wire format are independent of the Discord SDK.  The SDK adapter
// supplies the authenticated sender ID alongside every received message.
class discord_lobby
{
public:
	discord_lobby(std::string lobby_id, std::string local_discord_id, std::string host_discord_id, std::string game_name);

	std::string make_join_message(std::string_view display_name, std::string_view public_key) const;
	std::string make_host_message() const;
	std::string make_discovery_message(std::string_view public_key, std::vector<std::string> const &endpoints) const;
	std::string make_start_message() const;
	std::string make_leave_message() const;
	bool receive(std::string_view authenticated_sender_id, std::string_view message);
	bool can_start() const;
	bool started() const { return m_started; }
	bool is_host() const { return !m_host_discord_id.empty() && (m_local_discord_id == m_host_discord_id); }
	std::string const &host_id() const { return m_host_discord_id; }
	std::string const &game_name() const { return m_game_name; }
	std::string const &last_error() const { return m_last_error; }
	std::map<std::string, lobby_member> const &members() const { return m_members; }

private:
	bool validate_envelope(std::string_view authenticated_sender_id, std::string const &sender, std::string const &lobby, int protocol);

	std::string m_lobby_id;
	std::string m_local_discord_id;
	std::string m_host_discord_id;
	std::string m_game_name;
	std::map<std::string, lobby_member> m_members;
	std::string m_last_error;
	bool m_started = false;
};

} // namespace mamehub

#endif
