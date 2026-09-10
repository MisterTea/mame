// license:BSD-3-Clause

#ifndef MAME_FRONTEND_MAME_DISCORD_WAITING_ROOM_H
#define MAME_FRONTEND_MAME_DISCORD_WAITING_ROOM_H

#pragma once

#include "discord_directory_server.h"

#include <functional>

namespace mamehub {

void show_discord_waiting_room(discord_directory_server &directory, bool hosting, std::function<bool()> connection_finished);

} // namespace mamehub

#endif
