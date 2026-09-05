// license:BSD-3-Clause

#ifndef MAME_FRONTEND_MAME_DISCORD_SERVICE_H
#define MAME_FRONTEND_MAME_DISCORD_SERVICE_H

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mamehub {

struct discord_identity
{
	std::uint64_t id = 0;
	std::string display_name;
};

struct discord_message
{
	std::uint64_t lobby_id = 0;
	std::uint64_t author_id = 0;
	std::string content;
};

// Blocking facade around the callback-driven Discord Social SDK.  It is
// initialized before MAME creates its window and remains alive for the process.
class discord_service
{
public:
	static discord_service &instance();
	~discord_service();

	discord_service(discord_service const &) = delete;
	discord_service &operator=(discord_service const &) = delete;

	bool authenticate(discord_identity &identity, std::string &error);
	bool is_authenticated() const;
	discord_identity const &current_identity() const;
	bool create_or_join_lobby(std::string const &secret, std::uint64_t &lobby_id, std::string &error);
	bool leave_lobby(std::uint64_t lobby_id, std::string &error);
	bool send_lobby_message(std::uint64_t lobby_id, std::string const &content, std::string &error);
	bool get_lobby_messages(std::uint64_t lobby_id, std::vector<discord_message> &messages, std::string &error);
	std::vector<discord_message> drain_messages();
	void pump();
	static void set_mock_user(std::string const &name);
	static void reset_mock();
	static void clear_mock_storage();
	static bool is_mock_enabled();
	static void clear_cached_token();
	static bool has_cached_token();
	static void override_token_file_path(std::string const &path);

private:
	discord_service();
	class implementation;
	std::unique_ptr<implementation> m_impl;
};

} // namespace mamehub

#endif
