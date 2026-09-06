// license:BSD-3-Clause

#include "emu.h"
#include "ui/mamehub_menu.h"

#include "ui/menu.h"
#include "ui/optsmenu.h"
#include "ui/selgame.h"
#include "ui/selsoft.h"
#include "ui/systemlist.h"
#include "ui/ui.h"

#include "mame.h"
#include "mameopts.h"
#include "mamehub.h"
#include "NSM_Common.h"
#include "PortMappingHandler.hpp"

#include "drivenum.h"
#include "emuopts.h"
#include "softlist_dev.h"
#include "util/corestr.h"

#include <algorithm>
#include <future>
#include <string_view>
#include <unordered_set>

namespace ui {

namespace {

enum
{
	ITEM_HOST = 1,
	ITEM_JOIN,
	ITEM_OFFLINE,
	ITEM_OPTIONS,
	ITEM_EXIT,
	ITEM_ARCADE = 100,
	ITEM_REFRESH = 500,
	ITEM_START_GAME = 600,
	ITEM_CANCEL_LOBBY,
	ITEM_LEAVE_LOBBY
};

// Relative present-day popularity for recognizable software platforms.  Keep
// these values constant so menu order is deterministic; aliases and regional
// variants intentionally share a score.  Every machine not listed here has a
// score of zero.
struct machine_popularity
{
	std::string_view short_name;
	int score;
};

constexpr machine_popularity MACHINE_POPULARITY[] = {
	{ "snes",       100 }, { "snespal",     99 },
	{ "nes",         98 }, { "nespal",      98 },
	{ "genesis",     96 }, { "megadriv",    96 },
	{ "psu",         94 }, { "psj",         94 }, { "pse", 94 },
	{ "n64",         92 },
	{ "gba",         90 },
	{ "gameboy",     88 }, { "gbcolor",     88 }, { "gbpocket", 88 },
	{ "dc",          84 },
	{ "saturn",      82 }, { "saturneu",    82 }, { "saturnjp", 82 },
	{ "c64",         80 }, { "c64pal",      80 },
	{ "a500",        78 }, { "a500p",       78 }, { "a1200", 78 },
	{ "pce",         76 }, { "tg16",        76 },
	{ "aes",         74 }, { "neocdz",      74 },
	{ "msx2",        72 }, { "msx2p",       72 },
	{ "spectrum",    70 }, { "spec128",     70 },
	{ "a2600",       68 }, { "a2600p",      68 },
	{ "coleco",      64 },
	{ "intv",        62 },
	{ "sms",         60 }, { "sms1",        60 }, { "smspal", 60 },
	{ "ggm",         58 }, { "gamegear",    58 },
	{ "lynx",        54 },
	{ "jaguar",      52 },
	{ "3do",         50 },
	{ "cdimono1",    46 },
	{ "apple2e",     42 }, { "apple2gs",    42 },
	{ "bbcb",        38 },
	{ "vic20",       36 },
	{ "cpc464",      34 }, { "cpc6128",     34 },
	{ "x68000",      32 },
	{ "fmtowns",     30 }
};

constexpr int popularity_score(std::string_view short_name)
{
	for (machine_popularity const &entry : MACHINE_POPULARITY)
		if (entry.short_name == short_name)
			return entry.score;
	return 0;
}

constexpr bool has_suffix(std::string_view text, std::string_view suffix)
{
	return (text.size() >= suffix.size()) && (text.substr(text.size() - suffix.size()) == suffix);
}

int popularity_score(game_driver const &driver)
{
	std::string_view const short_name(driver.name);
	std::string_view const description(driver.type.fullname());
	bool const is_pal = has_suffix(short_name, "pal") || (description.find("PAL") != std::string_view::npos);

	// Doubling leaves room for the regional tie-break without changing the
	// relative order of different base popularity levels.
	return (popularity_score(short_name) * 2) - (is_pal ? 1 : 0);
}

} // anonymous namespace

class mamehub_machine_catalog
{
public:
	explicit mamehub_machine_catalog(mame_ui_manager &mui)
	{
		auto &systems = system_list::instance();
		systems.cache_data(mui.options());
		for (ui_system_info const &system : systems.sorted_list())
		{
			bool has_software = false;
			try
			{
				machine_config config(*system.driver, mui.machine().options());
				for (software_list_device &swlist : software_list_device_enumerator(config.root_device()))
				{
					(void)swlist;
					has_software = true;
					break;
				}
			}
			catch (...)
			{
			}

			if (has_software)
				m_software_machines.push_back(system.driver);
			else
				m_arcade_machines.emplace(system.driver);
		}

		std::stable_sort(
				m_software_machines.begin(),
				m_software_machines.end(),
				[] (game_driver const *left, game_driver const *right)
				{
					int const left_score = popularity_score(*left);
					int const right_score = popularity_score(*right);
					return left_score != right_score
							? left_score > right_score
							: core_stricmp(left->type.fullname(), right->type.fullname()) < 0;
				});
	}

	std::vector<game_driver const *> const &software_machines() const { return m_software_machines; }
	bool is_arcade(game_driver const &driver) const { return m_arcade_machines.find(&driver) != m_arcade_machines.end(); }

private:
	std::vector<game_driver const *> m_software_machines;
	std::unordered_set<game_driver const *> m_arcade_machines;
};

static mamehub_machine_catalog &machine_catalog(mame_ui_manager &mui)
{
	return mui.get_session_data<mamehub_machine_catalog, mamehub_machine_catalog>(mui);
}

//-------------------------------------------------
//  menu_mamehub_main
//-------------------------------------------------

menu_mamehub_main::menu_mamehub_main(mame_ui_manager &mui, render_container &container)
	: menu(mui, container)
{
	set_needs_prev_menu_item(false);
	set_heading(_("MAMEHub Netplay (Discord)"));
}

void menu_mamehub_main::menu_activated()
{
	reset(reset_options::SELECT_FIRST);
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
			menu::stack_push<menu_mamehub_machine>(ui(), container());
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

bool menu_mamehub_main::custom_mouse_down()
{
	int const h = hover();
	if (h >= 0 && h < item_count() && is_selectable(item(h)))
	{
		set_selected_index(h);
		switch (uintptr_t(item(h).ref()))
		{
		case ITEM_HOST:
			menu::stack_push<menu_mamehub_machine>(ui(), container());
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
//  menu_mamehub_machine
//-------------------------------------------------

menu_mamehub_machine::menu_mamehub_machine(mame_ui_manager &mui, render_container &container)
	: menu(mui, container)
	, m_software_machines(machine_catalog(mui).software_machines())
{
}

void menu_mamehub_machine::populate()
{
	set_heading(m_search.empty() ? _("Select Machine") : string_format(_("Select Machine - Search: %s_"), m_search));
	auto const matches_prefix = [this] (char const *text)
	{
		return !core_strnicmp(text, m_search.c_str(), m_search.size());
	};

	if (m_search.empty() || matches_prefix(_("Arcade")))
		item_append(_("Arcade"), 0, (void *)ITEM_ARCADE);
	for (game_driver const *driver : m_software_machines)
	{
		if (m_search.empty() || matches_prefix(driver->type.fullname()) || matches_prefix(driver->name))
			item_append(driver->type.fullname(), driver->name, 0, (void *)driver);
	}
}

bool menu_mamehub_machine::handle(event const *ev)
{
	if (ev && (IPT_UI_PASTE == ev->iptkey))
	{
		if (paste_text(m_search, uchar_is_printable))
			reset(reset_options::SELECT_FIRST);
		return true;
	}
	else if (ev && (IPT_SPECIAL == ev->iptkey))
	{
		if (input_character(m_search, ev->unichar, uchar_is_printable))
			reset(reset_options::SELECT_FIRST);
		return true;
	}
	else if (ev && (IPT_UI_CANCEL == ev->iptkey) && !m_search.empty())
	{
		m_search.clear();
		reset(reset_options::SELECT_FIRST);
		return true;
	}

	if (!ev || (IPT_UI_SELECT != ev->iptkey) || !ev->itemref)
		return false;

	if (uintptr_t(ev->itemref) == ITEM_ARCADE)
	{
		menu::stack_push<menu_select_game>(
				ui(), container(), nullptr,
				[this] (game_driver const &driver)
				{
					menu::stack_push<menu_mamehub_lobby>(ui(), container(), &driver);
				},
				[this] (game_driver const &driver) { return machine_catalog(ui()).is_arcade(driver); });
		return true;
	}

	game_driver const *const driver = reinterpret_cast<game_driver const *>(ev->itemref);
	auto const &systems = system_list::instance().systems();
	int const index = driver_list::find(driver->name);
	if ((index < 0) || (std::size_t(index) >= systems.size()))
		return false;

	menu::stack_push<menu_select_software>(
			ui(), container(), systems[index],
			[this] (game_driver const &selected_driver, ui_software_info const &software)
			{
				menu::stack_push<menu_mamehub_lobby>(ui(), container(), &selected_driver, &software);
			});
	return true;
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
				menu::stack_push<menu_mamehub_lobby>(ui(), container(), lobby.secret, lobby.system_name, lobby.software_name, lobby.game_title, lobby.host_name);
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

menu_mamehub_lobby::menu_mamehub_lobby(mame_ui_manager &mui, render_container &container, game_driver const *driver, ui_software_info const *software)
	: menu(mui, container)
	, m_is_host(true)
	, m_driver(driver)
	, m_system_name(driver ? driver->name : "")
	, m_software_name((software && !software->startempty) ? string_format("%s:%s", software->listname, software->shortname) : "")
	, m_game_title((software && !software->startempty) ? software->longname : (driver ? driver->type.fullname() : ""))
	, m_last_poll(std::chrono::steady_clock::now())
{
	auto const identity = mamehub::discord_service::instance().current_identity();
	m_host_name = identity.display_name.empty() ? "Host" : identity.display_name;

	// Generate unique secret
	auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	m_secret = "mamehub-" + std::to_string(identity.id) + "-" + std::to_string(now_ms);

	unsigned short dir_port = (unsigned short)ui().machine().options().discord_directory_port();
	int expected_players = ui().machine().options().discord_players();

	set_needs_prev_menu_item(false);

	try
	{
		if (!mamehub::discord_discovery::instance().ensure_connected())
			throw emu_fatalerror("Failed to connect to lobby discovery");

		m_directory_server = std::make_unique<mamehub::discord_directory_server>(
			identity, m_secret, m_system_name + ";" + m_software_name, true, expected_players, dir_port, m_game_title);

		mamehub::discord_discovery::instance().set_my_hosted_lobby(
			m_secret, m_system_name, m_software_name, m_game_title, m_host_name, 1);
		begin_peer_connection();
	}
	catch (std::exception const &ex)
	{
		ui().popup_time(3, "Failed to create lobby: %s", ex.what());
	}
}

menu_mamehub_lobby::menu_mamehub_lobby(mame_ui_manager &mui, render_container &container, std::string secret, std::string system_name, std::string software_name, std::string game_title, std::string host_name)
	: menu(mui, container)
	, m_is_host(false)
	, m_secret(std::move(secret))
	, m_system_name(std::move(system_name))
	, m_software_name(std::move(software_name))
	, m_game_title(std::move(game_title))
	, m_host_name(std::move(host_name))
	, m_last_poll(std::chrono::steady_clock::now())
{
	set_needs_prev_menu_item(false);

	auto const identity = mamehub::discord_service::instance().current_identity();
	unsigned short dir_port = (unsigned short)ui().machine().options().discord_directory_port();
	int expected_players = ui().machine().options().discord_players();

	try
	{
		m_directory_server = std::make_unique<mamehub::discord_directory_server>(
			identity, m_secret, m_system_name + ";" + m_software_name, false, expected_players, dir_port);
		begin_peer_connection();
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
	if (!m_transitioning)
	{
		abortNetCommon();
		deleteNetCommon();
	}
}

void menu_mamehub_lobby::custom_render(void *selectedref, float top, float bottom, float x, float y, float x2, float y2)
{
	if (m_transitioning)
	{
		// Poll the async createNetCommon connection
		if (m_connection.valid())
		{
			mamehub::discord_service::instance().pump();
			bool const ready = (m_connection.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
			bool const timed_out = (std::chrono::steady_clock::now() >= m_connect_deadline);
			if (ready || timed_out)
				finish_connection();
		}
		return;
	}

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
			int other_players = 0;
			for (auto const &member : room.members)
			{
				if (member.discord_id != m_directory_server->host_id())
					other_players++;
			}
			bool const can_start = room.can_start;
			if ((room.members.size() != m_last_member_count) || (other_players != m_last_other_players) || (can_start != m_last_can_start))
			{
				bool const just_became_startable = (m_is_host && can_start && !m_last_can_start);
				if (m_is_host && (room.members.size() != m_last_member_count))
				{
					mamehub::discord_discovery::instance().set_my_hosted_lobby(
						m_secret, m_system_name, m_software_name, m_game_title, m_host_name, (int)room.members.size());
				}
				m_last_member_count = room.members.size();
				m_last_other_players = other_players;
				m_last_can_start = can_start;
				reset(just_became_startable ? reset_options::SELECT_FIRST : reset_options::REMEMBER_REF);
			}
		}
	}
}

void menu_mamehub_lobby::populate()
{
	set_heading(string_format(_("Lobby: %s"), m_game_title.empty() ? m_system_name : m_game_title));

	if (m_transitioning)
	{
		item_append(_("Connecting to peers..."), FLAG_DISABLE, nullptr);
		return;
	}

	item_append(string_format(_("Host: %s"), m_host_name), FLAG_DISABLE, nullptr);

	if (m_directory_server)
	{
		auto room = m_directory_server->waiting_room();
		int other_players = 0;
		for (auto const &member : room.members)
		{
			if (member.discord_id != m_directory_server->host_id())
				other_players++;
		}

		item_append(string_format(_("Players Connected: %d"), room.members.size()), FLAG_DISABLE, nullptr);
		item_append(menu_item_type::SEPARATOR);

		for (size_t i = 0; i < room.members.size(); ++i)
		{
			auto const &member = room.members[i];
			bool const is_member_host = (member.discord_id == m_directory_server->host_id());
			std::string role = is_member_host ? "[Host]" : "[Player]";
			item_append(string_format("%s %s", role, member.display_name), _("Connected"), FLAG_DISABLE, nullptr);
		}
		item_append(menu_item_type::SEPARATOR);

		if (m_is_host)
		{
			// Require directory-level readiness (every peer published endpoints)
			// so Start Game matches createNetCommon's pre-start registration.
			if (room.can_start)
				item_append(_("Start Game"), 0, (void *)ITEM_START_GAME);
			else if (other_players >= 1)
				item_append(_("Waiting for peer connections..."), FLAG_DISABLE, nullptr);
			else
				item_append(_("Waiting for players to connect..."), FLAG_DISABLE, nullptr);

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
	if (m_transitioning)
	{
		if (m_connection.valid())
		{
			mamehub::discord_service::instance().pump();
			bool const ready = (m_connection.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
			bool const timed_out = (std::chrono::steady_clock::now() >= m_connect_deadline);
			if (ready || timed_out)
			{
				finish_connection();
				return true;
			}
		}
		return false;
	}

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

bool menu_mamehub_lobby::custom_mouse_down()
{
	int const h = hover();
	if (h >= 0 && h < item_count() && is_selectable(item(h)))
	{
		set_selected_index(h);
		auto ref = uintptr_t(item(h).ref());
		if (ref == ITEM_START_GAME)
		{
			if (m_is_host)
				start_game_as_host();
			return true;
		}
		else if (ref == ITEM_CANCEL_LOBBY || ref == ITEM_LEAVE_LOBBY)
		{
			leave_and_pop();
			return true;
		}
	}
	return false;
}

bool menu_mamehub_lobby::custom_ui_back()
{
	leave_and_pop();
	return true;
}

void menu_mamehub_lobby::leave_and_pop()
{
	if (m_is_host)
	{
		mamehub::discord_discovery::instance().close_lobby(m_secret);
		mamehub::discord_discovery::instance().clear_my_hosted_lobby();
	}
	abortNetCommon();
	deleteNetCommon();
	m_directory_server.reset();
	stack_pop_to_special_main();
}

void menu_mamehub_lobby::begin_peer_connection()
{
	if (!m_directory_server || m_peer_connection_started)
		return;

	m_peer_connection_started = true;

	auto &options = ui().machine().options();
	std::string userId = std::to_string(mamehub::discord_service::instance().current_identity().id);
	std::string privateKey = options.password();
	unsigned short peerPort = (unsigned short)options.port();
	unsigned short dirPort = m_directory_server->port();
	bool fakeLag = options.fake_lag();
	// createNetCommon blocks until the host publishes start; allow time for
	// lobby wait + STUN/port-mapping beyond the post-start mesh timeout.
	int connectTimeout = std::max(options.direct_connect_timeout(), 180);
	std::string gameString = m_system_name + ";" + m_software_name;

	deleteNetCommon();

	if (mamehub::discord_service::is_mock_enabled())
		wga::DISABLE_PORT_MAPPING = true;

	// Match the CLI waiting-room order: register peer keys/endpoints first,
	// then block inside createNetCommon until the host publishes start.
	m_connection = std::async(std::launch::async, [userId, privateKey, peerPort, dirPort, gameString, fakeLag, connectTimeout] {
		createNetCommon(userId, privateKey, peerPort, "", dirPort, 50, gameString, fakeLag, connectTimeout);
	});
	m_connect_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(connectTimeout);
}

void menu_mamehub_lobby::start_game_as_host()
{
	if (!m_directory_server || m_transitioning)
		return;

	begin_peer_connection();

	// Close lobby listing so no new players can join the UI browser
	mamehub::discord_discovery::instance().close_lobby(m_secret);
	mamehub::discord_discovery::instance().clear_my_hosted_lobby();

	std::string error;
	if (!m_directory_server->start_game(error))
	{
		ui().popup_time(3, "Could not start game: %s", error.c_str());
		return;
	}

	m_transitioning = true;
	m_connect_deadline = std::chrono::steady_clock::now() +
		std::chrono::seconds(ui().machine().options().direct_connect_timeout());
	// Refresh the menu to show "Connecting..." status
	reset(reset_options::SELECT_FIRST);
}

void menu_mamehub_lobby::start_game_as_guest()
{
	if (!m_directory_server || m_transitioning)
		return;

	int driver_index = driver_list::find(m_system_name.c_str());
	if (driver_index < 0)
	{
		ui().popup_time(5, "Game not found: %s", m_system_name.c_str());
		return;
	}
	m_driver = &driver_list::driver(driver_index);

	begin_peer_connection();
	m_transitioning = true;
	m_connect_deadline = std::chrono::steady_clock::now() +
		std::chrono::seconds(ui().machine().options().direct_connect_timeout());

	// Refresh the menu to show "Connecting..." status
	reset(reset_options::SELECT_FIRST);
}

void menu_mamehub_lobby::finish_connection()
{
	// Retrieve any exception from the async task
	if (m_connection.valid())
	{
		if (m_connection.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
		{
			ui().popup_time(5, "Connection timed out waiting for peers");
			m_transitioning = false;
			reset(reset_options::SELECT_FIRST);
			return;
		}
		try
		{
			m_connection.get();
		}
		catch (std::exception const &ex)
		{
			ui().popup_time(5, "Connection failed: %s", ex.what());
			m_transitioning = false;
			reset(reset_options::SELECT_FIRST);
			return;
		}
	}

	// Keep directory server alive in manager
	mamehub_manager::instance()->set_discord_directory(std::move(m_directory_server));

	// Launch driver
	auto &options = ui().machine().options();
	options.set_system_name(m_system_name);
	if (!m_software_name.empty())
	{
		options.set_value(OPTION_SOFTWARENAME, m_software_name, OPTION_PRIORITY_CMDLINE);
		options.set_software(std::string(m_software_name));
	}
	if (m_driver)
		mame_machine_manager::instance()->schedule_new_driver(*m_driver);
	menu::stack_reset(ui());
	ui().machine().resume();
	ui().machine().schedule_hard_reset();
}

} // namespace ui
