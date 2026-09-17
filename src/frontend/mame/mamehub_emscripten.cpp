// license:BSD-3-Clause
// Minimal mamehub overlay for Emscripten (WebRTC) builds.
// Restores the native latency/stats text boxes; chat stays desktop-only.

#include "emu.h"
#include "mamehub.h"

#include "NSM_CommonInterface.h"
#include "ui/ui.h"

namespace {
bool s_statsVisible = true;
}

mamehub_manager *mamehub_manager::m_manager = nullptr;

mamehub_manager::mamehub_manager() = default;

mamehub_manager::~mamehub_manager() = default;

void mamehub_manager::ui(mame_ui_manager &ui_manager, render_target &target)
{
	if (!s_statsVisible || !netCommon)
		return;

	ui_manager.draw_text_box(
		target,
		netCommon->getLatencyString().c_str(),
		ui::text_layout::text_justify::CENTER,
		0.9f,
		0.1f,
		rgb_t(255, 0, 0, 128));
	ui_manager.draw_text_box(
		target,
		netCommon->getStatisticsString().c_str(),
		ui::text_layout::text_justify::CENTER,
		0.1f,
		0.1f,
		rgb_t(255, 0, 0, 128));
}

bool mamehub_manager::handleChat(running_machine &, ui_event &event)
{
	if (event.ch == 'N' || event.ch == 'n')
	{
		s_statsVisible = !s_statsVisible;
		return true;
	}
	return false;
}

void mamehub_manager::reset()
{
	s_statsVisible = true;
}
