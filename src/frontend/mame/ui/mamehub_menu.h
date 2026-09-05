// license:BSD-3-Clause

#ifndef MAME_FRONTEND_UI_MAMEHUB_MENU_H
#define MAME_FRONTEND_UI_MAMEHUB_MENU_H

#pragma once

#include "ui/menu.h"
#include "discord_directory_server.h"
#include "discord_discovery.h"
#include "discord_service.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

class game_driver;

namespace ui {

// Top-level MAMEHub Main Menu with Host, Join, Offline, Options, Exit
class menu_mamehub_main : public menu
{
public:
	menu_mamehub_main(mame_ui_manager &mui, render_container &container);
	virtual ~menu_mamehub_main() override = default;

	static void force_menu(mame_ui_manager &mui, render_container &container);

protected:
	virtual void populate() override;
	virtual bool handle(event const *ev) override;
};

// Menu to browse and join open Discord lobbies
class menu_mamehub_join : public menu
{
public:
	menu_mamehub_join(mame_ui_manager &mui, render_container &container);
	virtual ~menu_mamehub_join() override = default;

protected:
	virtual void populate() override;
	virtual bool handle(event const *ev) override;
	virtual void menu_activated() override;
	virtual void custom_render(void *selectedref, float top, float bottom, float x, float y, float x2, float y2) override;

private:
	std::vector<mamehub::open_lobby_info> m_lobbies;
	std::chrono::steady_clock::time_point m_last_poll;
};

// Lobby Waiting Room (for both Host and Guest)
class menu_mamehub_lobby : public menu
{
public:
	// Host constructor
	menu_mamehub_lobby(mame_ui_manager &mui, render_container &container, game_driver const *driver);
	// Guest constructor
	menu_mamehub_lobby(mame_ui_manager &mui, render_container &container, std::string secret, std::string system_name, std::string game_title, std::string host_name);
	virtual ~menu_mamehub_lobby() override;

protected:
	virtual void populate() override;
	virtual bool handle(event const *ev) override;
	virtual void custom_render(void *selectedref, float top, float bottom, float x, float y, float x2, float y2) override;

private:
	void start_game_as_host();
	void start_game_as_guest();
	void leave_and_pop();

	bool m_is_host;
	game_driver const *m_driver = nullptr;
	std::string m_secret;
	std::string m_system_name;
	std::string m_game_title;
	std::string m_host_name;

	std::unique_ptr<mamehub::discord_directory_server> m_directory_server;
	std::chrono::steady_clock::time_point m_last_poll;
	std::size_t m_last_member_count = 0;
	bool m_last_can_start = false;
	bool m_transitioning = false;
};

} // namespace ui

#endif
