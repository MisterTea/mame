#include "catch.hpp"

#include "discord_discovery.h"
#include "discord_lobby.h"
#include "discord_service.h"

TEST_CASE("Discord lobby requires authenticated, ready peers", "[mamehub][discord]")
{
	mamehub::discord_lobby host("lobby-1", "host", "host", "sf2");
	mamehub::discord_lobby guest("lobby-1", "guest", "host", "sf2");

	auto const host_join = host.make_join_message("Host", "host-key");
	auto const guest_join = guest.make_join_message("Guest", "guest-key");
	REQUIRE(host.receive("host", host_join));
	REQUIRE(host.receive("guest", guest_join));
	REQUIRE_FALSE(host.can_start());

	REQUIRE(host.receive("host", host.make_discovery_message("host-key", { "192.0.2.1:5805", "198.51.100.1:62000" })));
	REQUIRE(host.receive("guest", guest.make_discovery_message("guest-key", { "192.0.2.2:5805" })));
	REQUIRE(host.can_connect());
	REQUIRE_FALSE(host.can_start());

	REQUIRE(host.receive("host", host.make_ready_message()));
	REQUIRE_FALSE(host.can_start());
	REQUIRE(host.receive("guest", guest.make_ready_message()));
	REQUIRE(host.can_start());
	REQUIRE(host.receive("host", host.make_start_message()));
	REQUIRE(host.started());
}

TEST_CASE("Discord lobby rejects identity spoofing and premature starts", "[mamehub][discord]")
{
	mamehub::discord_lobby host("lobby-1", "host", "host", "sf2");
	mamehub::discord_lobby guest("lobby-1", "guest", "host", "sf2");

	REQUIRE_FALSE(host.receive("attacker", guest.make_join_message("Guest", "guest-key")));
	REQUIRE(host.last_error() == "Discord sender does not match message identity");
	REQUIRE_FALSE(host.receive("guest", guest.make_start_message()));
	REQUIRE(host.last_error() == "host tried to start before every player was ready");
}

TEST_CASE("A joining player learns the game and host from the authenticated host announcement", "[mamehub][discord]")
{
	mamehub::discord_lobby host("lobby-1", "host", "host", "sf2");
	mamehub::discord_lobby guest("lobby-1", "guest", "", "");

	REQUIRE(guest.receive("host", host.make_host_message()));
	REQUIRE(guest.host_id() == "host");
	REQUIRE(guest.game_name() == "sf2");
	REQUIRE_FALSE(guest.receive("attacker", host.make_start_message()));
}

TEST_CASE("Once the host starts the game, the lobby is closed to new players", "[mamehub][discord]")
{
	mamehub::discord_lobby host("lobby-1", "host", "host", "sf2");
	mamehub::discord_lobby guest1("lobby-1", "guest1", "host", "sf2");
	mamehub::discord_lobby guest2("lobby-1", "guest2", "host", "sf2");

	REQUIRE(host.receive("host", host.make_join_message("Host", "host-key")));
	REQUIRE(host.receive("guest1", guest1.make_join_message("Guest 1", "guest1-key")));
	REQUIRE(host.receive("host", host.make_discovery_message("host-key", { "192.0.2.1:5805" })));
	REQUIRE(host.receive("guest1", guest1.make_discovery_message("guest1-key", { "192.0.2.2:5805" })));
	REQUIRE(host.can_connect());
	REQUIRE_FALSE(host.can_start());
	REQUIRE(host.receive("host", host.make_ready_message()));
	REQUIRE(host.receive("guest1", guest1.make_ready_message()));
	REQUIRE(host.can_start());

	// Host starts the game
	REQUIRE(host.receive("host", host.make_start_message()));
	REQUIRE(host.started());

	// New player tries to join an already-started lobby
	REQUIRE_FALSE(host.receive("guest2", guest2.make_join_message("Guest 2", "guest2-key")));
	REQUIRE(host.last_error() == "lobby is closed to new players");

	// Existing members may re-register (crypto peer keys) after start
	REQUIRE(host.receive("guest1", guest1.make_join_message("Guest 1", "guest1-crypto-key")));
	REQUIRE(host.members().at("guest1").public_key == "guest1-crypto-key");
}

TEST_CASE("Players can leave the lobby before start", "[mamehub][discord]")
{
	mamehub::discord_lobby host("lobby-1", "host", "host", "sf2");
	mamehub::discord_lobby guest("lobby-1", "guest", "host", "sf2");

	REQUIRE(host.receive("host", host.make_join_message("Host", "host-key")));
	REQUIRE(host.receive("guest", guest.make_join_message("Guest", "guest-key")));
	REQUIRE(host.members().size() == 2);

	// Guest leaves
	REQUIRE(host.receive("guest", guest.make_leave_message()));
	REQUIRE(host.members().size() == 1);
	REQUIRE_FALSE(host.can_start());
}

TEST_CASE("Publishing endpoints does not make a Discord lobby ready to start", "[mamehub][discord]")
{
	mamehub::discord_lobby host("lobby-1", "host", "host", "sf2");
	mamehub::discord_lobby guest("lobby-1", "guest", "host", "sf2");

	REQUIRE(host.receive("host", host.make_join_message("Host", "host-key")));
	REQUIRE(host.receive("guest", guest.make_join_message("Guest", "guest-key")));
	REQUIRE_FALSE(host.receive("guest", guest.make_ready_message()));
	REQUIRE(host.last_error() == "player became ready before publishing endpoints");

	REQUIRE(host.receive("host", host.make_discovery_message("host-key", { "192.0.2.1:5805" })));
	REQUIRE(host.receive("guest", guest.make_discovery_message("guest-key", { "192.0.2.2:5805" })));
	REQUIRE(host.can_connect());
	REQUIRE_FALSE(host.can_start());
	REQUIRE_FALSE(host.receive("host", host.make_start_message()));
	REQUIRE(host.last_error() == "host tried to start before every player was ready");

	REQUIRE(host.receive("host", host.make_ready_message()));
	REQUIRE_FALSE(host.can_start());
	REQUIRE(host.receive("guest", guest.make_ready_message()));
	REQUIRE(host.can_start());
}

TEST_CASE("The host controls the expected Discord lobby player count", "[mamehub][discord]")
{
	mamehub::discord_lobby host("lobby-1", "host", "host", "xmen", 3);
	mamehub::discord_lobby guest("lobby-1", "guest1", "", "");
	mamehub::discord_lobby guest2("lobby-1", "guest2", "host", "xmen", 3);

	REQUIRE(guest.receive("host", host.make_host_message()));
	REQUIRE(guest.expected_players() == 3);
	REQUIRE(guest.receive("host", host.make_join_message("Host", "host-key")));
	REQUIRE(guest.receive("guest1", guest.make_join_message("Guest 1", "guest1-key")));
	REQUIRE(guest.receive("host", host.make_discovery_message("host-key", { "192.0.2.1:5805" })));
	REQUIRE(guest.receive("guest1", guest.make_discovery_message("guest1-key", { "192.0.2.2:5805" })));
	REQUIRE_FALSE(guest.can_connect());

	REQUIRE(guest.receive("guest2", guest2.make_join_message("Guest 2", "guest2-key")));
	REQUIRE(guest.receive("guest2", guest2.make_discovery_message("guest2-key", { "192.0.2.3:5805" })));
	REQUIRE(guest.can_connect());
}

TEST_CASE("Mock Discord service supports local loopback messaging without Discord account", "[mamehub][discord][mock]")
{
	mamehub::discord_service::clear_mock_storage();
	mamehub::discord_service::reset_mock();

	// Player 1
	mamehub::discord_service::set_mock_user("Player1");
	REQUIRE(mamehub::discord_service::is_mock_enabled());

	mamehub::discord_identity p1_ident;
	std::string err;
	REQUIRE(mamehub::discord_service::instance().authenticate(p1_ident, err));
	REQUIRE(p1_ident.display_name == "Player1");
	REQUIRE(p1_ident.id != 0);

	uint64_t lobby_id = 0;
	REQUIRE(mamehub::discord_service::instance().create_or_join_lobby("test-mock-lobby-secret", lobby_id, err));
	REQUIRE(lobby_id != 0);

	REQUIRE(mamehub::discord_service::instance().send_lobby_message(lobby_id, "hello from player 1", err));

	std::vector<mamehub::discord_message> messages;
	REQUIRE(mamehub::discord_service::instance().get_lobby_messages(lobby_id, messages, err));
	REQUIRE(messages.size() == 1);
	REQUIRE(messages[0].author_id == p1_ident.id);
	REQUIRE(messages[0].content == "hello from player 1");

	mamehub::discord_service::reset_mock();
	mamehub::discord_service::clear_mock_storage();
}

TEST_CASE("Decentralized discovery and lobby lifecycle over mock Discord transport", "[mamehub][discord][mock]")
{
	mamehub::discord_service::clear_mock_storage();
	mamehub::discord_service::reset_mock();
	mamehub::discord_discovery::instance().reset();

	// Step 1: Host creates lobby and announces to directory
	mamehub::discord_service::set_mock_user("AliceHost");
	mamehub::discord_identity host_ident;
	std::string err;
	REQUIRE(mamehub::discord_service::instance().authenticate(host_ident, err));
	REQUIRE(mamehub::discord_discovery::instance().ensure_connected());

	std::string const secret = "alice-snes-lobby-secret";
	mamehub::discord_discovery::instance().set_my_hosted_lobby(secret, "snes", "snes:smw", "Super Mario World", "AliceHost", 1);

	// Step 2: Guest connects to discovery and discovers Alice's lobby
	mamehub::discord_service::reset_mock();
	mamehub::discord_discovery::instance().reset();

	mamehub::discord_service::set_mock_user("BobGuest");
	mamehub::discord_identity guest_ident;
	REQUIRE(mamehub::discord_service::instance().authenticate(guest_ident, err));
	REQUIRE(mamehub::discord_discovery::instance().ensure_connected());

	auto open_lobbies = mamehub::discord_discovery::instance().get_open_lobbies();
	REQUIRE(open_lobbies.size() == 1);
	REQUIRE(open_lobbies[0].secret == secret);
	REQUIRE(open_lobbies[0].system_name == "snes");
	REQUIRE(open_lobbies[0].software_name == "snes:smw");
	REQUIRE(open_lobbies[0].game_title == "Super Mario World");
	REQUIRE(open_lobbies[0].host_name == "AliceHost");

	// Step 3: Host starts the game and announces lobby closure
	mamehub::discord_service::reset_mock();
	mamehub::discord_discovery::instance().reset();

	mamehub::discord_service::set_mock_user("AliceHost");
	REQUIRE(mamehub::discord_service::instance().authenticate(host_ident, err));
	REQUIRE(mamehub::discord_discovery::instance().ensure_connected());
	mamehub::discord_discovery::instance().close_lobby(secret);

	// Step 4: 3rd party Charlie discovers no open lobbies
	mamehub::discord_service::reset_mock();
	mamehub::discord_discovery::instance().reset();

	mamehub::discord_service::set_mock_user("Charlie");
	mamehub::discord_identity charlie_ident;
	REQUIRE(mamehub::discord_service::instance().authenticate(charlie_ident, err));
	REQUIRE(mamehub::discord_discovery::instance().ensure_connected());

	auto lobbies_after_close = mamehub::discord_discovery::instance().get_open_lobbies();
	REQUIRE(lobbies_after_close.empty());

	mamehub::discord_service::reset_mock();
	mamehub::discord_discovery::instance().reset();
	mamehub::discord_service::clear_mock_storage();
}

TEST_CASE("Discord service token caching utilities", "[frontend][discord]")
{
	std::string const test_token_file = "/tmp/mamehub_test_discord_token";
	mamehub::discord_service::override_token_file_path(test_token_file);

	mamehub::discord_service::clear_cached_token();
	REQUIRE(!mamehub::discord_service::has_cached_token());

	// Clean up and restore default path
	mamehub::discord_service::clear_cached_token();
	mamehub::discord_service::override_token_file_path("");
}

TEST_CASE("Analog control wire protocol serialization and parsing", "[mamehub][analog]")
{
	// Test encoding and decoding matching ioport_manager / input_manager logic
	auto encode_analog = [](int32_t rawval, int iclass, bool inc, bool dec) -> std::string {
		return std::to_string(rawval) + ":" + std::to_string(iclass) + ":" + (inc ? "1" : "0") + ":" + (dec ? "1" : "0");
	};

	auto decode_analog = [](std::string const &payload, int32_t &rawval, int &iclass, bool &inc, bool &dec) -> bool {
		int i_class = 0, inc_val = 0, dec_val = 0;
		int raw = 0;
		if (sscanf(payload.c_str(), "%d:%d:%d:%d", &raw, &i_class, &inc_val, &dec_val) == 4) {
			rawval = raw;
			iclass = i_class;
			inc = (inc_val != 0);
			dec = (dec_val != 0);
			return true;
		}
		return false;
	};

	// 1. Center / idle state
	std::string idle_payload = encode_analog(0, 0, false, false);
	REQUIRE(idle_payload == "0:0:0:0");
	int32_t rawval = -1;
	int iclass = -1;
	bool inc = true, dec = true;
	REQUIRE(decode_analog(idle_payload, rawval, iclass, inc, dec));
	REQUIRE(rawval == 0);
	REQUIRE(iclass == 0);
	REQUIRE_FALSE(inc);
	REQUIRE_FALSE(dec);

	// 2. Full positive axis deflection (e.g. analog joystick/paddle +32767, INPUT_CLASS_ABSOLUTE=2)
	std::string max_pos_payload = encode_analog(32767, 2, false, false);
	REQUIRE(max_pos_payload == "32767:2:0:0");
	REQUIRE(decode_analog(max_pos_payload, rawval, iclass, inc, dec));
	REQUIRE(rawval == 32767);
	REQUIRE(iclass == 2);
	REQUIRE_FALSE(inc);
	REQUIRE_FALSE(dec);

	// 3. Full negative axis deflection (-32768)
	std::string max_neg_payload = encode_analog(-32768, 2, false, false);
	REQUIRE(max_neg_payload == "-32768:2:0:0");
	REQUIRE(decode_analog(max_neg_payload, rawval, iclass, inc, dec));
	REQUIRE(rawval == -32768);
	REQUIRE(iclass == 2);
	REQUIRE_FALSE(inc);
	REQUIRE_FALSE(dec);

	// 4. Digital increment / decrement pulse (e.g. keyboard steering / pedal tap)
	std::string inc_payload = encode_analog(0, 0, true, false);
	REQUIRE(inc_payload == "0:0:1:0");
	REQUIRE(decode_analog(inc_payload, rawval, iclass, inc, dec));
	REQUIRE(inc == true);
	REQUIRE(dec == false);

	std::string dec_payload = encode_analog(0, 0, false, true);
	REQUIRE(dec_payload == "0:0:0:1");
	REQUIRE(decode_analog(dec_payload, rawval, iclass, inc, dec));
	REQUIRE(inc == false);
	REQUIRE(dec == true);

	// 5. Analog key naming and suffix handling
	std::string const base_id = ":IN0/P1_PADDLE";
	std::string const analog_key = "ANALOG/" + base_id;
	REQUIRE(analog_key == "ANALOG/:IN0/P1_PADDLE");

	std::string const inc_seq_key = base_id + "/INC";
	std::string const dec_seq_key = base_id + "/DEC";
	REQUIRE(inc_seq_key.substr(inc_seq_key.length() - 4) == "/INC");
	REQUIRE(dec_seq_key.substr(dec_seq_key.length() - 4) == "/DEC");
	REQUIRE(inc_seq_key.substr(0, inc_seq_key.length() - 4) == base_id);
}
