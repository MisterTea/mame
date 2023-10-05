// license:BSD-3-Clause
// copyright-holders:Aaron Giles
/***************************************************************************

    mameopts.h

    Options file and command line management.

***************************************************************************/

#ifndef MAME_FRONTEND_MAMEOPTS_H
#define MAME_FRONTEND_MAMEOPTS_H

#pragma once

#include "emuopts.h"

//**************************************************************************
//  CONSTANTS
//**************************************************************************
#undef OPTION_PRIORITY_CMDLINE

// option priorities
enum
{
	// command-line options are HIGH priority
	OPTION_PRIORITY_SUBCMD = OPTION_PRIORITY_HIGH,
	OPTION_PRIORITY_CMDLINE,

	// CONF-based options are NORMAL priority, in increasing order:
	OPTION_PRIORITY_MAME_CONF = OPTION_PRIORITY_NORMAL + 1,
	OPTION_PRIORITY_DEBUG_CONF,
	OPTION_PRIORITY_ORIENTATION_CONF,
	OPTION_PRIORITY_SCREEN_CONF,
	OPTION_PRIORITY_SOURCE_CONF,
	OPTION_PRIORITY_GPARENT_CONF,
	OPTION_PRIORITY_PARENT_CONF,
	OPTION_PRIORITY_DRIVER_CONF,
	OPTION_PRIORITY_CONF,
};

//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// forward references
class game_driver;
class software_part;

class mame_options
{
public:
	// parsing wrappers
	static void parse_standard_confs(emu_options &options, std::ostream &error_stream, const game_driver *driver = nullptr);
	static const game_driver *system(const emu_options &options);
	static void populate_hashpath_from_args_and_confs(emu_options &options, const std::vector<std::string> &args);

private:
	// CONF parsing helper
	static void parse_one_conf(emu_options &options, const char *basename, int priority, std::ostream *error_stream = nullptr);
};

#endif  // MAME_FRONTEND_MAMEOPTS_H
