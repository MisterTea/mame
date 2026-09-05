// license:BSD-3-Clause

#ifndef MAME_FRONTEND_MAME_DISCORD_DISCOVERY_H
#define MAME_FRONTEND_MAME_DISCORD_DISCOVERY_H

#pragma once

#include "discord_service.h"

#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace mamehub {

struct open_lobby_info
{
	std::string secret;
	std::string system_name;
	std::string game_title;
	std::string host_name;
	std::string host_id;
	int players = 1;
	bool is_open = true;
	std::chrono::steady_clock::time_point last_seen;
};

class discord_discovery
{
public:
	static discord_discovery &instance();

	bool ensure_connected();
	void announce_lobby(std::string const &secret, std::string const &system_name, std::string const &game_title, std::string const &host_name, int players);
	void close_lobby(std::string const &secret);
	void query_lobbies();
	void update();
	std::vector<open_lobby_info> get_open_lobbies();

	void set_my_hosted_lobby(std::string secret, std::string system_name, std::string game_title, std::string host_name, int players);
	void clear_my_hosted_lobby();
	void reset();

private:
	discord_discovery();
	~discord_discovery() = default;

	std::mutex m_mutex;
	std::uint64_t m_directory_lobby_id = 0;
	bool m_connected = false;
	std::map<std::string, open_lobby_info> m_lobbies;

	// Information about the lobby hosted by this instance, if any
	bool m_hosting = false;
	open_lobby_info m_my_lobby;
	std::chrono::steady_clock::time_point m_last_heartbeat;
};

} // namespace mamehub

#endif
