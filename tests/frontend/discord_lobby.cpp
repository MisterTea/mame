#include "catch.hpp"

#include "discord_lobby.h"

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
