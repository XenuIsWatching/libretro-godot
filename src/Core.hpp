#pragma once

#include <libretro.h>

#include <string>

#include "Debug.hpp"
#include "DynLib.hpp"

namespace SK
{
class Wrapper;

class Core
{
public:
    Core(const std::string& path);
    ~Core() = default;

    bool Load();
    void Unload();

    const std::string& GetName() const;
    bool GetSupportsNoGame() const;

    decltype(&::retro_set_environment) retro_set_environment                       = nullptr;
    decltype(&::retro_set_video_refresh) retro_set_video_refresh                   = nullptr;
    decltype(&::retro_set_audio_sample) retro_set_audio_sample                     = nullptr;
    decltype(&::retro_set_audio_sample_batch) retro_set_audio_sample_batch         = nullptr;
    decltype(&::retro_set_input_poll) retro_set_input_poll                         = nullptr;
    decltype(&::retro_set_input_state) retro_set_input_state                       = nullptr;
    decltype(&::retro_init) retro_init                                             = nullptr;
    decltype(&::retro_deinit) retro_deinit                                         = nullptr;
    decltype(&::retro_api_version) retro_api_version                               = nullptr;
    decltype(&::retro_get_system_info) retro_get_system_info                       = nullptr;
    decltype(&::retro_get_system_av_info) retro_get_system_av_info                 = nullptr;
    decltype(&::retro_set_controller_port_device) retro_set_controller_port_device = nullptr;
    decltype(&::retro_reset) retro_reset                                           = nullptr;
    decltype(&::retro_run) retro_run                                               = nullptr;
    decltype(&::retro_serialize_size) retro_serialize_size                         = nullptr;
    decltype(&::retro_serialize) retro_serialize                                   = nullptr;
    decltype(&::retro_unserialize) retro_unserialize                               = nullptr;
    decltype(&::retro_cheat_reset) retro_cheat_reset                               = nullptr;
    decltype(&::retro_cheat_set) retro_cheat_set                                   = nullptr;
    decltype(&::retro_load_game) retro_load_game                                   = nullptr;
    decltype(&::retro_load_game_special) retro_load_game_special                   = nullptr;
    decltype(&::retro_unload_game) retro_unload_game                               = nullptr;
    decltype(&::retro_get_region) retro_get_region                                 = nullptr;
    decltype(&::retro_get_memory_data) retro_get_memory_data                       = nullptr;
    decltype(&::retro_get_memory_size) retro_get_memory_size                       = nullptr;

    bool SetSupportsNoGame(bool* supports);
    bool GetLibretroPath(const char** path) const;

private:
    std::string m_name;
    std::string m_path;
    void* m_handle = nullptr;
    bool m_supports_no_game = false;

    bool LoadHandle();

    template<typename T>
    bool LoadFunction_(T& function_ptr, const std::string& function_name)
    {
        function_ptr = reinterpret_cast<T>(DynLib_Sym(m_handle, function_name.c_str()));
        if (!function_ptr)
        {
            LogError("Failed to load function: " + function_name);
            return false;
        }
        return true;
    }
};
}
