#pragma once

#include <string>

#include "OptionsHandler.hpp"

namespace Xenu
{
/// Read a core's option set without starting it.
///
/// Cores register their options from inside retro_set_environment, which the
/// libretro API requires be callable before retro_init. Opening the library,
/// handing it a capture-only environment callback and closing it again is
/// therefore enough to read the whole set — no retro_init, no content, no
/// emulation thread, no GPU, no audio.
///
/// Opens the core in place rather than through Core's temp-copy scheme: nothing
/// here lives long enough to collide with a running instance, and a copy per
/// peek would grow the temp directory every time a menu is opened.
///
/// `out` is filled with the definitions and with each option's declared default
/// as its value. Returns false if the core could not be opened or registered no
/// options at all.
bool PeekCoreOptions(const std::string& core_path, OptionsHandler& out);
}
