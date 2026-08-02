#include "CoreOptionsPeek.hpp"

#include <filesystem>

#include <libretro.h>

#include "Debug.hpp"
#include "DynLib.hpp"

namespace Xenu
{
namespace
{
// retro_environment_t is a plain C function pointer with no user-data argument,
// so the destination travels in a thread_local — the same pattern Wrapper uses
// for its own callbacks. A peek runs start to finish on the calling thread.
thread_local OptionsHandler* t_peek_target = nullptr;

bool PeekEnvironment(unsigned cmd, void* data)
{
    OptionsHandler* options = t_peek_target;
    if (!options)
        return false;

    switch (cmd)
    {
    // Must be answered: a core that gets no version back assumes 0 and falls
    // down to the legacy retro_variable format, which carries no categories, no
    // per-value labels and no declared defaults.
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        return options->GetCoreOptionsVersion(static_cast<uint32_t*>(data));

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        return options->SetCoreOptionsV2(static_cast<const retro_core_options_v2*>(data));
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
        return options->SetCoreOptionsV2Intl(static_cast<const retro_core_options_v2_intl*>(data));
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        return options->SetCoreOptions(static_cast<const retro_core_option_definition*>(data));
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        return options->SetCoreOptions(static_cast<const retro_core_options_intl*>(data));
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        return options->SetVariables(static_cast<const retro_variable*>(data));

    // Left unanswered on purpose. With no stored value the core keeps the default
    // it just declared, which is the value a peek is meant to report.
    case RETRO_ENVIRONMENT_GET_VARIABLE:
        return false;

    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        if (data)
            *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH;
        return true;

    default:
        return false;
    }
}
}

bool PeekCoreOptions(const std::string& core_path, OptionsHandler& out)
{
    if (!std::filesystem::is_regular_file(core_path))
    {
        LogError("PeekCoreOptions: core not found: " + core_path);
        return false;
    }

    void* handle = DynLib_Open(core_path.c_str());
    if (!handle)
    {
        LogError("PeekCoreOptions: failed to open core: " + core_path);
        return false;
    }

    auto set_environment = reinterpret_cast<decltype(&::retro_set_environment)>(
        DynLib_Sym(handle, "retro_set_environment"));
    if (!set_environment)
    {
        LogError("PeekCoreOptions: no retro_set_environment in " + core_path);
        DynLib_Close(handle);
        return false;
    }

    t_peek_target = &out;
    set_environment(PeekEnvironment);

    // Most cores register from retro_set_environment, but not all: fceumm returns
    // nothing until retro_init has run. RetroArch reaches its options menu the same
    // way (set_environment, then init), so this is a supported order rather than a
    // trick — it is just skipped when the first call already produced the set,
    // since retro_init is far more work than a peek should do by default.
    if (out.GetDefinitions().empty())
    {
        auto core_init = reinterpret_cast<decltype(&::retro_init)>(DynLib_Sym(handle, "retro_init"));
        auto core_deinit = reinterpret_cast<decltype(&::retro_deinit)>(DynLib_Sym(handle, "retro_deinit"));
        if (core_init && core_deinit)
        {
            core_init();
            core_deinit();
        }
    }

    t_peek_target = nullptr;

    DynLib_Close(handle);

    const size_t count = out.GetDefinitions().size();
    if (count == 0)
    {
        LogWarning("PeekCoreOptions: core registered no options: " + core_path);
        return false;
    }

    Log("PeekCoreOptions: " + std::to_string(count) + " definitions from " + core_path);
    return true;
}
}
