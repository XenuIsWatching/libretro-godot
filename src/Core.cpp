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
    // Seed once per thread. The emulation runs on a raw std::thread, so keep this
    // self-contained and cheap (no per-call random_device).
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<int> dis(0, 255);
    return static_cast<uint32_t>(dis(gen));
}

// Build a random hex string WITHOUT std::stringstream. On Linux the first stream
// op on the emulation thread lazily instantiates locale/codecvt facets (via
// std::call_once), which threw here and aborted the whole thread (uncaught) — a
// latent crash that only surfaced once cores actually load (the .so path fix).
// Manual hex has no locale dependency.
static std::string GenerateHex(const uint32_t len)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(static_cast<size_t>(len) * 2);
    for (uint32_t i = 0; i < len; i++)
    {
        const uint32_t rc = RandomChar() & 0xFFu;
        out.push_back(digits[(rc >> 4) & 0xF]);
        out.push_back(digits[rc & 0xF]);
    }
    return out;
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
    // error_code overload — the throwing overload would abort the emulation thread
    // (no try/catch around the raw std::thread) on any copy failure.
    std::error_code copy_ec;
    if (!std::filesystem::copy_file(m_path, temp_path, std::filesystem::copy_options::overwrite_existing, copy_ec) || copy_ec)
    {
        LogError("Failed to copy core file: " + m_path + " to " + temp_path.string() + " - " + copy_ec.message());
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

    // Cache need_fullpath before retro_init — it decides whether Wrapper hands the
    // core a byte buffer or just a path. Disc cores set it and open the image
    // themselves (VFS), so filling a multi-GB buffer for them is pure waste.
    {
        retro_system_info system_info = {};
        retro_get_system_info(&system_info);
        m_need_fullpath = system_info.need_fullpath;
        Log("Core need_fullpath: " + std::string(m_need_fullpath ? "true" : "false"));
    }

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

bool Core::GetNeedFullpath() const
{
    return m_need_fullpath;
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
