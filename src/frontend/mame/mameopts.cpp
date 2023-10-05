// license:BSD-3-Clause
// copyright-holders:Aaron Giles
/***************************************************************************

    mameopts.cpp

    Options file and command line management.

***************************************************************************/

#include "emu.h"
#include "mameopts.h"

#include "clifront.h"

// emu
#include "drivenum.h"
#include "fileio.h"
#include "hashfile.h"
#include "main.h"
#include "screen.h"
#include "softlist_dev.h"

// lib/util
#include "path.h"
#include "zippath.h"

#include <cctype>
#include <stack>


//-------------------------------------------------
//  parse_standard_confs - parse the standard set
//  of CONF files
//-------------------------------------------------

void mame_options::parse_standard_confs(emu_options &options, std::ostream &error_stream, const game_driver *driver)
{
	// parse the CONF file defined by the platform (e.g., "mame.conf")
	// we do this twice so that the first file can change the CONF path
	parse_one_conf(options, emulator_info::get_configname(), OPTION_PRIORITY_MAME_CONF);
	parse_one_conf(options, emulator_info::get_configname(), OPTION_PRIORITY_MAME_CONF, &error_stream);

	// debug mode: parse "debug.conf" as well
	if (options.debug())
		parse_one_conf(options, "debug", OPTION_PRIORITY_DEBUG_CONF, &error_stream);

	// if we have a valid system driver, parse system-specific CONF files
	game_driver const *const cursystem = !driver ? system(options) : driver;
	if (!cursystem)
		return;

	if (&GAME_NAME(___empty) != cursystem) // hacky - this thing isn't a real system
	{
		// parse "vertical.conf" or "horizont.conf"
		if (cursystem->flags & ORIENTATION_SWAP_XY)
			parse_one_conf(options, "vertical", OPTION_PRIORITY_ORIENTATION_CONF, &error_stream);
		else
			parse_one_conf(options, "horizont", OPTION_PRIORITY_ORIENTATION_CONF, &error_stream);

		machine_config config(*cursystem, options);
		for (const screen_device &device : screen_device_enumerator(config.root_device()))
		{
			// parse "raster.conf" for raster games
			if (device.screen_type() == SCREEN_TYPE_RASTER)
			{
				parse_one_conf(options, "raster", OPTION_PRIORITY_SCREEN_CONF, &error_stream);
				break;
			}
			// parse "vector.conf" for vector games
			if (device.screen_type() == SCREEN_TYPE_VECTOR)
			{
				parse_one_conf(options, "vector", OPTION_PRIORITY_SCREEN_CONF, &error_stream);
				break;
			}
			// parse "lcd.conf" for lcd games
			if (device.screen_type() == SCREEN_TYPE_LCD)
			{
				parse_one_conf(options, "lcd", OPTION_PRIORITY_SCREEN_CONF, &error_stream);
				break;
			}
		}
	}

	// next parse "source/<sourcefile>.conf"
	std::string sourcename = std::string(core_filename_extract_base(cursystem->type.source(), true)).insert(0, "source" PATH_SEPARATOR);
	parse_one_conf(options, sourcename.c_str(), OPTION_PRIORITY_SOURCE_CONF, &error_stream);

	// then parse the grandparent, parent, and system-specific CONFs
	int parent = driver_list::clone(*cursystem);
	int gparent = (parent != -1) ? driver_list::clone(parent) : -1;
	if (gparent != -1)
		parse_one_conf(options, driver_list::driver(gparent).name, OPTION_PRIORITY_GPARENT_CONF, &error_stream);
	if (parent != -1)
		parse_one_conf(options, driver_list::driver(parent).name, OPTION_PRIORITY_PARENT_CONF, &error_stream);
	parse_one_conf(options, cursystem->name, OPTION_PRIORITY_DRIVER_CONF, &error_stream);
}


//-------------------------------------------------
//  system - return a pointer to the specified
//  system driver, or nullptr if no match
//-------------------------------------------------

const game_driver *mame_options::system(const emu_options &options)
{
	int index = driver_list::find(std::string(core_filename_extract_base(options.system_name(), true)).c_str());
	return (index != -1) ? &driver_list::driver(index) : nullptr;
}


//-------------------------------------------------
//  parse_one_conf - parse a single CONF file
//-------------------------------------------------

void mame_options::parse_one_conf(emu_options &options, const char *basename, int priority, std::ostream *error_stream)
{
	// don't parse if it has been disabled
	if (!options.read_config())
		return;

	// open the file; if we fail, that's ok
	emu_file file(options.conf_path(), OPEN_FLAG_READ);
	osd_printf_verbose("Attempting load of %s.conf\n", basename);
	std::error_condition const filerr = file.open(std::string(basename) + ".conf");
	if (filerr)
		return;

	// parse the file
	osd_printf_verbose("Parsing %s.conf\n", basename);
	try
	{
		options.parse_conf_file((util::core_file&)file, priority, priority < OPTION_PRIORITY_DRIVER_CONF, false);
	}
	catch (options_exception &ex)
	{
		if (error_stream)
			util::stream_format(*error_stream, "While parsing %s:\n%s\n", file.fullpath(), ex.message());
		return;
	}

}


//-------------------------------------------------
//  populate_hashpath_from_args_and_confs
//-------------------------------------------------

void mame_options::populate_hashpath_from_args_and_confs(emu_options &options, const std::vector<std::string> &args)
{
	// The existence of this function comes from the fact that for softlist options to be properly
	// evaluated, we need to have the hashpath variable set.  The problem is that the hashpath may
	// be set anywhere on the command line, but also in any of the myriad CONF files that we parse, some
	// of which may be system specific (e.g. - nes.conf) or otherwise influenced by the system (e.g. - vector.conf)
	//
	// I think that it is terrible that we have to do a completely independent pass on the command line and every
	// argument simply because any one of these things might be setting - hashpath.Unless we invest the effort in
	// building some sort of "late binding" apparatus for options(e.g. - delay evaluation of softlist options until
	// we've scoured all CONFs for hashpath) that can completely straddle the command line and the CONF worlds, doing
	// this is the best that we can do IMO.

	// parse the command line
	emu_options temp_options(emu_options::option_support::GENERAL_AND_SYSTEM);

	// pick up whatever changes the osd did to the default confpath
	temp_options.set_default_value(OPTION_CONFPATH, options.conf_path());

	try
	{
		temp_options.parse_command_line(args, OPTION_PRIORITY_CMDLINE, true);
	}
	catch (options_exception &)
	{
		// Something is very long; we have bigger problems than -hashpath possibly
		// being in never-never land.  Punt and let the main code fail
		return;
	}

	// if we have an auxillary verb, hashpath is irrelevant
	if (!temp_options.command().empty())
		return;

	// read CONF files
	if (temp_options.read_config())
	{
		std::ostringstream error_stream;
		parse_standard_confs(temp_options, error_stream);
	}

	// and fish out hashpath
	const auto entry = temp_options.get_entry(OPTION_HASHPATH);
	if (entry)
	{
		try
		{
			options.set_value(OPTION_HASHPATH, entry->value(), entry->priority());
		}
		catch (options_exception &)
		{
		}
	}
}
