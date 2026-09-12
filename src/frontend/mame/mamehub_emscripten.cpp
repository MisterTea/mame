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

void mamehub_manager::set_discord_directory(std::unique_ptr<mamehub::discord_directory_server> dir)
{
	m_discord_directory = std::move(dir);
}

mamehub::discord_directory_server *mamehub_manager::discord_directory() const
{
	return m_discord_directory.get();
}

void mamehub_manager::reset()
{
	m_discord_directory.reset();
}
