// license:BSD-3-Clause

#ifndef MAME_FRONTEND_UI_LAUNCHBOX_H
#define MAME_FRONTEND_UI_LAUNCHBOX_H

#pragma once

#include <string>

class game_driver;
class mame_ui_manager;
struct ui_software_info;

namespace ui {
// Appends LaunchBox Games Database fields when launchbox.sqlite3 is available
// in the configured UI search path.
void append_launchbox_metadata(mame_ui_manager &mui, game_driver const &driver, std::string &text);
void append_launchbox_metadata(mame_ui_manager &mui, ui_software_info const &software, std::string &text);

} // namespace ui

#endif
