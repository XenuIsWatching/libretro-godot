#include "Core.hpp"

#include "Wrapper.hpp"
#include "CallbackTrampolines.hpp"

#include <filesystem>
#include <sstream>
#include <mutex>
#include <random>
#include <cstring>

#include "Debug.hpp"
#include "EnvironmentHandler.hpp"
#include "VideoHandler.hpp"
#include "AudioHandler.hpp"
#include "InputHandler.hpp"

namespace Xenu
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
// std::call_once), which throws here and aborts the whole thread uncaught, since
// there is no try/catch around the raw std::thread. Manual hex has no locale
// dependency.
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
    if (!trampolines || !trampolines->IsValid())
    {
        LogError("Cannot load core without executable callback trampolines");
        return false;
    }

    if (!std::filesystem::is_regular_file(m_path))
    {
        LogError("Core not found: " + m_path);
        return false;
    }

    std::string name = std::filesystem::path(m_path).filename().replace_extension("").string();
    // Longest first: stripping "_libretro" off an Android name would leave "_android"
    // behind. Both are tried on every platform because the "_android" infix is a
    // convention, not a rule (azahar ships as "azahar_libretro.so" on Android), and
    // the bare name keys core_options/<name>.opt, which must match across platforms.
    static const char* const suffixes[] = { "_libretro_android", "_libretro" };
    for (const char* suffix : suffixes)
    {
        size_t pos = name.rfind(suffix);
        if (pos != std::string::npos)
        {
            name.erase(pos, std::strlen(suffix));
            break;
        }
    }
    m_name = name;

    std::string extension = std::filesystem::path(m_path).extension().string();
    const std::filesystem::path temp_dir = Wrapper::GetCurrentThreadWrapper()->GetTempDirectory();

    // Unload() removes this run's copy, but a process that dies with a core loaded
    // (a wedged core, a force-stop) leaves it behind for good — 246 MB a time for
    // Dolphin, which has filled a Quest to 100% and then failed the next load with
    // nothing but a silent refusal. Sweep the strays before adding another: a copy
    // still in use by a live instance survives, because Windows will not delete a
    // mapped DLL and unlinking a mapped .so leaves that mapping intact.
    //
    // Held from the sweep through to the load, because the "still in use" part is
    // only true once the library is actually MAPPED. Between copy_file and
    // LoadHandle the copy is an ordinary file on disk, and a second instance
    // starting in that window sweeps it away; the first instance then fails to
    // open a path that existed a moment ago. Nothing hit this until two cores
    // started in the same frame, which is exactly what cabling two handhelds
    // together does.
    static std::mutex s_temp_copy_mutex;
    std::unique_lock<std::mutex> temp_copy_lock(s_temp_copy_mutex);

    std::error_code sweep_ec;
    for (const auto& entry : std::filesystem::directory_iterator(temp_dir, sweep_ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != extension)
            continue;
        std::error_code rm_ec;
        std::filesystem::remove(entry.path(), rm_ec);
    }

    std::filesystem::path temp_path = temp_dir / (name + GenerateHex(10) + extension);
    // error_code overload: the throwing overload would abort the emulation thread
    // (no try/catch around the raw std::thread) on any copy failure.
    std::error_code copy_ec;
    if (!std::filesystem::copy_file(m_path, temp_path, std::filesystem::copy_options::overwrite_existing, copy_ec) || copy_ec)
    {
        LogError("Failed to copy core file: " + m_path + " to " + temp_path.string() + " - " + copy_ec.message());
        // copy_file is permitted to leave a partial destination on failure.
        std::error_code remove_ec;
        std::filesystem::remove(temp_path, remove_ec);
        return false;
    }

    m_temporary_path = temp_path.string();
    m_path = m_temporary_path;
    std::replace(m_path.begin(), m_path.end(), '\\', '/');
    m_temporary_path = m_path;

    if (!LoadHandle())
        return false;

    // Mapped now, so the next sweep will leave it alone.
    temp_copy_lock.unlock();

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

    // Cache need_fullpath before retro_init: it decides whether Wrapper hands the
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

    if (!m_temporary_path.empty())
    {
        std::error_code remove_ec;
        if (!std::filesystem::remove(m_temporary_path, remove_ec) && remove_ec)
            LogError("Failed to remove temporary core file: " + m_temporary_path + " - " + remove_ec.message());
        m_temporary_path.clear();
    }
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
