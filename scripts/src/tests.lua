-- license:BSD-3-Clause
-- copyright-holders:MAMEdev Team

---------------------------------------------------------------------------
--
--   tests.lua
--
--   Rules for building tests
--
---------------------------------------------------------------------------

project("mametests")
	uuid ("66d4c639-196b-4065-a411-7ee9266564f5")
	kind "ConsoleApp"

	flags {
		"Symbols", -- always include minimum symbols for executables
	}

	if _OPTIONS["SEPARATE_BIN"]~="1" then
		targetdir(MAME_DIR)
	end

	configuration { "Release" }
		targetsuffix "hub"
		if _OPTIONS["PROFILE"] then
			targetsuffix "hubp"
		end

	configuration { "Debug" }
		targetsuffix "hubd"
		if _OPTIONS["PROFILE"] then
			targetsuffix "hubdp"
		end

	configuration { "mingw*" or "vs*" }
		targetextension ".exe"

	configuration { }

	links {
		"frontend",
		"utils",
		ext_lib("expat"),
		ext_lib("zlib"),
		"ocore_" .. _OPTIONS["osd"],
	}

	configuration { "macosx" }
		libdirs { MAME_DIR .. "3rdparty/discord_social_sdk/lib/release" }
		links { "discord_partner_sdk" }
		linkoptions { "-Wl,-rpath,@loader_path/3rdparty/discord_social_sdk/lib/release" }
	configuration { "linux" }
		libdirs { MAME_DIR .. "3rdparty/discord_social_sdk/lib/release" }
		links { "discord_partner_sdk" }
		linkoptions { "-Wl,-rpath,'$$ORIGIN/3rdparty/discord_social_sdk/lib/release'" }
	configuration { "x64", "vs*" }
		libdirs { MAME_DIR .. "3rdparty/discord_social_sdk/lib/release" }
		links { "discord_partner_sdk" }
	configuration { "arm64", "vs*" }
		libdirs { MAME_DIR .. "3rdparty/discord_social_sdk/lib/release/arm64" }
		links { "discord_partner_sdk" }
	configuration { }

	includedirs {
		MAME_DIR .. "3rdparty/catch/single_include",
		MAME_DIR .. "3rdparty/wga/peer/external/json/include",
		MAME_DIR .. "3rdparty/discord_social_sdk/include",
		MAME_DIR .. "src/frontend/mame",
		MAME_DIR .. "src/osd",
		MAME_DIR .. "src/emu",
		MAME_DIR .. "src/lib/util",
		ext_includedir("expat"),
		ext_includedir("zlib"),
	}

	files {
		MAME_DIR .. "src/emu/video/rgbsse.cpp",
		MAME_DIR .. "src/emu/video/rgbsse.h",
		MAME_DIR .. "src/emu/video/rgbvmx.cpp",
		MAME_DIR .. "src/emu/video/rgbvmx.h",
	}

	files {
		MAME_DIR .. "tests/main.cpp",
		MAME_DIR .. "tests/lib/util/corestr.cpp",
		MAME_DIR .. "tests/lib/util/options.cpp",
		MAME_DIR .. "tests/emu/attotime.cpp",
		MAME_DIR .. "tests/emu/video/rgbutil.cpp",
		MAME_DIR .. "tests/frontend/discord_lobby.cpp",
	}
