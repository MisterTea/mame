// license:BSD-3-Clause

#ifndef MAME_FRONTEND_MAME_DISCORD_DIRECTORY_SERVER_H
#define MAME_FRONTEND_MAME_DISCORD_DIRECTORY_SERVER_H

#pragma once

#include "SimpleWebServer/server_http.hpp"
#include "nlohmann/json.hpp"

#include "discord_lobby.h"
#include "discord_service.h"

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mamehub {

struct discord_waiting_room
{
	std::vector<lobby_member> members;
	bool can_start = false;
	bool started = false;
};

// Local compatibility endpoint for WGA's existing directory API.  Every
// MAMEHub process owns one of these; Discord messages keep their mirrors in
// sync, so no remotely hosted MAMEHub directory is involved.
class discord_directory_server
{
public:
	discord_directory_server(discord_identity identity, std::string secret, std::string game_name, bool hosting, int expected_players, unsigned short port, std::string announcement_game = { });
	~discord_directory_server();

	discord_directory_server(discord_directory_server const &) = delete;
	discord_directory_server &operator=(discord_directory_server const &) = delete;

	unsigned short port() const { return m_port; }
	std::string const &host_id() const { return m_lobby.host_id(); }
	discord_waiting_room waiting_room();
	bool start_game(std::string &error);

private:
	void sync_messages();
	bool publish(std::string const &message);
	nlohmann::json game_info();

	discord_identity m_identity;
	std::string m_secret;
	std::uint64_t m_discord_lobby_id = 0;
	bool m_hosting;
	int m_expected_players;
	unsigned short m_port;
	std::string m_announcement_game;
	bool m_announcement_sent = false;
	discord_lobby m_lobby;
	std::mutex m_mutex;
	SimpleWeb::Server<SimpleWeb::HTTP> m_server;
	std::thread m_server_thread;
};

} // namespace mamehub

#endif
