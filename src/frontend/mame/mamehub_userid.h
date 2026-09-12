// license:BSD-3-Clause

#ifndef MAME_FRONTEND_MAME_MAMEHUB_USERID_H
#define MAME_FRONTEND_MAME_MAMEHUB_USERID_H

#pragma once

#include <string>
#include <string_view>

namespace mamehub {

// Prefer an explicit -user_id.  When empty, derive a stable-ish random
// username by seeding a PRNG from the host primary MAC address (or a
// platform fallback when no MAC is available, e.g. Emscripten).
std::string resolve_user_id(std::string_view configured_user_id);

} // namespace mamehub

#endif
