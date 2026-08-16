#pragma once

#include <string>

#include "OptionsHandler.hpp"

namespace Xenu
{
/// The directories a peeked core is told about, which are the ones a real run of
/// that core would be given. A core reads them during retro_init and can decide
/// what to publish from what it finds there.
struct PeekDirectories
{
    std::string system_directory;
    std::string save_directory;
    std::string core_assets_directory;
};

/// Read a core's option set without starting it.
///
/// Most cores register their options from inside retro_set_environment, which the
/// libretro API requires be callable before retro_init, so opening the library and
/// handing it an environment callback is usually enough to read the whole set: no
/// content, no emulation thread, no GPU, no audio. Cores that publish nothing that
/// early are then given a retro_init/retro_deinit pair, which is how RetroArch
/// reaches its own options menu.
///
/// Because retro_init runs real core code, the environment callback answers the
/// queries a core legitimately expects any frontend to answer by then -- the
/// directories, the log interface, the language. Those are out-parameters that
/// cores read without checking the return value, so refusing one hands the core an
/// untouched pointer rather than a "no".
///
/// Opens the core in place rather than through Core's temp-copy scheme: nothing
/// here lives long enough to collide with a running instance, and a copy per
/// peek would grow the temp directory every time a menu is opened.
///
/// `out` is filled with the definitions and with each option's declared default
/// as its value. Returns false if the core could not be opened or registered no
/// options at all.
bool PeekCoreOptions(const std::string& core_path, const PeekDirectories& directories, OptionsHandler& out);
}
