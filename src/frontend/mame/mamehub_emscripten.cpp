// license:BSD-3-Clause
// Minimal mamehub overlay for Emscripten offline builds.

#include "emu.h"
#include "mamehub.h"

mamehub_manager *mamehub_manager::m_manager = nullptr;

mamehub_manager::mamehub_manager() = default;

mamehub_manager::~mamehub_manager() = default;

void mamehub_manager::ui(mame_ui_manager &, render_target &)
{
}

bool mamehub_manager::handleChat(running_machine &, ui_event &)
{
	return false;
}

void mamehub_manager::reset()
{
}
