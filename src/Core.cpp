#include "Core.hpp"

#include "Wrapper.hpp"
#include "CallbackTrampolines.hpp"

#include <filesystem>
#include <sstream>
#include <random>

#include "Debug.hpp"
#include "EnvironmentHandler.hpp"
#include "VideoHandler.hpp"
#include "AudioHandler.hpp"
#include "InputHandler.hpp"

namespace SK
{
static uint32_t RandomChar()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    return dis(gen);
}

static std::string GenerateHex(const uint32_t len)
{
    std::stringstream ss;
    for (auto i = 0; i < len; i++)
    {
        const auto rc = RandomChar();
        std::stringstream hexstream;
        hexstream << std::hex << rc;
        auto hex = hexstream.str();
        ss << (hex.length() < 2 ? '0' + hex : hex);
    }
    return ss.str();
}

#define LoadFunction(funcPtr) \
    if (!LoadFunction_(funcPtr, #funcPtr)) \
        return false; \

Core::Core(const std::string& path)
: m_path(path)
{
}

bool Core::Load(CallbackTrampolines* trampolines)
{
    if (!std::filesystem::is_regular_file(m_path))
    {
        LogError("Core not found: " + m_path);
        return false;
    }

    std::string name = std::filesystem::path(m_path).filename().replace_extension("").string();
#ifdef __ANDROID__
    const std::string suffix = "_libretro_android";
#else
    const std::string suffix = "_libretro";
#endif
    size_t pos = name.rfind(suffix);
    if (pos != std::string::npos)
        name.erase(pos, suffix.length());
    m_name = name;

    std::string extension = std::filesystem::path(m_path).extension().string();
    std::filesystem::path temp_path = std::filesystem::path(Wrapper::GetCurrentThreadWrapper()->GetTempDirectory()) / (name + GenerateHex(10) + extension);
    if (!std::filesystem::copy_file(m_path, temp_path, std::filesystem::copy_options::overwrite_existing))
    {
        LogError("Failed to copy core file: " + m_path + " to " + temp_path.string());
        return false;
    }

    m_path = temp_path.string();
    std::replace(m_path.begin(), m_path.end(), '\\', '/');

    if (!LoadHandle())
        return false;

    LoadFunction(retro_set_environment);
    LoadFunction(retro_set_video_refresh);
    LoadFunction(retro_set_audio_sample);
    LoadFunction(retro_set_audio_sample_batch);
    LoadFunction(retro_set_input_poll);
    LoadFunction(retro_set_input_state);
    LoadFunction(retro_init);
    LoadFunction(retro_deinit);
    LoadFunction(retro_api_version);
    LoadFunction(retro_get_system_info);
    LoadFunction(retro_get_system_av_info);
    LoadFunction(retro_set_controller_port_device);
    LoadFunction(retro_reset);
    LoadFunction(retro_run);
    LoadFunction(retro_serialize_size);
    LoadFunction(retro_serialize);
    LoadFunction(retro_unserialize);
    LoadFunction(retro_cheat_reset);
    LoadFunction(retro_cheat_set);
    LoadFunction(retro_load_game);
    LoadFunction(retro_load_game_special);
    LoadFunction(retro_unload_game);
    LoadFunction(retro_get_region);
    LoadFunction(retro_get_memory_data);
    LoadFunction(retro_get_memory_size);

    retro_set_environment(trampolines->GetEnvironmentCallback());
    retro_set_video_refresh(trampolines->GetVideoRefreshCallback());
    retro_set_audio_sample(trampolines->GetAudioSampleCallback());
    retro_set_audio_sample_batch(trampolines->GetAudioSampleBatchCallback());
    retro_set_input_poll(trampolines->GetInputPollCallback());
    retro_set_input_state(trampolines->GetInputStateCallback());

    retro_init();

    return true;
}

void Core::Unload()
{
    if (m_handle)
    {
        DynLib_Close(m_handle);
        m_handle = nullptr;
    }

    if (std::filesystem::is_regular_file(m_path))
        if (!std::filesystem::remove(m_path))
            LogError("Core file not found for removal: " + m_path);
}

const std::string& Core::GetName() const
{
    return m_name;
}

bool Core::GetSupportsNoGame() const
{
    return m_supports_no_game;
}

bool Core::LoadHandle()
{
    m_handle = DynLib_Open(m_path.c_str());
    if (!m_handle)
    {
        LogError("Failed to load core handle: " + m_path);
        return false;
    }
    return true;
}

bool Core::SetSupportsNoGame(bool* supports)
{
    if (!supports)
        return true;

    m_supports_no_game = *supports;
    return true;
}

bool Core::GetLibretroPath(const char** path) const
{
    if (!path)
        return false;

    *path = m_path.c_str();
    return true;
}
}
