// license:BSD-3-Clause

#ifndef MAME_FRONTEND_UI_MAMEHUB_MENU_H
#define MAME_FRONTEND_UI_MAMEHUB_MENU_H

#pragma once

#include "ui/menu.h"

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class game_driver;
struct ui_software_info;

#if !defined(__EMSCRIPTEN__)
#include "discord_directory_server.h"
#include "discord_discovery.h"
#include "discord_service.h"
#endif

namespace ui {

// Top-level MAMEHub Main Menu with Host, Join, Offline, Options, Exit
class menu_mamehub_main : public menu
{
public:
	menu_mamehub_main(mame_ui_manager &mui, render_target &target);
	virtual ~menu_mamehub_main() override = default;

	static void force_menu(mame_ui_manager &mui, render_target &target);

protected:
	virtual void populate() override;
	virtual bool handle(event const *ev) override;
	virtual void menu_activated() override;

#if defined(__EMSCRIPTEN__)
private:
	bool m_opened_softlist = false;
#endif
};

#if defined(__EMSCRIPTEN__)
// Lightweight SNES software picker for browser builds.
// Avoids menu_select_software — building/drawing the full softlist freezes WASM.
class menu_mamehub_browser_software : public menu
{
public:
	menu_mamehub_browser_software(mame_ui_manager &mui, render_target &target, game_driver const &driver);
	virtual ~menu_mamehub_browser_software() override = default;

protected:
	virtual void populate() override;
	virtual bool handle(event const *ev) override;
	virtual bool custom_ui_back() override { return !m_search.empty(); }

private:
	void launch_selected(std::string const &shortname);

	std::string m_search;
	std::vector<std::pair<std::string, std::string>> m_entries; // shortname, title
};
#endif

#if !defined(__EMSCRIPTEN__)
// First level of host selection: Arcade or a machine with software lists.
class menu_mamehub_machine : public menu
{
public:
	menu_mamehub_machine(mame_ui_manager &mui, render_target &target, bool offline_launch = false);
	virtual ~menu_mamehub_machine() override = default;

protected:
	virtual void populate() override;
	virtual bool handle(event const *ev) override;
	virtual bool custom_ui_back() override { return !m_search.empty(); }

private:
	std::vector<game_driver const *> m_software_machines;
	bool m_offline_launch;
	std::string m_search;
};

// Menu to browse and join open Discord lobbies
class menu_mamehub_join : public menu
{
public:
	menu_mamehub_join(mame_ui_manager &mui, render_target &target);
	virtual ~menu_mamehub_join() override = default;

protected:
	virtual void populate() override;
	virtual bool handle(event const *ev) override;
	virtual void menu_activated() override;
	virtual void custom_render(uint32_t flags, void *selectedref, float top, float bottom, float origx1, float origy1, float origx2, float origy2) override;

private:
	std::vector<mamehub::open_lobby_info> m_lobbies;
	std::chrono::steady_clock::time_point m_last_poll;
};

// Lobby Waiting Room (for both Host and Guest)
class menu_mamehub_lobby : public menu
{
public:
	// Host constructor
	menu_mamehub_lobby(mame_ui_manager &mui, render_target &target, game_driver const *driver, ui_software_info const *software = nullptr);
	// Guest constructor
	menu_mamehub_lobby(mame_ui_manager &mui, render_target &target, std::string secret, std::string system_name, std::string software_name, std::string game_title, std::string host_name);
	virtual ~menu_mamehub_lobby() override;

protected:
	virtual void populate() override;
	virtual bool handle(event const *ev) override;
	virtual void custom_render(uint32_t flags, void *selectedref, float top, float bottom, float origx1, float origy1, float origx2, float origy2) override;
	virtual bool custom_ui_back() override;

private:
	void begin_peer_connection();
	void start_game_as_host();
	void start_game_as_guest();
	void finish_connection();
	void leave_and_pop();

	bool m_is_host;
	game_driver const *m_driver = nullptr;
	std::string m_secret;
	std::string m_system_name;
	std::string m_software_name;
	std::string m_game_title;
	std::string m_host_name;

	std::unique_ptr<mamehub::discord_directory_server> m_directory_server;
	std::chrono::steady_clock::time_point m_last_poll;
	std::size_t m_last_member_count = 0;
	int m_last_other_players = -1;
	bool m_last_can_start = false;
	bool m_peer_connection_started = false;
	bool m_transitioning = false;

	// Async connection state (non-blocking createNetCommon).
	// Started when entering the lobby (same order as the CLI waiting room);
	// createNetCommon blocks until the host publishes the start message.
	std::future<void> m_connection;
	std::chrono::steady_clock::time_point m_connect_deadline;
};
#endif // !__EMSCRIPTEN__

} // namespace ui

#endif
