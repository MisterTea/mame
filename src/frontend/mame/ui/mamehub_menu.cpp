// license:BSD-3-Clause

#include "emu.h"
#include "ui/mamehub_menu.h"

#include "ui/menu.h"
#include "ui/optsmenu.h"
#include "ui/selgame.h"
#include "ui/ui.h"

#include "mame.h"
#include "mameopts.h"
#include "mamehub.h"
#include "NSM_Common.h"

#include "drivenum.h"
#include "emuopts.h"

#include <future>

namespace ui {

namespace {

enum
{
	ITEM_HOST = 1,
	ITEM_JOIN,
	ITEM_OFFLINE,
	ITEM_OPTIONS,
	ITEM_EXIT,
	ITEM_REFRESH = 500,
	ITEM_START_GAME = 600,
	ITEM_CANCEL_LOBBY,
	ITEM_LEAVE_LOBBY
};

} // anonymous namespace

//-------------------------------------------------
//  menu_mamehub_main
//-------------------------------------------------

menu_mamehub_main::menu_mamehub_main(mame_ui_manager &mui, render_container &container)
	: menu(mui, container)
{
	set_needs_prev_menu_item(false);
	set_heading(_("MAMEHub Netplay (Discord)"));
}

void menu_mamehub_main::force_menu(mame_ui_manager &mui, render_container &container)
{
	menu::stack_reset(mui);
	menu::stack_push_special_main<menu_mamehub_main>(mui, container);
	mui.show_menu();
	mui.machine().pause();
}

void menu_mamehub_main::populate()
{
	if (mamehub::discord_service::instance().is_authenticated())
	{
		auto const &identity = mamehub::discord_service::instance().current_identity();
		item_append(string_format(_("Signed in as: %s"), identity.display_name), FLAG_DISABLE, nullptr);
		item_append(menu_item_type::SEPARATOR);
	}

	item_append(_("Host Game"), 0, (void *)ITEM_HOST);
	item_append(_("Join Game"), 0, (void *)ITEM_JOIN);
	item_append(_("Select Game (Offline)"), 0, (void *)ITEM_OFFLINE);
	item_append(menu_item_type::SEPARATOR);
	item_append(_("General Settings"), 0, (void *)ITEM_OPTIONS);
	item_append(_("Exit"), 0, (void *)ITEM_EXIT);
}

bool menu_mamehub_main::handle(event const *ev)
{
	if (ev && (IPT_UI_SELECT == ev->iptkey))
	{
		switch (uintptr_t(ev->itemref))
		{
		case ITEM_HOST:
			menu::stack_push<menu_select_game>(ui(), container(), nullptr, [this] (game_driver const &driver)
			{
				menu::stack_push<menu_mamehub_lobby>(ui(), container(), &driver);
			});
			return true;

		case ITEM_JOIN:
			menu::stack_push<menu_mamehub_join>(ui(), container());
			return true;

		case ITEM_OFFLINE:
			menu::stack_push<menu_select_game>(ui(), container(), nullptr);
			return true;

		case ITEM_OPTIONS:
			menu::stack_push<menu_simple_game_options>(ui(), container(), [this] () { reset(reset_options::REMEMBER_REF); });
			return true;

		case ITEM_EXIT:
			machine().schedule_exit();
			return true;
		}
	}
	return false;
}

//-------------------------------------------------
//  menu_mamehub_join
//-------------------------------------------------

menu_mamehub_join::menu_mamehub_join(mame_ui_manager &mui, render_container &container)
	: menu(mui, container)
	, m_last_poll(std::chrono::steady_clock::now())
{
	set_heading(_("Open Lobbies (Discord)"));
	mamehub::discord_discovery::instance().ensure_connected();
	mamehub::discord_discovery::instance().query_lobbies();
}

void menu_mamehub_join::menu_activated()
{
	mamehub::discord_discovery::instance().query_lobbies();
	reset(reset_options::REMEMBER_REF);
}

void menu_mamehub_join::custom_render(void *selectedref, float top, float bottom, float x, float y, float x2, float y2)
{
	auto const now = std::chrono::steady_clock::now();
	if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_poll).count() >= 500)
	{
		m_last_poll = now;
		mamehub::discord_discovery::instance().update();
		reset(reset_options::REMEMBER_REF);
	}
}

void menu_mamehub_join::populate()
{
	m_lobbies = mamehub::discord_discovery::instance().get_open_lobbies();

	if (m_lobbies.empty())
	{
		item_append(_("No open lobbies found"), FLAG_DISABLE, nullptr);
	}
	else
	{
		for (size_t i = 0; i < m_lobbies.size(); ++i)
		{
			auto const &lobby = m_lobbies[i];
			std::string title = lobby.game_title.empty() ? lobby.system_name : lobby.game_title;
			std::string desc = string_format("%s (%s)", title, lobby.system_name);
			std::string sub = string_format(_("Host: %s [%d player(s)]"), lobby.host_name, lobby.players);
			item_append(std::move(desc), std::move(sub), 0, (void *)(uintptr_t)(100 + i));
		}
	}

	item_append(menu_item_type::SEPARATOR);
	item_append(_("Refresh Lobbies"), 0, (void *)ITEM_REFRESH);
	item_append(_("Return to Previous Menu"), 0, nullptr);
}

bool menu_mamehub_join::handle(event const *ev)
{
	if (ev && (IPT_UI_SELECT == ev->iptkey))
	{
		if (uintptr_t(ev->itemref) == ITEM_REFRESH)
		{
			mamehub::discord_discovery::instance().query_lobbies();
			reset(reset_options::REMEMBER_REF);
			return true;
		}
		else if (uintptr_t(ev->itemref) >= 100)
		{
			size_t index = uintptr_t(ev->itemref) - 100;
			if (index < m_lobbies.size())
			{
				auto const &lobby = m_lobbies[index];
				menu::stack_push<menu_mamehub_lobby>(ui(), container(), lobby.secret, lobby.system_name, lobby.game_title, lobby.host_name);
				return true;
			}
		}
		else if (!ev->itemref)
		{
			stack_pop();
			return true;
		}
	}
	return false;
}

//-------------------------------------------------
//  menu_mamehub_lobby
//-------------------------------------------------

menu_mamehub_lobby::menu_mamehub_lobby(mame_ui_manager &mui, render_container &container, game_driver const *driver)
	: menu(mui, container)
	, m_is_host(true)
	, m_driver(driver)
	, m_system_name(driver ? driver->name : "")
	, m_game_title(driver ? driver->type.fullname() : "")
	, m_last_poll(std::chrono::steady_clock::now())
{
	auto const identity = mamehub::discord_service::instance().current_identity();
	m_host_name = identity.display_name.empty() ? "Host" : identity.display_name;

	// Generate unique secret
	auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	m_secret = "mamehub-" + std::to_string(identity.id) + "-" + std::to_string(now_ms);

	unsigned short dir_port = (unsigned short)ui().machine().options().discord_directory_port();
	int expected_players = ui().machine().options().discord_players();

	try
	{
		m_directory_server = std::make_unique<mamehub::discord_directory_server>(
			identity, m_secret, m_system_name, true, expected_players, dir_port);

		mamehub::discord_discovery::instance().set_my_hosted_lobby(
			m_secret, m_system_name, m_game_title, m_host_name, 1);
	}
	catch (std::exception const &ex)
	{
		ui().popup_time(3, "Failed to create lobby: %s", ex.what());
	}
}

menu_mamehub_lobby::menu_mamehub_lobby(mame_ui_manager &mui, render_container &container, std::string secret, std::string system_name, std::string game_title, std::string host_name)
	: menu(mui, container)
	, m_is_host(false)
	, m_secret(std::move(secret))
	, m_system_name(std::move(system_name))
	, m_game_title(std::move(game_title))
	, m_host_name(std::move(host_name))
	, m_last_poll(std::chrono::steady_clock::now())
{
	auto const identity = mamehub::discord_service::instance().current_identity();
	unsigned short dir_port = (unsigned short)ui().machine().options().discord_directory_port();
	int expected_players = ui().machine().options().discord_players();

	try
	{
		m_directory_server = std::make_unique<mamehub::discord_directory_server>(
			identity, m_secret, m_system_name, false, expected_players, dir_port);
	}
	catch (std::exception const &ex)
	{
		ui().popup_time(3, "Failed to join lobby: %s", ex.what());
	}
}

menu_mamehub_lobby::~menu_mamehub_lobby()
{
	if (m_is_host && !m_transitioning)
	{
		mamehub::discord_discovery::instance().close_lobby(m_secret);
		mamehub::discord_discovery::instance().clear_my_hosted_lobby();
	}
}

void menu_mamehub_lobby::custom_render(void *selectedref, float top, float bottom, float x, float y, float x2, float y2)
{
	if (m_transitioning)
		return;

	auto const now = std::chrono::steady_clock::now();
	if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_poll).count() >= 100)
	{
		m_last_poll = now;
		mamehub::discord_service::instance().pump();

		if (m_directory_server)
		{
			auto room = m_directory_server->waiting_room();
			if (!m_is_host && room.started && !m_transitioning)
			{
				start_game_as_guest();
				return;
			}
			if ((room.members.size() != m_last_member_count) || (room.can_start != m_last_can_start))
			{
				m_last_member_count = room.members.size();
				m_last_can_start = room.can_start;
				reset(reset_options::REMEMBER_REF);
			}
		}
	}
}

void menu_mamehub_lobby::populate()
{
	set_heading(string_format(_("Lobby: %s"), m_game_title.empty() ? m_system_name : m_game_title));
	item_append(string_format(_("Host: %s"), m_host_name), FLAG_DISABLE, nullptr);
	item_append(menu_item_type::SEPARATOR);

	if (m_directory_server)
	{
		auto room = m_directory_server->waiting_room();
		for (size_t i = 0; i < room.members.size(); ++i)
		{
			auto const &member = room.members[i];
			std::string role = (m_is_host && (i == 0)) ? "[Host]" : "[Player]";
			std::string status = member.ready ? _("Ready") : _("Connecting...");
			item_append(string_format("%s %s", role, member.display_name), status, FLAG_DISABLE, nullptr);
		}
		item_append(menu_item_type::SEPARATOR);

		if (m_is_host)
		{
			if (room.can_start)
				item_append(_("Start Game"), 0, (void *)ITEM_START_GAME);
			else
				item_append(_("Waiting for players to be ready..."), FLAG_DISABLE, nullptr);

			item_append(_("Cancel Lobby"), 0, (void *)ITEM_CANCEL_LOBBY);
		}
		else
		{
			item_append(_("Waiting for host to start game..."), FLAG_DISABLE, nullptr);
			item_append(_("Leave Lobby"), 0, (void *)ITEM_LEAVE_LOBBY);
		}
	}
	else
	{
		item_append(_("Error initializing directory server"), FLAG_DISABLE, nullptr);
		item_append(_("Exit"), 0, (void *)ITEM_CANCEL_LOBBY);
	}
}

bool menu_mamehub_lobby::handle(event const *ev)
{
	if (ev && (IPT_UI_SELECT == ev->iptkey))
	{
		switch (uintptr_t(ev->itemref))
		{
		case ITEM_START_GAME:
			if (m_is_host)
				start_game_as_host();
			return true;

		case ITEM_CANCEL_LOBBY:
		case ITEM_LEAVE_LOBBY:
			leave_and_pop();
			return true;
		}
	}
	return false;
}

void menu_mamehub_lobby::leave_and_pop()
{
	if (m_is_host)
	{
		mamehub::discord_discovery::instance().close_lobby(m_secret);
		mamehub::discord_discovery::instance().clear_my_hosted_lobby();
	}
	m_directory_server.reset();
	stack_pop();
}

void menu_mamehub_lobby::start_game_as_host()
{
	if (!m_directory_server || m_transitioning)
		return;

	m_transitioning = true;

	// Close lobby so no new players can join
	mamehub::discord_discovery::instance().close_lobby(m_secret);
	mamehub::discord_discovery::instance().clear_my_hosted_lobby();

	std::string error;
	if (!m_directory_server->start_game(error))
	{
		m_transitioning = false;
		ui().popup_time(3, "Could not start game: %s", error.c_str());
		return;
	}

	auto &options = ui().machine().options();
	std::string userId = std::to_string(mamehub::discord_service::instance().current_identity().id);
	std::string privateKey = options.password();
	unsigned short peerPort = (unsigned short)options.port();
	unsigned short dirPort = m_directory_server->port();
	bool fakeLag = options.fake_lag();
	int connectTimeout = options.direct_connect_timeout();
	std::string gameString = m_system_name;

	deleteNetCommon();

	auto connection = std::async(std::launch::async, [userId, privateKey, peerPort, dirPort, gameString, fakeLag, connectTimeout] {
		return createNetCommon(userId, privateKey, peerPort, "", dirPort, 50, gameString, fakeLag, connectTimeout);
	});

	// Wait briefly for direct connections
	connection.wait_for(std::chrono::seconds(connectTimeout));

	// Keep directory server alive in manager
	mamehub_manager::instance()->set_discord_directory(std::move(m_directory_server));

	// Launch driver
	options.set_system_name(m_system_name);
	if (m_driver)
		mame_machine_manager::instance()->schedule_new_driver(*m_driver);
	ui().machine().schedule_hard_reset();
	menu::stack_reset(ui());
}

void menu_mamehub_lobby::start_game_as_guest()
{
	if (!m_directory_server || m_transitioning)
		return;

	m_transitioning = true;

	int driver_index = driver_list::find(m_system_name.c_str());
	if (driver_index < 0)
	{
		ui().popup_time(5, "Game not found: %s", m_system_name.c_str());
		m_transitioning = false;
		return;
	}
	game_driver const &driver = driver_list::driver(driver_index);

	auto &options = ui().machine().options();
	std::string userId = std::to_string(mamehub::discord_service::instance().current_identity().id);
	std::string privateKey = options.password();
	unsigned short peerPort = (unsigned short)options.port();
	unsigned short dirPort = m_directory_server->port();
	bool fakeLag = options.fake_lag();
	int connectTimeout = options.direct_connect_timeout();
	std::string gameString = m_system_name;

	deleteNetCommon();

	auto connection = std::async(std::launch::async, [userId, privateKey, peerPort, dirPort, gameString, fakeLag, connectTimeout] {
		return createNetCommon(userId, privateKey, peerPort, "", dirPort, 50, gameString, fakeLag, connectTimeout);
	});

	// Wait briefly for direct connections
	connection.wait_for(std::chrono::seconds(connectTimeout));

	// Keep directory server alive in manager
	mamehub_manager::instance()->set_discord_directory(std::move(m_directory_server));

	// Launch driver
	options.set_system_name(m_system_name);
	mame_machine_manager::instance()->schedule_new_driver(driver);
	ui().machine().schedule_hard_reset();
	menu::stack_reset(ui());
}

} // namespace ui
