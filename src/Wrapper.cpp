#include "Wrapper.hpp"

#include "RetroAchievements.hpp"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_action.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>

#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include "Libretro.hpp"
#include "LinkCoordinator.hpp"
#include "Debug.hpp"
#include "ThreadCommandInitAudio.hpp"
#include "ThreadCommandReinitAudio.hpp"
#include "ThreadCommandCreateTexture.hpp"
#include "ThreadCommandUpdateTexture.hpp"
#include "ThreadCommandEmitSignal.hpp"

using namespace godot;

namespace Xenu
{
namespace
{
template<typename Callback>
class ScopeExit
{
public:
    explicit ScopeExit(Callback callback)
    : m_callback(std::move(callback))
    {
    }

    ~ScopeExit()
    {
        m_callback();
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

private:
    Callback m_callback;
};
}

thread_local Wrapper* t_current_wrapper = nullptr;

Wrapper* Wrapper::GetCurrentThreadWrapper()
{
    return t_current_wrapper;
}

void Wrapper::SetCurrentThreadWrapper(Wrapper* wrapper)
{
    t_current_wrapper = wrapper;
}

// Free-function form for callers that must not depend on Wrapper's full
// definition. LinkCoordinator uses this so the link bus can be built and
// tested without dragging in Godot.
Wrapper* CurrentThreadWrapper()
{
    return t_current_wrapper;
}

// Godot reports both Shift/Ctrl/Alt/Super keys under one keycode, so the whole
// event is needed: get_location() picks the left or right RETROK_* variant.
// Exposed to GDScript as Libretro::GodotKeyToRetroKey so there is only one copy
// of this table; retro_keyboard.gd decides *when* to send a key.
retro_key Wrapper::GodotKeyToRetroKey(const Ref<InputEventKey>& keyEvent)
{
    switch (keyEvent->get_keycode())
    {
        case KEY_NONE: return RETROK_UNKNOWN;
        case KEY_BACKSPACE: return RETROK_BACKSPACE;
        case KEY_TAB: return RETROK_TAB;
        case KEY_CLEAR: return RETROK_CLEAR;
        case KEY_ENTER: return RETROK_RETURN;
        case KEY_PAUSE: return RETROK_PAUSE;
        case KEY_ESCAPE: return RETROK_ESCAPE;
        case KEY_SPACE: return RETROK_SPACE;
        case KEY_EXCLAM: return RETROK_EXCLAIM;
        case KEY_QUOTEDBL: return RETROK_QUOTEDBL;
        case KEY_NUMBERSIGN: return RETROK_HASH;
        case KEY_DOLLAR: return RETROK_DOLLAR;
        case KEY_AMPERSAND: return RETROK_AMPERSAND;
        case KEY_APOSTROPHE: return RETROK_QUOTE;
        case KEY_PARENLEFT: return RETROK_LEFTPAREN;
        case KEY_PARENRIGHT: return RETROK_RIGHTPAREN;
        case KEY_ASTERISK: return RETROK_ASTERISK;
        case KEY_PLUS: return RETROK_PLUS;
        case KEY_COMMA: return RETROK_COMMA;
        case KEY_MINUS: return RETROK_MINUS;
        case KEY_PERIOD: return RETROK_PERIOD;
        case KEY_SLASH: return RETROK_SLASH;
        case KEY_0: return RETROK_0;
        case KEY_1: return RETROK_1;
        case KEY_2: return RETROK_2;
        case KEY_3: return RETROK_3;
        case KEY_4: return RETROK_4;
        case KEY_5: return RETROK_5;
        case KEY_6: return RETROK_6;
        case KEY_7: return RETROK_7;
        case KEY_8: return RETROK_8;
        case KEY_9: return RETROK_9;
        case KEY_COLON: return RETROK_COLON;
        case KEY_SEMICOLON: return RETROK_SEMICOLON;
        case KEY_LESS: return RETROK_LESS;
        case KEY_EQUAL: return RETROK_EQUALS;
        case KEY_GREATER: return RETROK_GREATER;
        case KEY_QUESTION: return RETROK_QUESTION;
        case KEY_AT: return RETROK_AT;
        case KEY_BRACKETLEFT: return RETROK_LEFTBRACKET;
        case KEY_BACKSLASH: return RETROK_BACKSLASH;
        case KEY_BRACKETRIGHT: return RETROK_RIGHTBRACKET;
        case KEY_ASCIICIRCUM: return RETROK_CARET;
        case KEY_UNDERSCORE: return RETROK_UNDERSCORE;
        case KEY_QUOTELEFT: return RETROK_BACKQUOTE;
        case KEY_A: return RETROK_a;
        case KEY_B: return RETROK_b;
        case KEY_C: return RETROK_c;
        case KEY_D: return RETROK_d;
        case KEY_E: return RETROK_e;
        case KEY_F: return RETROK_f;
        case KEY_G: return RETROK_g;
        case KEY_H: return RETROK_h;
        case KEY_I: return RETROK_i;
        case KEY_J: return RETROK_j;
        case KEY_K: return RETROK_k;
        case KEY_L: return RETROK_l;
        case KEY_M: return RETROK_m;
        case KEY_N: return RETROK_n;
        case KEY_O: return RETROK_o;
        case KEY_P: return RETROK_p;
        case KEY_Q: return RETROK_q;
        case KEY_R: return RETROK_r;
        case KEY_S: return RETROK_s;
        case KEY_T: return RETROK_t;
        case KEY_U: return RETROK_u;
        case KEY_V: return RETROK_v;
        case KEY_W: return RETROK_w;
        case KEY_X: return RETROK_x;
        case KEY_Y: return RETROK_y;
        case KEY_Z: return RETROK_z;
        case KEY_BRACELEFT: return RETROK_LEFTBRACE;
        case KEY_BAR: return RETROK_BAR;
        case KEY_BRACERIGHT: return RETROK_RIGHTBRACE;
        case KEY_ASCIITILDE: return RETROK_TILDE;
        case KEY_DELETE: return RETROK_DELETE;
        case KEY_KP_0: return RETROK_KP0;
        case KEY_KP_1: return RETROK_KP1;
        case KEY_KP_2: return RETROK_KP2;
        case KEY_KP_3: return RETROK_KP3;
        case KEY_KP_4: return RETROK_KP4;
        case KEY_KP_5: return RETROK_KP5;
        case KEY_KP_6: return RETROK_KP6;
        case KEY_KP_7: return RETROK_KP7;
        case KEY_KP_8: return RETROK_KP8;
        case KEY_KP_9: return RETROK_KP9;
        case KEY_KP_PERIOD: return RETROK_KP_PERIOD;
        case KEY_KP_DIVIDE: return RETROK_KP_DIVIDE;
        case KEY_KP_MULTIPLY: return RETROK_KP_MULTIPLY;
        case KEY_KP_SUBTRACT: return RETROK_KP_MINUS;
        case KEY_KP_ADD: return RETROK_KP_PLUS;
        case KEY_KP_ENTER: return RETROK_KP_ENTER;
        // case KEY_KP_EQUALS: return RETROK_KP_EQUALS;
        case KEY_UP: return RETROK_UP;
        case KEY_DOWN: return RETROK_DOWN;
        case KEY_RIGHT: return RETROK_RIGHT;
        case KEY_LEFT: return RETROK_LEFT;
        case KEY_INSERT: return RETROK_INSERT;
        case KEY_HOME: return RETROK_HOME;
        case KEY_END: return RETROK_END;
        case KEY_PAGEUP: return RETROK_PAGEUP;
        case KEY_PAGEDOWN: return RETROK_PAGEDOWN;
        case KEY_F1: return RETROK_F1;
        case KEY_F2: return RETROK_F2;
        case KEY_F3: return RETROK_F3;
        case KEY_F4: return RETROK_F4;
        case KEY_F5: return RETROK_F5;
        case KEY_F6: return RETROK_F6;
        case KEY_F7: return RETROK_F7;
        case KEY_F8: return RETROK_F8;
        case KEY_F9: return RETROK_F9;
        case KEY_F10: return RETROK_F10;
        case KEY_F11: return RETROK_F11;
        case KEY_F12: return RETROK_F12;
        case KEY_F13: return RETROK_F13;
        case KEY_F14: return RETROK_F14;
        case KEY_F15: return RETROK_F15;
        case KEY_NUMLOCK: return RETROK_NUMLOCK;
        case KEY_CAPSLOCK: return RETROK_CAPSLOCK;
        case KEY_SCROLLLOCK: return RETROK_SCROLLOCK;
        case KEY_SHIFT:
        {
            if (keyEvent->get_location() == KeyLocation::KEY_LOCATION_LEFT)
                return RETROK_LSHIFT;
            if (keyEvent->get_location() == KeyLocation::KEY_LOCATION_RIGHT)
                return RETROK_RSHIFT;
        }
        break;
        case KEY_CTRL:
        {
            if (keyEvent->get_location() == KeyLocation::KEY_LOCATION_LEFT)
                return RETROK_LCTRL;
            if (keyEvent->get_location() == KeyLocation::KEY_LOCATION_RIGHT)
                return RETROK_RCTRL;
        }
        break;
        case KEY_ALT:
        {
            if (keyEvent->get_location() == KeyLocation::KEY_LOCATION_LEFT)
                return RETROK_LALT;
            if (keyEvent->get_location() == KeyLocation::KEY_LOCATION_RIGHT)
                return RETROK_RALT;
        }
        break;
        case KEY_META:
        {
            // NOTE: may need to return RETROK_LSUPER/RETK_RSUPER instead for some platforms
            if (keyEvent->get_location() == KeyLocation::KEY_LOCATION_LEFT)
                return RETROK_LMETA;
            if (keyEvent->get_location() == KeyLocation::KEY_LOCATION_RIGHT)
                return RETROK_RMETA;
        }
        break;
        // case KEY_MODE: return RETROK_MODE;
        // case KEY_COMPOSE: return RETROK_COMPOSE;
        case KEY_HELP: return RETROK_HELP;
        case KEY_PRINT: return RETROK_PRINT;
        case KEY_SYSREQ: return RETROK_SYSREQ;
        // case KEY_BREAK: return RETROK_BREAK;
        case KEY_MENU: return RETROK_MENU;
        // case KEY_POWER: return RETROK_POWER;
        // case KEY_EURO: return RETROK_EURO;
        // case KEY_UNDO: return RETROK_UNDO;
        // case KEY_OEM_102: return RETROK_OEM_102;
        case KEY_BACK: return RETROK_BROWSER_BACK;
        case KEY_FORWARD: return RETROK_BROWSER_FORWARD;
        case KEY_REFRESH: return RETROK_BROWSER_REFRESH;
        case KEY_STOP: return RETROK_BROWSER_STOP;
        case KEY_SEARCH: return RETROK_BROWSER_SEARCH;
        case KEY_FAVORITES: return RETROK_BROWSER_FAVORITES;
        case KEY_HOMEPAGE: return RETROK_BROWSER_HOME;
        case KEY_VOLUMEMUTE: return RETROK_VOLUME_MUTE;
        case KEY_VOLUMEDOWN: return RETROK_VOLUME_DOWN;
        case KEY_VOLUMEUP: return RETROK_VOLUME_UP;
        case KEY_MEDIANEXT: return RETROK_MEDIA_NEXT;
        case KEY_MEDIAPREVIOUS: return RETROK_MEDIA_PREV;
        case KEY_MEDIASTOP: return RETROK_MEDIA_STOP;
        case KEY_MEDIAPLAY: return RETROK_MEDIA_PLAY_PAUSE;
        case KEY_LAUNCHMAIL: return RETROK_LAUNCH_MAIL;
        case KEY_LAUNCHMEDIA: return RETROK_LAUNCH_MEDIA;
        // case KEY_LAUNCH_APP1: return RETROK_LAUNCH_APP1;
        // case KEY_LAUNCH_APP2: return RETROK_LAUNCH_APP2;

        // Unhandled godot keys
        case KEY_SPECIAL: return RETROK_UNKNOWN;
        case KEY_BACKTAB: return RETROK_UNKNOWN;
        case KEY_F16: return RETROK_UNKNOWN;
        case KEY_F17: return RETROK_UNKNOWN;
        case KEY_F18: return RETROK_UNKNOWN;
        case KEY_F19: return RETROK_UNKNOWN;
        case KEY_F20: return RETROK_UNKNOWN;
        case KEY_F21: return RETROK_UNKNOWN;
        case KEY_F22: return RETROK_UNKNOWN;
        case KEY_F23: return RETROK_UNKNOWN;
        case KEY_F24: return RETROK_UNKNOWN;
        case KEY_F25: return RETROK_UNKNOWN;
        case KEY_F26: return RETROK_UNKNOWN;
        case KEY_F27: return RETROK_UNKNOWN;
        case KEY_F28: return RETROK_UNKNOWN;
        case KEY_F29: return RETROK_UNKNOWN;
        case KEY_F30: return RETROK_UNKNOWN;
        case KEY_F31: return RETROK_UNKNOWN;
        case KEY_F32: return RETROK_UNKNOWN;
        case KEY_F33: return RETROK_UNKNOWN;
        case KEY_F34: return RETROK_UNKNOWN;
        case KEY_F35: return RETROK_UNKNOWN;
        case KEY_HYPER: return RETROK_UNKNOWN;
        case KEY_MEDIARECORD: return RETROK_UNKNOWN;
        case KEY_STANDBY: return RETROK_UNKNOWN;
        case KEY_OPENURL: return RETROK_UNKNOWN;
        case KEY_LAUNCH0: return RETROK_UNKNOWN;
        case KEY_LAUNCH1: return RETROK_UNKNOWN;
        case KEY_LAUNCH2: return RETROK_UNKNOWN;
        case KEY_LAUNCH3: return RETROK_UNKNOWN;
        case KEY_LAUNCH4: return RETROK_UNKNOWN;
        case KEY_LAUNCH5: return RETROK_UNKNOWN;
        case KEY_LAUNCH6: return RETROK_UNKNOWN;
        case KEY_LAUNCH7: return RETROK_UNKNOWN;
        case KEY_LAUNCH8: return RETROK_UNKNOWN;
        case KEY_LAUNCH9: return RETROK_UNKNOWN;
        case KEY_LAUNCHA: return RETROK_UNKNOWN;
        case KEY_LAUNCHB: return RETROK_UNKNOWN;
        case KEY_LAUNCHC: return RETROK_UNKNOWN;
        case KEY_LAUNCHD: return RETROK_UNKNOWN;
        case KEY_LAUNCHE: return RETROK_UNKNOWN;
        case KEY_LAUNCHF: return RETROK_UNKNOWN;
        case KEY_GLOBE: return RETROK_UNKNOWN;
        case KEY_KEYBOARD: return RETROK_UNKNOWN;
        case KEY_JIS_EISU: return RETROK_UNKNOWN;
        case KEY_JIS_KANA: return RETROK_UNKNOWN;
        case KEY_UNKNOWN: return RETROK_UNKNOWN;
        case KEY_PERCENT: return RETROK_UNKNOWN;
        case KEY_YEN: return RETROK_UNKNOWN;
        case KEY_SECTION: return RETROK_UNKNOWN;
        default:
            LogError("Unmapped key code from Godot to Libretro: " + std::to_string(keyEvent->get_keycode()));
            break;
    };

    return RETROK_UNKNOWN;
}

static int16_t ToShort(float floatValue, int mul = 1)
{
    return static_cast<int16_t>(Math::clamp(Math::round(floatValue), static_cast<float>(INT16_MIN), static_cast<float>(INT16_MAX)) * mul);
}

// Locate a core inside <root>/cores. Android cores are conventionally named
// "<name>_libretro_android.so" but not universally (azahar ships as plain
// "azahar_libretro.so"), so try every convention the platform can use and take
// the first that exists. With none present, return the canonical name so the
// error names that file.
std::string Wrapper::ResolveCorePath(const std::string& root_directory, const std::string& core_name)
{
    const std::filesystem::path cores_dir = std::filesystem::path(root_directory).append("cores");

    static const char* const suffixes[] = {
#ifdef __ANDROID__
        "_libretro_android.so", "_libretro.so",
#elif defined(__linux__)
        "_libretro.so",
#elif defined(__APPLE__)
        "_libretro.dylib",
#else
        "_libretro.dll",
#endif
    };

    for (const char* suffix : suffixes)
    {
        std::filesystem::path candidate = cores_dir / (core_name + suffix);
        if (std::filesystem::is_regular_file(candidate))
            return candidate.string();
    }
    return (cores_dir / (core_name + suffixes[0])).string();
}

void Wrapper::StartContent(const std::string& root_directory, const std::string& core_name, const std::string& game_path)
{
    StartSubsystemContent(root_directory, core_name, game_path, std::string(), {});
}

void Wrapper::StartSubsystemContent(const std::string& root_directory, const std::string& core_name,
                                    const std::string& game_path, const std::string& subsystem_ident,
                                    const std::vector<std::string>& subsystem_paths)
{
    // Nothing is told where to render. The core draws into its own texture and a
    // display samples it, so a machine with nowhere to show its picture is not a
    // case this has to know about.

    StopEmulationThread();

    // A cartridge run owns an in-memory ROM image. Do not retain or reuse it
    // when this Wrapper is restarted with disc/full-path content.
    std::vector<uint8_t>().swap(m_game_buffer);

    // Same reasoning for a subsystem set: a restart must not inherit the last
    // run's ROM images.
    std::vector<std::vector<unsigned char>>().swap(m_subsystem_buffers);

    auto audio_stream_player = memnew(AudioStreamPlayer3D);
    audio_stream_player->set_name("AudioStreamPlayer3D");
    audio_stream_player->set_attenuation_model(AudioStreamPlayer3D::ATTENUATION_INVERSE_DISTANCE);
    audio_stream_player->set_panning_strength(1.0f);
    audio_stream_player->set_max_db(0.0f);
    Libretro* owner = LiveLibretroNode();
    if (!owner)
    {
        memdelete(audio_stream_player);
        LogError("StartContent: the Libretro node is gone");
        return;
    }
    owner->add_child(audio_stream_player);

    std::filesystem::path core_path = ResolveCorePath(root_directory, core_name);

    m_core = std::make_unique<Core>(core_path.string());
    m_trampolines = std::make_unique<CallbackTrampolines>(this);
    m_environment_handler = std::make_unique<EnvironmentHandler>();
    m_video_handler = std::make_unique<VideoHandler>();
    m_audio_handler = std::make_unique<AudioHandler>();
    m_audio_handler->SetAudioStreamPlayer(audio_stream_player);
    m_input_handler = std::make_unique<InputHandler>();
    m_options_handler = std::make_unique<OptionsHandler>();
    m_message_handler = std::make_unique<MessageHandler>();
    m_log_handler = std::make_unique<LogHandler>();

    m_video_handler->Init();

    m_root_directory = root_directory;
    m_temp_directory = std::filesystem::path(root_directory).append("temp").string();
    m_game_path = game_path;
    // Set before the emulation thread is constructed, like everything else here:
    // the std::thread constructor is the synchronisation edge, and these are read
    // only on that thread.
    m_subsystem_ident = subsystem_ident;
    m_subsystem_paths = subsystem_paths;
    // Per-content, and a restart reuses this Wrapper, so the descriptors belong to
    // the core instance that is about to be torn down and re-created.
    m_memory_descriptors.clear();
    m_memory_addrspaces.clear();
    m_supports_achievements = true;
    std::string system_directory = std::filesystem::path(root_directory).append("system").append(core_name).string();
    std::string save_directory = std::filesystem::path(root_directory).append("save").append(core_name).string();
    std::string core_assets_directory = std::filesystem::path(root_directory).append("core_assets").append(core_name).string();
    m_environment_handler->SetDirectories(system_directory, save_directory, core_assets_directory);

    if (!std::filesystem::is_directory(m_temp_directory))
    {
        std::error_code ec;
        std::filesystem::create_directories(m_temp_directory, ec);
        if (ec)
        {
            LogError("Failed to create temp directory: " + m_temp_directory + " - " + ec.message());
            return;
        }
    }

    ClearCoreIdentity();

    m_stop_requested = false;
    m_thread_exited = false;
    // Before the thread exists, so a request made on the very next line of the
    // caller's frame is queued for it rather than refused.
    m_starting = true;
    m_thread = std::thread(&Wrapper::EmulationThreadLoop, this);
}

void Wrapper::StopContent()
{
    // Silence output immediately (main-thread entry point, so touching the
    // AudioStreamPlayer3D is safe here): a core whose teardown hangs can keep
    // firing audio callbacks from its internal threads long after the stop, and
    // the player must not keep voicing them.
    if (m_audio_handler)
        m_audio_handler->SetPlaying(false);
    // Non-blocking: the join + teardown (SRAM flush, retro_unload_game,
    // retro_deinit, DLL unload) all happen off the main thread / deferred to
    // _process, so powering a system off does not hitch the frame.
    StopEmulationThread(false);
}

bool Wrapper::ShutdownForExit(uint32_t budget_ms)
{
    if (m_audio_handler)
        m_audio_handler->SilenceForTeardown();
    return StopEmulationThreadBounded(budget_ms);
}

bool Wrapper::StopEmulationThreadBounded(uint32_t budget_ms)
{
    if (!m_core)
        return true;

    m_stop_requested = true;
    m_running = false;
    m_condition_variable.notify_all();
    m_np_cv.notify_all();
    m_stopping = true;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budget_ms);
    while (!m_thread_exited.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!m_thread_exited.load(std::memory_order_acquire))
        return false;

    if (m_thread.joinable())
        m_thread.join();
    FinishTeardown();
    return true;
}

void Wrapper::AbandonThread()
{
    if (m_audio_handler)
        m_audio_handler->SilenceForTeardown();
    // Deliberately no FinishTeardown: the handlers and the core are still in use
    // by the thread we are walking away from.
    if (m_thread.joinable())
        m_thread.detach();
}

Libretro* Wrapper::LiveLibretroNode() const
{
    if (m_libretro_node_id == 0)
        return nullptr;
    return Object::cast_to<Libretro>(ObjectDB::get_instance(m_libretro_node_id));
}

void Wrapper::NotifyContentLoadFailed(const char* reason) const
{
    if (Libretro* node = LiveLibretroNode())
        node->NotifyContentLoadFailed(godot::String(reason));
}

namespace
{
// Read on the emulation thread as the content loads, written from GDScript
// before StartContent. Defaults to the libretro convention.
std::atomic<bool> g_no_content_passes_null{ true };
}

void Wrapper::SetNoContentPassesNull(bool passes_null)
{
    g_no_content_passes_null.store(passes_null, std::memory_order_relaxed);
}

bool Wrapper::GetNoContentPassesNull()
{
    return g_no_content_passes_null.load(std::memory_order_relaxed);
}

Ref<ImageTexture> Wrapper::GetVideoTexture() const
{
    return m_video_handler ? m_video_handler->GetTexture() : Ref<ImageTexture>();
}

Ref<Image> Wrapper::GetVideoImage() const
{
    return m_video_handler ? m_video_handler->GetImage() : Ref<Image>();
}

void Wrapper::SetAudioPlaying(bool playing)
{
    if (m_audio_handler)
        m_audio_handler->SetPlaying(playing);
}


godot::Array Wrapper::GetControllerInfo() const
{
    Array result;
    if (!m_input_handler)
        return result;

    const auto controllers = m_input_handler->GetControllers();
    for (uint32_t port = 0; port < controllers.size(); ++port)
    {
        Array port_controllers;
        for (const auto& ctrl : controllers[port])
        {
            Dictionary entry;
            entry["name"] = String(ctrl.name.c_str());
            entry["id"]   = static_cast<int>(ctrl.id);
            port_controllers.append(entry);
        }
        // Also record which device is currently selected for this port
        Dictionary port_entry;
        port_entry["port"]        = static_cast<int>(port);
        port_entry["controllers"] = port_controllers;
        port_entry["current_id"]  = static_cast<int>(m_input_handler->GetPortDevice(port));
        result.append(port_entry);
    }
    return result;
}

void Wrapper::SetControllerPortDevice(uint32_t port, uint32_t device)
{
    // Always remember the selection: a controller/mouse plugged in while the
    // system is OFF has no core yet; the emulation thread applies this map
    // right after retro_load_game so the core polls the right device.
    {
        std::lock_guard<std::mutex> lock(m_port_device_mutex);
        m_pending_port_devices[port] = device;
    }
    if (!m_core || !m_input_handler)
    {
        Log("SetControllerPortDevice: no core running, port=" + std::to_string(port)
            + " device=" + std::to_string(device) + " recorded for next start");
        return;
    }
    // Live change: route through the emulation thread so the core call lands
    // strictly between retro_run() frames (never mid-frame from this thread).
    m_emu_thread_commands_queue.enqueue(
        std::make_unique<EmuThreadCommandSetPortDevice>(port, device));
}

void Wrapper::SetLightgunPosition(uint32_t port, int16_t x, int16_t y)
{
    if (!m_input_handler)
        return;
    if (IsNetplayPortManaged(port))
    {
        if (m_np_rollback.load(std::memory_order_acquire) &&
            (m_np_local_mask.load(std::memory_order_relaxed) & (1u << port)))
        {
            std::lock_guard<std::mutex> lock(m_np_mutex);
            m_np_live_local[port * 5 + 1] = x;
            m_np_live_local[port * 5 + 2] = y;
        }
        return;
    }
    m_input_handler->SetLightgunPosition(port, x, y);
}

void Wrapper::SetLightgunIsOffscreen(uint32_t port, bool offscreen)
{
    if (!m_input_handler)
        return;
    if (IsNetplayPortManaged(port))
    {
        if (m_np_rollback.load(std::memory_order_acquire) &&
            (m_np_local_mask.load(std::memory_order_relaxed) & (1u << port)))
        {
            std::lock_guard<std::mutex> lock(m_np_mutex);
            m_np_live_local[port * 5 + 3] = offscreen ? 1 : 0;
        }
        return;
    }
    m_input_handler->SetLightgunIsOffscreen(port, offscreen ? 1 : 0);
}

void Wrapper::SetLightgunButton(uint32_t port, int button_id, bool pressed)
{
    if (!m_input_handler)
        return;
    uint32_t buttons = 0;
    if (IsNetplayPortManaged(port))
    {
        if (!(m_np_rollback.load(std::memory_order_acquire) &&
              (m_np_local_mask.load(std::memory_order_relaxed) & (1u << port))))
            return;
        std::lock_guard<std::mutex> lock(m_np_mutex);
        buttons = static_cast<uint32_t>(m_np_live_local[port * 5]);
        if (pressed)
            buttons |= (1u << button_id);
        else
            buttons &= ~(1u << button_id);
        m_np_live_local[port * 5] = static_cast<int32_t>(buttons);
        return;
    }
    buttons = m_input_handler->GetLightgunButtons(port);
    if (pressed)
        buttons |= (1u << button_id);
    else
        buttons &= ~(1u << button_id);
    m_input_handler->SetLightgunButtons(port, buttons);
}

uint32_t Wrapper::GetAudioBufferOccupancy() const
{
    return m_audio_handler ? m_audio_handler->BufferOccupancy() : 0;
}

double Wrapper::GetAudioBrakeMs() const
{
    return m_audio_handler ? m_audio_handler->LastBrakeMs() : 0.0;
}

bool Wrapper::IsNetplayPortManaged(uint32_t port) const
{
    return port < 4 &&
        m_netplay_enabled.load(std::memory_order_acquire) &&
        (m_np_port_mask.load(std::memory_order_relaxed) & (1u << port));
}

void Wrapper::SetJoypadState(uint32_t port, uint16_t button_mask, int16_t analog_lx, int16_t analog_ly, int16_t analog_rx, int16_t analog_ry)
{
    if (!m_input_handler)
        return;
    // In netplay mode the emulation thread applies the agreed per-frame inputs
    // for the masked ports; live main-thread writes would desync peers.
    if (IsNetplayPortManaged(port))
    {
        // Rollback: locally-owned masked ports feed the live-input slot that
        // the emulation thread samples each frame (zero added delay). Remote
        // masked ports stay blocked (prediction + confirmations own them).
        if (m_np_rollback.load(std::memory_order_acquire) &&
            (m_np_local_mask.load(std::memory_order_relaxed) & (1u << port)))
        {
            std::lock_guard<std::mutex> lock(m_np_mutex);
            int32_t* v = m_np_live_local.data() + port * 5;
            v[0] = button_mask;
            v[1] = analog_lx; v[2] = analog_ly;
            v[3] = analog_rx; v[4] = analog_ry;
        }
        return;
    }
    m_input_handler->SetJoypadButtonStates(port, button_mask);
    m_input_handler->SetAnalogLeft(port, analog_lx, analog_ly);
    m_input_handler->SetAnalogRight(port, analog_rx, analog_ry);
}

godot::PackedInt32Array Wrapper::PeekJoypadState(uint32_t port) const
{
    godot::PackedInt32Array out;
    out.resize(5);
    if (!m_input_handler)
        return out;

    const InputHandler::NetplayState state = m_input_handler->CaptureNetplayState();
    const auto value_or_zero = [port](const auto& values) -> int32_t
    {
        const auto it = values.find(port);
        return it == values.end() ? 0 : static_cast<int32_t>(it->second);
    };
    out[0] = value_or_zero(state.joypad_buttons);
    out[1] = value_or_zero(state.analog_left_x);
    out[2] = value_or_zero(state.analog_left_y);
    out[3] = value_or_zero(state.analog_right_x);
    out[4] = value_or_zero(state.analog_right_y);
    return out;
}

void Wrapper::SetMouseState(uint32_t port, int32_t dx, int32_t dy, uint32_t buttons)
{
    if (!m_input_handler)
        return;
    auto clamp16 = [](int64_t v) -> int32_t
    {
        return static_cast<int32_t>(v < -0x7fff ? -0x7fff : (v > 0x7fff ? 0x7fff : v));
    };
    if (IsNetplayPortManaged(port))
    {
        // Match InputHandler's relative-delta semantics: collect every main-
        // thread update until the emulation thread samples this local frame.
        if (m_np_rollback.load(std::memory_order_acquire) &&
            (m_np_local_mask.load(std::memory_order_relaxed) & (1u << port)))
        {
            std::lock_guard<std::mutex> lock(m_np_mutex);
            int32_t* v = m_np_live_local.data() + port * 5;
            v[0] = static_cast<int32_t>(buttons);
            v[1] = clamp16(static_cast<int64_t>(v[1]) + dx);
            v[2] = clamp16(static_cast<int64_t>(v[2]) + dy);
        }
        return;
    }
    m_input_handler->SetMousePosition(port, static_cast<int16_t>(clamp16(dx)), static_cast<int16_t>(clamp16(dy)));
    m_input_handler->SetMouseButtons(port, buttons);
}

void Wrapper::SetKeyState(uint32_t port, uint32_t keycode, bool down, uint32_t character)
{
    if (!m_input_handler)
        return;
    // During netplay, keyboard input rides the deterministic frame schedule
    // (key-event slots); live writes would desync peers. Keyboard state is
    // global (port 0), so gate on port 0 being masked.
    if (m_netplay_enabled.load(std::memory_order_acquire) &&
        (m_np_port_mask.load(std::memory_order_relaxed) & 1u))
        return;
    if (!m_running.load(std::memory_order_acquire)
        || m_stopping.load(std::memory_order_acquire))
        return;
    m_emu_thread_commands_queue.enqueue(
        std::make_unique<EmuThreadCommandKeyboardEvent>(port, keycode, down, character));
}

void Wrapper::SetSensorAccel(uint32_t port, float x, float y, float z, uint32_t index)
{
    if (m_input_handler && !IsNetplayPortManaged(port))
        m_input_handler->SetSensorAccel(port, x, y, z, index);
}

void Wrapper::SetSensorGyro(uint32_t port, float x, float y, float z, uint32_t index)
{
    if (m_input_handler && !IsNetplayPortManaged(port))
        m_input_handler->SetSensorGyro(port, x, y, z, index);
}

void Wrapper::SetPointerState(uint32_t port, int16_t x, int16_t y, bool pressed)
{
    if (m_input_handler && !IsNetplayPortManaged(port))
    {
        m_input_handler->SetPointerPosition(port, x, y);
        m_input_handler->SetPointerPressed(port, pressed ? 1 : 0);
        // A single-point caller owns the whole device: anything a multi-point
        // caller left on the other indices would otherwise keep being reported.
        m_input_handler->ClearPointersFrom(port, 1);
    }
}

void Wrapper::SetPointerIndexState(uint32_t port, uint32_t index, int16_t x, int16_t y, bool pressed)
{
    if (m_input_handler && !IsNetplayPortManaged(port))
        m_input_handler->SetPointerIndexState(port, index, x, y, pressed);
}

// ── Netplay (deterministic lockstep) ─────────────────────────────────────────

void Wrapper::SetNetplayMode(bool enabled, uint32_t port_mask, int64_t start_frame)
{
    {
        std::lock_guard<std::mutex> lock(m_np_mutex);
        m_np_inputs.clear();
        m_disc_schedule.clear();
        m_reset_schedule.clear();
        m_np_local_mask_schedule.clear();
        // Link operations retain Wrapper pointers until they land.  They must
        // not survive a stopped session or reach a later core invocation.
        m_link_schedule.clear();
    }
    m_np_port_mask.store(port_mask == 0 ? 0x1u : port_mask, std::memory_order_relaxed);
    if (start_frame >= 0)
        m_frame_counter.store(start_frame, std::memory_order_relaxed);
    m_netplay_enabled.store(enabled, std::memory_order_release);
    m_np_cv.notify_all();
    Log("Netplay mode " + std::string(enabled ? "ON" : "OFF") +
        " mask=" + std::to_string(port_mask) + " start_frame=" + std::to_string(start_frame));
}

void Wrapper::PostNetplayInputs(int64_t frame, const godot::PackedInt32Array& flat)
{
    if (flat.size() < 20)
        return;
    NpFrame values{};
    const int n = flat.size() < NP_FRAME_INTS ? static_cast<int>(flat.size()) : NP_FRAME_INTS;
    for (int i = 0; i < n; ++i)
        values[i] = flat[i];
    const bool rollback = m_np_rollback.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(m_np_mutex);
        const int64_t current = m_frame_counter.load(std::memory_order_relaxed);
        const int64_t oldest = rollback && current > std::numeric_limits<int64_t>::min() + NP_ROLLBACK_HISTORY
            ? current - NP_ROLLBACK_HISTORY
            : (rollback ? std::numeric_limits<int64_t>::min() : current);
        const int64_t newest = current < std::numeric_limits<int64_t>::max() - NP_MAX_FUTURE_INPUTS
            ? current + NP_MAX_FUTURE_INPUTS
            : std::numeric_limits<int64_t>::max();

        // Bound the schedule by frame distance, not total map size. A global
        // size guard lets far-future packets fill the map and then rejects the
        // current frame forever, so the consumer can never advance and prune.
        m_np_inputs.erase(m_np_inputs.begin(), m_np_inputs.lower_bound(oldest));
        m_np_inputs.erase(m_np_inputs.upper_bound(newest), m_np_inputs.end());
        if (frame < oldest || frame > newest)
            return;
        // Lockstep: inputs for already-run frames are stale. Rollback: they are
        // CONFIRMATIONS for speculatively-run frames, which is the point.
        if (!rollback && frame < current)
            return;
        m_np_inputs[frame] = values;
    }
    m_np_cv.notify_all();
}

void Wrapper::SetNetplayRollback(bool enabled, uint32_t local_mask, int max_ahead)
{
    {
        std::lock_guard<std::mutex> lock(m_np_mutex);
        m_np_live_local.fill(0);
        m_np_local_records.clear();
        m_np_initial_local_mask = local_mask;
        m_np_local_mask_schedule.clear();
    }
    m_np_local_mask.store(local_mask, std::memory_order_relaxed);
    m_np_max_ahead = std::clamp(max_ahead, 2, NP_MAX_AHEAD_LIMIT);
    m_np_rollback.store(enabled, std::memory_order_release);
    m_np_cv.notify_all();
    Log("Netplay rollback " + std::string(enabled ? "ON" : "OFF") +
        " local_mask=" + std::to_string(local_mask) +
        " max_ahead=" + std::to_string(max_ahead));
}

bool Wrapper::ScheduleNetplayLocalMask(int64_t frame, uint32_t local_mask)
{
    if (!m_np_rollback.load(std::memory_order_acquire))
        return false;
    {
        std::lock_guard<std::mutex> lock(m_np_mutex);
        if (frame < m_frame_counter.load(std::memory_order_relaxed))
            return false;
        m_np_local_mask_schedule[frame] = local_mask;
    }
    m_np_cv.notify_all();
    Log("Netplay local ownership mask=" + std::to_string(local_mask) +
        " @frame " + std::to_string(frame));
    return true;
}

godot::PackedInt32Array Wrapper::TakeNetplayLocalRecords()
{
    godot::PackedInt32Array out;
    std::lock_guard<std::mutex> lock(m_np_mutex);
    if (m_np_local_records.empty())
        return out;
    out.resize(static_cast<int64_t>(m_np_local_records.size()));
    for (size_t i = 0; i < m_np_local_records.size(); ++i)
        out[static_cast<int64_t>(i)] = m_np_local_records[i];
    m_np_local_records.clear();
    return out;
}

bool Wrapper::AcceptsEmuCommands() const
{
    return m_core && (m_running.load(std::memory_order_acquire) ||
                      m_starting.load(std::memory_order_acquire));
}

void Wrapper::AnswerNoSaveState()
{
    if (Libretro* node = LiveLibretroNode())
        node->call_deferred("emit_signal", "savestate_ready",
            godot::PackedByteArray(), static_cast<int64_t>(-1));
}

void Wrapper::AnswerNoLoadState()
{
    if (Libretro* node = LiveLibretroNode())
        node->call_deferred("emit_signal", "savestate_loaded", false);
}

void Wrapper::AnswerNoDiskInfo()
{
    if (Libretro* node = LiveLibretroNode())
        node->call_deferred("emit_signal", "disk_control_ready",
            false, static_cast<int64_t>(0), static_cast<int64_t>(0), false);
}

void Wrapper::RequestSaveState()
{
    if (!AcceptsEmuCommands())
    {
        AnswerNoSaveState();
        return;
    }
    m_emu_thread_commands_queue.enqueue(std::make_unique<EmuThreadCommandSaveState>());
}

void Wrapper::RequestLoadState(const godot::PackedByteArray& data, int64_t frame)
{
    if (!AcceptsEmuCommands())
    {
        AnswerNoLoadState();
        return;
    }
    m_emu_thread_commands_queue.enqueue(std::make_unique<EmuThreadCommandLoadState>(data, frame));
}

// ── Disk control (multi-disc games) ──────────────────────────────────────────

void Wrapper::RequestDiskInfo()
{
    if (!AcceptsEmuCommands())
    {
        AnswerNoDiskInfo();
        return;
    }
    m_emu_thread_commands_queue.enqueue(std::make_unique<EmuThreadCommandDiskInfo>());
}

void Wrapper::SetDiskEjectState(bool ejected)
{
    if (!AcceptsEmuCommands())
        return;
    m_emu_thread_commands_queue.enqueue(std::make_unique<EmuThreadCommandSetDiskEjected>(ejected));
}

void Wrapper::ReplaceDiskImage(uint32_t index, const godot::String& path)
{
    if (!AcceptsEmuCommands())
        return;
    m_emu_thread_commands_queue.enqueue(
        std::make_unique<EmuThreadCommandReplaceDisk>(index, std::string(path.utf8().get_data())));
}

void Wrapper::ScheduleDiscOp(int64_t frame, int32_t op, uint32_t index, const godot::String& path)
{
    if (!m_core || !m_running)
        return;
    Log("Disc op " + std::to_string(op) + " scheduled for frame " + std::to_string(frame)
        + " (index " + std::to_string(index) + ")");
    std::lock_guard<std::mutex> lock(m_np_mutex);
    m_disc_schedule.emplace(frame, DiscOp{ op, index, std::string(path.utf8().get_data()) });
}

void Wrapper::ScheduleLinkOp(int64_t frame, int32_t op,
                             const std::vector<std::pair<Wrapper*, unsigned>>& group)
{
    if (!m_core || !m_running || group.empty())
        return;
    Log("Link op " + std::to_string(op) + " scheduled for frame " + std::to_string(frame)
        + " over " + std::to_string(group.size()) + " machine(s)");
    std::lock_guard<std::mutex> lock(m_np_mutex);
    m_link_schedule.emplace(frame, LinkOp{ op, group });
}

/// Emulation thread: apply any netplay-scheduled link change whose frame has
/// arrived, strictly before running `frame`.
///
/// LinkCoordinator says this is the caller's job and it is right: it cannot
/// enforce that Connect and Disconnect land on the same emulated frame on every
/// peer, and a cable applied whenever a hand happened to move is a different
/// frame on each of them. It takes its own lock, so calling it from here is
/// safe even though the room schedules from the main thread.
void Wrapper::ApplyScheduledLinkOps(int64_t frame)
{
    std::vector<LinkOp> pending;
    {
        std::lock_guard<std::mutex> lock(m_np_mutex);
        auto it = m_link_schedule.begin();
        while (it != m_link_schedule.end() && it->first <= frame)
        {
            pending.push_back(std::move(it->second));
            it = m_link_schedule.erase(it);
        }
    }
    for (const LinkOp& op : pending)
    {
        if (op.group.empty())
            continue;
        if (op.op == 0)
        {
            LinkCoordinator::Get().Disconnect(op.group[0].first, op.group[0].second);
            Log("Netplay link PULL applied @frame " + std::to_string(frame));
        }
        else
        {
            const bool ok = LinkCoordinator::Get().ConnectGroup(op.group);
            LogOK("Netplay link JOIN applied @frame " + std::to_string(frame)
                + " over " + std::to_string(op.group.size()) + " machine(s), ok="
                + std::string(ok ? "true" : "false"));
        }
    }
}

/// Emulation thread: read the disk-control state and emit disk_control_ready.
void Wrapper::EmitDiskInfo()
{
    bool has = false;
    int64_t count = 0;
    int64_t index = 0;
    bool ejected = false;
    if (m_environment_handler)
    {
        has = m_environment_handler->HasDiskControl();
        if (has)
        {
            count = static_cast<int64_t>(m_environment_handler->GetDiskImageCount());
            index = static_cast<int64_t>(m_environment_handler->GetDiskImageIndex());
            ejected = m_environment_handler->GetDiskEjected();
        }
    }
    Log("Disk control: has=" + std::string(has ? "yes" : "no")
        + " images=" + std::to_string(count)
        + " index=" + std::to_string(index)
        + " ejected=" + std::string(ejected ? "yes" : "no"));
    godot::Array args;
    args.append(has);
    args.append(count);
    args.append(index);
    args.append(ejected);
    EmitSignalOnMainThread("disk_control_ready", args);
}

/// Emulation thread: apply any netplay-scheduled disc op whose frame has
/// arrived, strictly before running `frame`, so every netplay peer swaps on
/// the identical frame. Rollback waits for this boundary to be confirmed first.
void Wrapper::ApplyScheduledDiscOps(int64_t frame)
{
    std::vector<DiscOp> pending;
    {
        std::lock_guard<std::mutex> lock(m_np_mutex);
        auto it = m_disc_schedule.begin();
        while (it != m_disc_schedule.end() && it->first <= frame)
        {
            pending.push_back(it->second);
            it = m_disc_schedule.erase(it);
        }
    }
    if (pending.empty() || !m_environment_handler)
        return;
    for (const DiscOp& op : pending)
    {
        if (op.op == 0)
        {
            Log("Netplay disc EJECT applied @frame " + std::to_string(frame));
            m_environment_handler->SetDiskEjected(true);
        }
        else
        {
            LogOK("Netplay disc SWAP applied @frame " + std::to_string(frame)
                + " -> " + op.path);
            m_environment_handler->SetDiskEjected(true);   // idempotent if already open
            m_environment_handler->ReplaceDiskImage(op.index, op.path);
            m_environment_handler->SetDiskEjected(false);
        }
    }
    EmitDiskInfo();
}

/// Emulation thread: apply every front-panel reset due at this confirmed frame.
/// Rollback never crosses the boundary: the scheduler stalls until confirmation,
/// then the pre-reset history is discarded before the new anchor is serialized.
void Wrapper::ApplyScheduledResets(int64_t frame)
{
    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(m_np_mutex);
        auto end = m_reset_schedule.upper_bound(frame);
        count = static_cast<size_t>(std::distance(m_reset_schedule.begin(), end));
        m_reset_schedule.erase(m_reset_schedule.begin(), end);
    }
    if (count == 0 || !m_core || !m_core->retro_reset)
        return;
    for (size_t i = 0; i < count; ++i)
        m_core->retro_reset();
    m_np_states.clear();
    m_np_used.clear();
    m_np_crc_pending.clear();
    LogOK("Netplay reset applied @frame " + std::to_string(frame));
}

// ── Battery saves (SRAM) ─────────────────────────────────────────────────────

void Wrapper::SetSramPath(const godot::String& path)
{
    std::string p = path.utf8().get_data();
    if (m_running)
    {
        // Hot-swap on the emulation thread (memory-card insert/remove).
        m_emu_thread_commands_queue.enqueue(std::make_unique<EmuThreadCommandSetSram>(p));
        return;
    }
    m_sram_path = p;
}

void Wrapper::SetSramData(const godot::PackedByteArray& data)
{
    m_sram_pending = data;
}

void Wrapper::SetRemovableStorage(bool removable)
{
    m_removable_storage = removable;
}

void Wrapper::RequestSramFlush()
{
    if (m_core && m_running)
        m_emu_thread_commands_queue.enqueue(std::make_unique<EmuThreadCommandFlushSram>());
}

void Wrapper::RequestReset()
{
    if (m_core && m_running)
        m_emu_thread_commands_queue.enqueue(std::make_unique<EmuThreadCommandReset>());
}

void Wrapper::ScheduleReset(int64_t frame)
{
    if (!m_core || !m_running)
        return;
    Log("Reset scheduled for frame " + std::to_string(frame));
    std::lock_guard<std::mutex> lock(m_np_mutex);
    m_reset_schedule.insert(frame);
    m_np_cv.notify_all();
}

/// Emu thread: fill SAVE_RAM from the pending bytes (netplay) or the backing
/// file, then snapshot the shadow copy used for dirty checks.
void Wrapper::LoadSramFromSource()
{
    m_sram_shadow.clear();
    if (!m_core || !m_core->retro_get_memory_data || !m_core->retro_get_memory_size)
        return;
    void* sram = m_core->retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = m_core->retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (sram == nullptr || size == 0)
        return;

    if (m_sram_pending.size() > 0)
    {
        // Netplay-injected content: every peer boots with identical SRAM.
        size_t n = std::min(size, static_cast<size_t>(m_sram_pending.size()));
        std::memcpy(sram, m_sram_pending.ptr(), n);
        Log("SRAM: applied " + std::to_string(n) + " injected bytes (netplay)");
    }
    else if (!m_sram_path.empty() && std::filesystem::is_regular_file(m_sram_path))
    {
        std::ifstream file(m_sram_path, std::ios::binary | std::ios::ate);
        if (file)
        {
            size_t file_size = static_cast<size_t>(file.tellg());
            file.seekg(0, std::ios::beg);
            size_t n = std::min(size, file_size);
            file.read(reinterpret_cast<char*>(sram), n);
            Log("SRAM: loaded " + std::to_string(n) + " bytes from " + m_sram_path);
        }
    }
    else if (m_removable_storage)
    {
        // Nothing plugged in. Cores hand back their own idea of an empty save,
        // and pcsx_rearmed's is a fully FORMATTED card that a game will write to
        // and then lose at power-off. Blank it so the machine reports
        // unformatted media instead.
        std::memset(sram, 0, size);
        Log("SRAM: no removable media seated - SAVE_RAM blanked");
    }

    m_sram_shadow.assign(static_cast<uint8_t*>(sram), static_cast<uint8_t*>(sram) + size);
}

/// Emu thread: write SAVE_RAM to the backing file iff it changed since the
/// last flush. Never deletes or truncates an existing file to nothing.
void Wrapper::FlushSramIfDirty(bool final_flush)
{
    if (m_sram_path.empty() || !m_core || !m_core->retro_get_memory_data || !m_core->retro_get_memory_size)
        return;
    void* sram = m_core->retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = m_core->retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (sram == nullptr || size == 0)
        return;
    if (m_sram_shadow.size() == size &&
        std::memcmp(m_sram_shadow.data(), sram, size) == 0)
        return;   // unchanged

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(m_sram_path).parent_path(), ec);
    std::ofstream file(m_sram_path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        LogError("SRAM: cannot write " + m_sram_path);
        return;
    }
    file.write(static_cast<const char*>(sram), size);
    file.close();
    m_sram_shadow.assign(static_cast<uint8_t*>(sram), static_cast<uint8_t*>(sram) + size);
    Log("SRAM: flushed " + std::to_string(size) + " bytes to " + m_sram_path);

    // Closed above so the file is complete on disk before anyone is told about
    // it; a listener that uploads must never read a half-written save.
    if (Libretro* node = LiveLibretroNode())
        node->NotifySramFlushed(
            godot::String(m_sram_path.c_str()), static_cast<int64_t>(size), final_flush);
}

/// Emu thread: memory-card hot-swap. Flush the old card, adopt the new one.
void Wrapper::ApplySramSwap(const std::string& new_path)
{
    // Final for the outgoing file: nothing will write to it again this run, so
    // a listener should treat it as committed rather than debounce it.
    FlushSramIfDirty(true);
    m_sram_path = new_path;
    m_sram_pending = godot::PackedByteArray();
    LoadSramFromSource();
    Log("SRAM: swapped to " + (new_path.empty() ? std::string("<none>") : new_path));
}

/// snes9x's id for the BS-X 8M Memory Pack. Core-specific, so it is not in
/// libretro.h; the value is fixed by the core's own header.
static constexpr unsigned RETRO_MEMORY_SNES_BSX_PRAM = (2 << 8) | RETRO_MEMORY_SAVE_RAM;

void Wrapper::SetPackPath(const godot::String& path)
{
    m_pack_path = path.utf8().get_data();
}

/// Emu thread: remember the pack as the content load left it.
///
/// There is deliberately no read from disk here, unlike SRAM. The pack IS the
/// content -- the core was handed the .bs and loaded it into flash -- so reading
/// the file back over it would at best be a no-op and at worst overwrite the
/// medium with a stale copy of itself.
void Wrapper::SnapshotPack()
{
    m_pack_shadow.clear();
    if (m_pack_path.empty() || !m_core || !m_core->retro_get_memory_data || !m_core->retro_get_memory_size)
        return;
    void* pack = m_core->retro_get_memory_data(RETRO_MEMORY_SNES_BSX_PRAM);
    size_t size = m_core->retro_get_memory_size(RETRO_MEMORY_SNES_BSX_PRAM);
    if (pack == nullptr || size == 0)
        return;
    m_pack_shadow.assign(static_cast<uint8_t*>(pack), static_cast<uint8_t*>(pack) + size);
    Log("Pack: watching " + std::to_string(size) + " bytes for " + m_pack_path);
}

/// Emu thread: write the pack back over its own file iff the core changed it.
///
/// Written to a temporary beside the target and renamed, because this overwrites
/// the player's medium in place rather than a save file kept alongside it: a
/// half-written pack is a destroyed one, and there is no other copy.
void Wrapper::FlushPackIfDirty(bool final_flush)
{
    if (m_pack_path.empty() || !m_core || !m_core->retro_get_memory_data || !m_core->retro_get_memory_size)
        return;
    void* pack = m_core->retro_get_memory_data(RETRO_MEMORY_SNES_BSX_PRAM);
    size_t size = m_core->retro_get_memory_size(RETRO_MEMORY_SNES_BSX_PRAM);
    if (pack == nullptr || size == 0)
        return;
    if (m_pack_shadow.size() == size &&
        std::memcmp(m_pack_shadow.data(), pack, size) == 0)
        return;   // unchanged

    std::error_code ec;
    std::filesystem::path target(m_pack_path);
    std::filesystem::path tmp = target;
    tmp += ".part";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            LogError("Pack: cannot write " + tmp.string());
            return;
        }
        file.write(static_cast<const char*>(pack), size);
        if (!file)
        {
            file.close();
            std::filesystem::remove(tmp, ec);
            LogError("Pack: short write to " + tmp.string());
            return;
        }
    }
    std::filesystem::rename(tmp, target, ec);
    if (ec)
    {
        std::filesystem::remove(tmp, ec);
        LogError("Pack: cannot replace " + m_pack_path);
        return;
    }
    m_pack_shadow.assign(static_cast<uint8_t*>(pack), static_cast<uint8_t*>(pack) + size);
    Log("Pack: flushed " + std::to_string(size) + " bytes to " + m_pack_path);

    if (Libretro* node = LiveLibretroNode())
        node->NotifySramFlushed(
            godot::String(m_pack_path.c_str()), static_cast<int64_t>(size), final_flush);
}

void Wrapper::EmitSignalOnMainThread(const godot::StringName& signal_name, const godot::Array& args)
{
    m_main_thread_commands_queue.enqueue(
        std::make_unique<ThreadCommandEmitSignal>(this, signal_name, args));
}

void Wrapper::SetMemoryDescriptors(const retro_memory_map* memory_maps)
{
    m_memory_descriptors.clear();
    m_memory_addrspaces.clear();
    if (memory_maps == nullptr || memory_maps->descriptors == nullptr)
        return;

    const size_t count = memory_maps->num_descriptors;
    // Both reserved up front. The descriptors hold char* into the strings, so a
    // reallocation of either vector mid-loop would leave dangling pointers behind.
    m_memory_descriptors.reserve(count);
    m_memory_addrspaces.reserve(count);

    for (size_t i = 0; i < count; ++i)
        m_memory_addrspaces.emplace_back(memory_maps->descriptors[i].addrspace
            ? memory_maps->descriptors[i].addrspace : "");

    for (size_t i = 0; i < count; ++i)
    {
        retro_memory_descriptor descriptor = memory_maps->descriptors[i];
        // ptr is the core's own allocation and stays valid for the session, so it
        // is carried across as-is. Only addrspace has to be re-pointed at our copy.
        descriptor.addrspace = m_memory_addrspaces[i].empty()
            ? nullptr : m_memory_addrspaces[i].c_str();
        m_memory_descriptors.push_back(descriptor);
    }

    Log("Memory map: captured " + std::to_string(count) + " descriptor(s)");
}

void Wrapper::GetCoreMemory(uint32_t id, uint8_t*& out_data, size_t& out_size) const
{
    out_data = nullptr;
    out_size = 0;
    if (!m_core || !m_core->retro_get_memory_data || !m_core->retro_get_memory_size)
        return;
    out_data = static_cast<uint8_t*>(m_core->retro_get_memory_data(id));
    out_size = m_core->retro_get_memory_size(id);
}

retro_memory_map Wrapper::GetMemoryMap() const
{
    retro_memory_map map = {};
    map.descriptors = m_memory_descriptors.empty() ? nullptr : m_memory_descriptors.data();
    map.num_descriptors = static_cast<unsigned>(m_memory_descriptors.size());
    return map;
}

// Self-contained CRC32 (polynomial 0xEDB88320). libretro-common's crc32.c is
// not part of this build, and the table init must be thread-safe (multiple
// emulation threads may race the first call).
static const uint32_t* Crc32Table()
{
    static uint32_t table[256];
    static std::once_flag once;
    std::call_once(once, []
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t crc = i;
            for (int j = 0; j < 8; ++j)
                crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
            table[i] = crc;
        }
    });
    return table;
}

// Folds one more block into a running CRC. `crc` is the RAW register rather than
// a finished value: start at 0xFFFFFFFF, feed as many blocks as there are, and
// invert once at the end. That is what lets a hash span several disjoint regions
// without hashing a hash.
static uint32_t Crc32Update(uint32_t crc, const uint8_t* data, size_t size)
{
    const uint32_t* table = Crc32Table();
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}

static uint32_t Crc32(const uint8_t* data, size_t size)
{
    return Crc32Update(0xFFFFFFFFu, data, size) ^ 0xFFFFFFFFu;
}

uint32_t Wrapper::ComputeNetplayCrc(bool& ok)
{
    return m_np_crc_from_state ? StateCrc(ok) : ComputeRamCrc(ok);
}

uint32_t Wrapper::StateCrc(bool& ok)
{
    ok = false;
    if (!m_core || !m_core->retro_serialize || !m_core->retro_serialize_size)
        return 0;
    // Asked each time rather than cached: a core may grow its state as a game
    // loads more, and a stale size would hash a truncated one.
    const size_t size = m_core->retro_serialize_size();
    if (size == 0)
        return 0;
    if (m_np_crc_state_buffer.size() < size)
        m_np_crc_state_buffer.resize(size);
    if (!m_core->retro_serialize(m_np_crc_state_buffer.data(), size))
        return 0;
    ok = true;
    return Crc32(m_np_crc_state_buffer.data(), size);
}

uint32_t Wrapper::ComputeRamCrc(bool& ok) const
{
    ok = false;
    if (!m_core || !m_core->retro_get_memory_data || !m_core->retro_get_memory_size)
        return 0;
    void* ram = m_core->retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t size = m_core->retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    if (ram != nullptr && size != 0)
    {
        ok = true;
        return Crc32(static_cast<const uint8_t*>(ram), size);
    }
    return MappedRamCrc(ok);
}

uint32_t Wrapper::MappedRamCrc(bool& ok) const
{
    // Not every core answers RETRO_MEMORY_SYSTEM_RAM, and one that does not is
    // not thereby exempt from being checked for determinism. mGBA is the case in
    // hand: it returns NULL for SYSTEM_RAM and publishes a memory MAP instead,
    // so before this the netplay CRC signal simply never fired for it and the
    // determinism spike sat there waiting for checkpoints that were never coming.
    //
    // Everything writable, in the order the core listed it. CONST descriptors
    // are ROM and BIOS and cannot change, so hashing them would cost time and
    // tell nobody anything. What is left, on a Game Boy Advance, is IWRAM,
    // EWRAM, the save, VRAM, palette, OAM and the IO block. That is a STRICTER
    // oracle than a flat SYSTEM_RAM: a divergence that has only reached a sprite
    // table shows up here and would not show up there.
    //
    // Order is the core's own and does not vary between runs of the same build,
    // which is all a comparison needs. The number is not portable across cores
    // or across core versions, and nothing asks it to be: every use compares two
    // runs of one build against each other.
    uint32_t crc = 0xFFFFFFFFu;
    size_t hashed = 0;
    for (const retro_memory_descriptor& descriptor : m_memory_descriptors)
    {
        if (descriptor.ptr == nullptr || descriptor.len == 0)
            continue;
        if (descriptor.flags & RETRO_MEMDESC_CONST)
            continue;
        const uint8_t* base = static_cast<const uint8_t*>(descriptor.ptr) + descriptor.offset;
        crc = Crc32Update(crc, base, descriptor.len);
        hashed += descriptor.len;
    }
    ok = hashed > 0;
    return ok ? (crc ^ 0xFFFFFFFFu) : 0;
}

/// Emulation thread, once the core has been loaded and has declared its
/// options, and strictly before retro_load_game: a core reads the options that
/// decide what machine it even is - mGBA's gb_model among them - while loading
/// the game, so applying them afterwards would be applying them too late.
void Wrapper::ApplyPendingCoreOptions()
{
    std::vector<std::pair<std::string, std::string>> pending;
    {
        std::lock_guard<std::mutex> lock(m_pending_options_mutex);
        pending.swap(m_pending_core_options);
    }
    if (!m_options_handler)
        return;
    for (const auto& option : pending)
    {
        m_options_handler->SetVariable(option.first, option.second);
        Log("Applied held core option " + option.first + " = " + option.second);
    }
}

godot::Dictionary Wrapper::SnapshotMappedRam() const
{
    godot::Dictionary out;
    godot::PackedByteArray data;
    godot::Array regions;
    int64_t offset = 0;
    for (const retro_memory_descriptor& descriptor : m_memory_descriptors)
    {
        if (descriptor.ptr == nullptr || descriptor.len == 0)
            continue;
        if (descriptor.flags & RETRO_MEMDESC_CONST)
            continue;
        const uint8_t* base = static_cast<const uint8_t*>(descriptor.ptr) + descriptor.offset;
        const int64_t len = static_cast<int64_t>(descriptor.len);
        const int64_t at = data.size();
        data.resize(at + len);
        std::memcpy(data.ptrw() + at, base, static_cast<size_t>(len));
        godot::Dictionary region;
        region["offset"] = at;
        region["len"] = len;
        region["start"] = static_cast<int64_t>(descriptor.start);
        region["addrspace"] = descriptor.addrspace ? godot::String(descriptor.addrspace) : godot::String();
        regions.append(region);
        offset += len;
    }
    out["data"] = data;
    out["regions"] = regions;
    return out;
}

void Wrapper::PublishCoreIdentity()
{
    if (!m_core)
        return;
    std::lock_guard<std::mutex> lock(m_core_identity_mutex);
    m_core_library_name = m_core->GetLibraryName();
    m_core_library_version = m_core->GetLibraryVersion();
    m_core_api_version = m_core->GetApiVersion();
    m_core_identity_ready = true;
}

void Wrapper::PublishCoreSerializeSize()
{
    if (m_core_serialize_size_published || !m_core || !m_core->retro_serialize_size)
        return;
    m_core_serialize_size_published = true;
    const int64_t serialize_size = static_cast<int64_t>(m_core->retro_serialize_size());
    std::lock_guard<std::mutex> lock(m_core_identity_mutex);
    m_core_serialize_size = serialize_size;
}

void Wrapper::ClearCoreIdentity()
{
    std::lock_guard<std::mutex> lock(m_core_identity_mutex);
    m_core_library_name.clear();
    m_core_library_version.clear();
    m_core_api_version = 0;
    m_core_serialize_size = 0;
    m_core_identity_ready = false;
    m_core_serialize_size_published = false;
    m_core_ran_frame = false;
}

godot::Dictionary Wrapper::GetCoreIdentity() const
{
    godot::Dictionary out;
    std::lock_guard<std::mutex> lock(m_core_identity_mutex);
    if (!m_core_identity_ready)
        return out;
    out["library_name"] = godot::String(m_core_library_name.c_str());
    out["library_version"] = godot::String(m_core_library_version.c_str());
    out["api_version"] = static_cast<int64_t>(m_core_api_version);
    out["serialize_size"] = m_core_serialize_size;
    return out;
}

void Wrapper::EmitNetplayCrc(int64_t frame)
{
    bool ok = false;
    uint32_t crc = ComputeNetplayCrc(ok);
    if (!ok)
        return;
    godot::Array args;
    args.append(frame);
    args.append(static_cast<int64_t>(crc));
    EmitSignalOnMainThread("netplay_crc", args);
}

// ── Rollback engine (all emulation-thread) ───────────────────────────────────

void Wrapper::ApplyNetplayInputs(const NpFrame& inputs, uint32_t mask)
{
    for (uint32_t port = 0; port < 4; ++port)
    {
        if (!(mask & (1u << port)))
            continue;
        const int32_t* v = inputs.data() + port * 5;
        // Route by the port's device type: a RETRO_DEVICE_MOUSE port carries
        // {buttons, dx, dy, -, -} in its five slots (deltas accumulate until
        // the core's next read, so replaying a frame re-supplies exactly that
        // frame's delta, deterministic under both lockstep and rollback).
        uint32_t device = m_input_handler->GetPortDevice(port) & RETRO_DEVICE_MASK;
        if (device == RETRO_DEVICE_MOUSE)
        {
            m_input_handler->SetMousePosition(port, static_cast<int16_t>(v[1]), static_cast<int16_t>(v[2]));
            m_input_handler->SetMouseButtons(port, static_cast<uint32_t>(v[0]));
            continue;
        }
        if (device == RETRO_DEVICE_LIGHTGUN)
        {
            m_input_handler->SetLightgunButtons(port, static_cast<uint32_t>(v[0]));
            m_input_handler->SetLightgunPosition(port,
                static_cast<int16_t>(v[1]), static_cast<int16_t>(v[2]));
            m_input_handler->SetLightgunIsOffscreen(port, v[3] ? 1 : 0);
            continue;
        }
        m_input_handler->SetJoypadButtonStates(port, static_cast<uint16_t>(v[0]));
        m_input_handler->SetAnalogLeft(port, static_cast<int16_t>(v[1]), static_cast<int16_t>(v[2]));
        m_input_handler->SetAnalogRight(port, static_cast<int16_t>(v[3]), static_cast<int16_t>(v[4]));
    }
}

/// Apply every port's sensor and pointer block on the emulation thread, strictly
/// before the frame executes, so motion and touch share the deterministic line.
void Wrapper::ApplyNetplayAux(const NpFrame& inputs)
{
    const uint32_t mask = m_np_port_mask.load(std::memory_order_relaxed);
    for (uint32_t port = 0; port < NP_PORTS; ++port)
    {
        if (!(mask & (1u << port)))
            continue;
        const int base = NP_AUX_OFFSET + static_cast<int>(port) * NP_AUX_INTS_PER_PORT;
        const int32_t flags = inputs[base];
        for (uint32_t index = 0; index < NP_SENSOR_COUNT; ++index)
        {
            const int accel = base + 1 + static_cast<int>(index) * 3;
            const int gyro = base + 7 + static_cast<int>(index) * 3;
            if (flags & (1 << index))
                m_input_handler->SetSensorAccel(port,
                    inputs[accel] / 1000.0f, inputs[accel + 1] / 1000.0f,
                    inputs[accel + 2] / 1000.0f, index);
            else
                m_input_handler->SetSensorAccel(port, 0.0f, 0.0f, 1.0f, index);
            if (flags & (1 << (2 + index)))
                m_input_handler->SetSensorGyro(port,
                    inputs[gyro] / 100.0f, inputs[gyro + 1] / 100.0f,
                    inputs[gyro + 2] / 100.0f, index);
            else
                m_input_handler->SetSensorGyro(port, 0.0f, 0.0f, 0.0f, index);
        }
        for (uint32_t index = 0; index < NP_POINTER_COUNT; ++index)
        {
            const int pointer = base + 13 + static_cast<int>(index) * 3;
            if (flags & (1 << (4 + index)))
                m_input_handler->SetPointerIndexState(port, index,
                    static_cast<int16_t>(inputs[pointer]),
                    static_cast<int16_t>(inputs[pointer + 1]),
                    inputs[pointer + 2] != 0);
            else
                m_input_handler->SetPointerIndexState(port, index, 0, 0, false);
        }
    }
    // Key events: applied in slot order on the emulation thread, so the poll
    // bitset AND the core's keyboard event callback see the identical sequence
    // on every peer.
    for (int slot = 0; slot < NP_KEY_SLOTS; ++slot)
    {
        int32_t packed = inputs[NP_KEY_OFFSET + slot * 2];
        uint32_t keycode = static_cast<uint32_t>(packed) & 0xFFFF;
        if (keycode == 0)
            continue;
        bool down = (static_cast<uint32_t>(packed) >> 16) & 1;
        uint32_t character = static_cast<uint32_t>(inputs[NP_KEY_OFFSET + slot * 2 + 1]);
        m_input_handler->SetKeyState(0, keycode, down);
        m_input_handler->CallKeyboardEventCallback(down, keycode, character,
            m_input_handler->GetKeyModifiers(0));
    }
}

/// Serialize the machine state at the START of `frame` into the ring.
bool Wrapper::SaveRollbackState(int64_t frame)
{
    if (!m_np_states.empty() && m_np_states.back().frame >= frame)
        return true;   // already have it (rollback re-entry at the anchor frame)
    size_t size = m_core->retro_serialize_size();
    if (size == 0)
        return false;
    std::vector<uint8_t> buffer(size);
    const auto ser_t0 = std::chrono::steady_clock::now();
    const bool ser_ok = m_core->retro_serialize(buffer.data(), size);
    m_np_serialize_us.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - ser_t0).count(), std::memory_order_relaxed);
    m_np_serialize_n.fetch_add(1, std::memory_order_relaxed);
    if (!ser_ok)
        return false;
    m_np_states.push_back(RollbackState{
        frame,
        std::move(buffer),
        m_input_handler->CaptureNetplayState()
    });
    while (m_np_states.size() > static_cast<size_t>(StateRingDepth()))
        m_np_states.pop_front();
    m_np_state_slots.store(static_cast<int64_t>(m_np_states.size()),
                           std::memory_order_relaxed);
    int64_t resident = 0;
    for (const auto& s : m_np_states)
        resident += static_cast<int64_t>(s.core.size());
    m_np_state_bytes.store(resident, std::memory_order_relaxed);
    return true;
}

void Wrapper::FailNetplayRollback(const std::string& reason)
{
    LogError("Rollback cannot recover safely: " + reason);
    if (Libretro* node = LiveLibretroNode())
        node->call_deferred("emit_signal", "netplay_error",
            godot::String(reason.c_str()));
    // Continuing from a timeline that failed to rewind would silently desync.
    // Use the same deferred-stop path as RETRO_ENVIRONMENT_SHUTDOWN so teardown
    // remains on the main thread and never joins the emulation thread itself.
    StopEmulationThread(false);
}

/// Emit captured RAM CRCs once their frame is confirmed-verified; a CRC of a
/// speculative frame would false-positive against peers.
void Wrapper::FlushNetplayCrcs()
{
    for (auto it = m_np_crc_pending.begin(); it != m_np_crc_pending.end();)
    {
        if (it->first - 1 > m_np_verified)
            break;   // ordered map, so nothing later qualifies either
        godot::Array args;
        args.append(it->first);
        args.append(static_cast<int64_t>(it->second));
        EmitSignalOnMainThread("netplay_crc", args);
        it = m_np_crc_pending.erase(it);
    }
}

uint32_t Wrapper::NetplayLocalMaskForFrameLocked(int64_t frame) const
{
    auto next = m_np_local_mask_schedule.upper_bound(frame);
    if (next == m_np_local_mask_schedule.begin())
        return m_np_initial_local_mask;
    return std::prev(next)->second;
}

uint32_t Wrapper::ApplyScheduledNetplayLocalMask(int64_t frame)
{
    std::lock_guard<std::mutex> lock(m_np_mutex);
    const uint32_t next = NetplayLocalMaskForFrameLocked(frame);
    const uint32_t previous = m_np_local_mask.exchange(next, std::memory_order_acq_rel);
    const uint32_t changed = previous ^ next;
    for (uint32_t port = 0; port < NP_PORTS; ++port)
    {
        if (changed & (1u << port))
            std::fill_n(m_np_live_local.data() + port * NP_INPUT_INTS_PER_PORT,
                        NP_INPUT_INTS_PER_PORT, 0);
    }
    return next;
}

/// Rewind to `to_frame` and silently replay up to the current frame with
/// corrected inputs. Audio from replayed frames is dropped (it already played
/// from the mispredicted run); video is skipped except for the final frame.
bool Wrapper::NetplayRollbackReplay(int64_t to_frame, uint32_t mask)
{
    const auto rb_t0 = std::chrono::steady_clock::now();
    auto state_it = std::find_if(m_np_states.begin(), m_np_states.end(),
        [&](const auto& s) { return s.frame == to_frame; });
    if (state_it == m_np_states.end())
    {
        LogWarning("Rollback: no saved state for frame " + std::to_string(to_frame));
        return false;
    }
    if (!m_core->retro_unserialize(state_it->core.data(), state_it->core.size()))
    {
        LogError("Rollback: unserialize failed at frame " + std::to_string(to_frame));
        return false;
    }
    m_input_handler->RestoreNetplayState(state_it->input);
    const int64_t current = m_frame_counter.load(std::memory_order_relaxed);

    // Drop everything the rewind invalidated: states after the anchor and CRCs
    // captured on the mispredicted timeline.
    m_np_states.erase(std::next(state_it), m_np_states.end());
    m_np_crc_pending.erase(m_np_crc_pending.upper_bound(to_frame), m_np_crc_pending.end());

    m_np_replaying = true;
    for (int64_t x = to_frame; x < current; ++x)
    {
        NpFrame inputs{};
        {
            std::lock_guard<std::mutex> lock(m_np_mutex);
            const uint32_t local_mask = NetplayLocalMaskForFrameLocked(x) & mask;
            auto confirmed = m_np_inputs.find(x);
            auto& prev_used = m_np_used;
            for (uint32_t port = 0; port < 4; ++port)
            {
                if (!(mask & (1u << port)))
                    continue;
                int32_t* dst = inputs.data() + port * 5;
                if (local_mask & (1u << port))
                {
                    // Local input is ground truth: replay exactly what was pressed.
                    auto used = prev_used.find(x);
                    if (used != prev_used.end())
                        std::copy_n(used->second.data() + port * 5, 5, dst);
                }
                else if (confirmed != m_np_inputs.end())
                {
                    std::copy_n(confirmed->second.data() + port * 5, 5, dst);
                }
                else
                {
                    // Beyond the watermark: re-predict by holding the previous
                    // frame's (now corrected) value.
                    auto prev = prev_used.find(x - 1);
                    if (prev != prev_used.end())
                        std::copy_n(prev->second.data() + port * 5, 5, dst);
                }
            }
            if (confirmed != m_np_inputs.end())
                std::copy(confirmed->second.begin() + NP_AUX_OFFSET,
                          confirmed->second.end(), inputs.begin() + NP_AUX_OFFSET);
        }
        m_np_used[x] = inputs;
        if (!SaveRollbackState(x))   // no-op for the anchor, re-saves the rest
        {
            m_np_replaying = false;
            m_np_replay_mute_video = false;
            return false;
        }
        m_np_replay_mute_video = (x != current - 1);
        ApplyNetplayInputs(inputs, mask);
        ApplyNetplayAux(inputs);
        m_core->retro_run();
        m_core_ran_frame = true;
        if (m_np_crc_interval > 0 && (x + 1) % m_np_crc_interval == 0)
        {
            bool ok = false;
            uint32_t crc = ComputeNetplayCrc(ok);
            if (ok)
                m_np_crc_pending[x + 1] = crc;
        }
    }
    m_np_replaying = false;
    m_np_replay_mute_video = false;
    // Every replayed frame ≤ watermark ran with confirmed inputs.
    m_np_verified = std::min(m_np_watermark, current - 1);
    m_np_rollback_count.fetch_add(1, std::memory_order_relaxed);
    const int64_t depth = current - to_frame;
    m_np_replay_frames.fetch_add(depth, std::memory_order_relaxed);
    m_np_replay_us.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - rb_t0).count(), std::memory_order_relaxed);
    // How deep a rewind actually goes is what the ring has to be sized for,
    // and it is a measurement rather than a thing to reason about.
    int64_t seen = m_np_max_depth.load(std::memory_order_relaxed);
    while (depth > seen
           && !m_np_max_depth.compare_exchange_weak(seen, depth,
                                                    std::memory_order_relaxed))
    {
    }
    return true;
}

/// What rollback costs here. Read by a probe; safe to call at any time.
godot::Dictionary Wrapper::GetNetplayRollbackStats() const
{
    godot::Dictionary d;
    const int64_t ser_n = m_np_serialize_n.load(std::memory_order_relaxed);
    const int64_t ser_us = m_np_serialize_us.load(std::memory_order_relaxed);
    const int64_t rb_n = m_np_rollback_count.load(std::memory_order_relaxed);
    const int64_t rb_us = m_np_replay_us.load(std::memory_order_relaxed);
    const int64_t rb_f = m_np_replay_frames.load(std::memory_order_relaxed);
    d["serialize_count"] = ser_n;
    d["serialize_us_total"] = ser_us;
    d["serialize_us_mean"] = ser_n > 0 ? double(ser_us) / double(ser_n) : 0.0;
    d["rollback_count"] = rb_n;
    d["replay_us_total"] = rb_us;
    d["replay_us_mean"] = rb_n > 0 ? double(rb_us) / double(rb_n) : 0.0;
    d["replay_frames"] = rb_f;
    d["max_depth"] = m_np_max_depth.load(std::memory_order_relaxed);
    d["state_bytes"] = m_np_state_bytes.load(std::memory_order_relaxed);
    d["state_slots"] = m_np_state_slots.load(std::memory_order_relaxed);
    d["ring_limit"] = static_cast<int64_t>(StateRingDepth());
    d["max_ahead"] = static_cast<int64_t>(m_np_max_ahead);
    return d;
}

void Wrapper::NetplayRollbackIteration(double frame_duration_ms, double& accumulator)
{
    const uint32_t mask = m_np_port_mask.load(std::memory_order_relaxed);
    int64_t frame = m_frame_counter.load(std::memory_order_relaxed);

    // 1. Advance the confirmed watermark and verify executed frames against
    //    confirmations; the first contradiction sets the rollback anchor.
    int64_t rollback_to = -1;
    {
        std::lock_guard<std::mutex> lock(m_np_mutex);
        while (m_np_inputs.count(m_np_watermark + 1))
            ++m_np_watermark;
        const int64_t verify_upto = std::min(m_np_watermark, frame - 1);
        for (int64_t x = m_np_verified + 1; x <= verify_upto; ++x)
        {
            const uint32_t local_mask = NetplayLocalMaskForFrameLocked(x) & mask;
            auto confirmed = m_np_inputs.find(x);
            auto used = m_np_used.find(x);
            if (confirmed == m_np_inputs.end() || used == m_np_used.end())
            {
                m_np_verified = x;
                continue;
            }
            bool match = true;
            for (uint32_t port = 0; port < 4 && match; ++port)
            {
                // This peer's live local input is authoritative; the assembled
                // confirmation cannot correct it and must not cause a replay.
                if (!(mask & (1u << port)) || (local_mask & (1u << port)))
                    continue;
                match = std::equal(confirmed->second.data() + port * 5,
                                   confirmed->second.data() + port * 5 + 5,
                                   used->second.data() + port * 5);
            }
            if (match)
                match = std::equal(confirmed->second.begin() + NP_AUX_OFFSET,
                                   confirmed->second.end(),
                                   used->second.begin() + NP_AUX_OFFSET);
            if (!match)
            {
                rollback_to = x;
                break;
            }
            m_np_verified = x;
        }
    }
    if (rollback_to >= 0 && !NetplayRollbackReplay(rollback_to, mask))
    {
        FailNetplayRollback("failed to restore frame " + std::to_string(rollback_to));
        return;
    }

    // 2. Speculation throttle: never run more than max_ahead past the last
    //    confirmed frame (waits briefly; the outer loop re-enters and keeps
    //    draining commands / re-checking stop, so shutdown can't deadlock).
    frame = m_frame_counter.load(std::memory_order_relaxed);
    {
        std::unique_lock<std::mutex> lock(m_np_mutex);
        while (m_np_inputs.count(m_np_watermark + 1))
            ++m_np_watermark;
        auto can_run = [&]
        {
            while (m_np_inputs.count(m_np_watermark + 1))
                ++m_np_watermark;
            const bool speculation_ok = frame <= m_np_watermark + m_np_max_ahead;
            const bool disc_due = !m_disc_schedule.empty() && m_disc_schedule.begin()->first <= frame;
            const bool reset_due = !m_reset_schedule.empty() && *m_reset_schedule.begin() <= frame;
            // Disc state lives partly outside retro_serialize. Do not cross a
            // scheduled swap or reset speculatively: wait until this frame is
            // confirmed, apply it once, and it can never sit behind a later
            // rollback anchor.
            const bool boundary_ok = (!disc_due && !reset_due) || frame <= m_np_watermark;
            return m_stop_requested.load() || (speculation_ok && boundary_ok);
        };
        if (!can_run())
        {
            m_np_cv.wait_for(lock, std::chrono::milliseconds(4), can_run);
            if (!can_run())
                return;   // still stalled; outer loop spins us back in
        }
        if (m_stop_requested.load())
            return;
    }

    // 3. Pace to the core's fps.
    if (accumulator < frame_duration_ms)
        return;
    if (accumulator > frame_duration_ms * 4.0)
        accumulator = frame_duration_ms * 4.0;
    accumulator -= frame_duration_ms;

    ApplyScheduledDiscOps(frame);
    ApplyScheduledResets(frame);
    const uint32_t local_mask = ApplyScheduledNetplayLocalMask(frame) & mask;
    if (!SaveRollbackState(frame))
    {
        FailNetplayRollback("core failed to serialize frame " + std::to_string(frame));
        return;
    }

    // 4. Build this frame's inputs: live local, confirmed remote if already
    //    here, otherwise hold-last prediction. Record local values for the wire.
    NpFrame inputs{};
    {
        std::lock_guard<std::mutex> lock(m_np_mutex);
        auto confirmed = m_np_inputs.find(frame);
        for (uint32_t port = 0; port < 4; ++port)
        {
            if (!(mask & (1u << port)))
                continue;
            int32_t* dst = inputs.data() + port * 5;
            if (local_mask & (1u << port))
            {
                std::copy_n(m_np_live_local.data() + port * 5, 5, dst);
                m_np_local_records.push_back(static_cast<int32_t>(frame));
                m_np_local_records.push_back(static_cast<int32_t>(port));
                for (int i = 0; i < 5; ++i)
                    m_np_local_records.push_back(dst[i]);
                if ((m_input_handler->GetPortDevice(port) & RETRO_DEVICE_MASK) == RETRO_DEVICE_MOUSE)
                {
                    // Relative deltas belong to exactly one emulated frame;
                    // buttons remain held until the next main-thread update.
                    m_np_live_local[port * 5 + 1] = 0;
                    m_np_live_local[port * 5 + 2] = 0;
                }
            }
            else if (confirmed != m_np_inputs.end())
            {
                std::copy_n(confirmed->second.data() + port * 5, 5, dst);
            }
            else
            {
                auto prev = m_np_used.find(frame - 1);
                if (prev != m_np_used.end())
                    std::copy_n(prev->second.data() + port * 5, 5, dst);
            }
        }
        if (confirmed != m_np_inputs.end())
            std::copy(confirmed->second.begin() + NP_AUX_OFFSET,
                      confirmed->second.end(), inputs.begin() + NP_AUX_OFFSET);
        // Prune consumed confirmations well behind the verified point.
        m_np_inputs.erase(m_np_inputs.begin(), m_np_inputs.lower_bound(m_np_verified - NP_ROLLBACK_HISTORY));
    }
    m_np_used[frame] = inputs;
    m_np_used.erase(m_np_used.begin(), m_np_used.lower_bound(m_np_verified - NP_ROLLBACK_HISTORY));

    // 5. Run and count. The core + frontend input snapshot was captured above,
    // after any confirmed disc operation and before this frame's inputs.
    ApplyNetplayInputs(inputs, mask);
    ApplyNetplayAux(inputs);
    m_audio_handler->CallAudioBufferStatusCallback();
    m_core->retro_run();
    m_core_ran_frame = true;
    const int64_t frame_done = m_frame_counter.fetch_add(1, std::memory_order_relaxed) + 1;

    if (m_np_crc_interval > 0 && frame_done % m_np_crc_interval == 0)
    {
        bool ok = false;
        uint32_t crc = ComputeNetplayCrc(ok);
        if (ok)
            m_np_crc_pending[frame_done] = crc;
    }
    FlushNetplayCrcs();
}

void Wrapper::SetCoreOption(const std::string& key, const std::string& value)
{
    Log("SetCoreOption: key=" + key + " value=" + value);

    if (!m_options_handler || !m_running.load(std::memory_order_acquire)
        || m_stopping.load(std::memory_order_acquire))
    {
        // Not "skipping". A caller sets options and THEN starts content, so
        // this is the ordinary path, not an error one. Hold them until the
        // core has declared what it understands.
        std::lock_guard<std::mutex> lock(m_pending_options_mutex);
        for (auto& pending : m_pending_core_options)
        {
            if (pending.first == key)
            {
                pending.second = value;
                return;
            }
        }
        m_pending_core_options.emplace_back(key, value);
        Log("SetCoreOption: core not up yet; held for start");
        return;
    }

    m_emu_thread_commands_queue.enqueue(
        std::make_unique<EmuThreadCommandSetCoreOption>(key, value));
}

bool Wrapper::SetSystemAvInfo(const retro_system_av_info* av_info)
{
    if (!av_info || !m_running.load(std::memory_order_acquire) ||
        m_stopping.load(std::memory_order_acquire) || m_handling_av_info)
    {
        LogError("SET_SYSTEM_AV_INFO is unavailable outside a running frame");
        return false;
    }

    // The core owns the callback argument. Copy it before context_destroy or a
    // main-thread wait can invalidate the storage behind that pointer.
    const retro_system_av_info requested = *av_info;
    const auto valid_av_info = [](const retro_system_av_info& info)
    {
        const retro_game_geometry& geometry = info.geometry;
        const retro_system_timing& timing = info.timing;
        const unsigned max_dimension =
            static_cast<unsigned>(std::numeric_limits<int32_t>::max());
        return geometry.base_width > 0 && geometry.base_height > 0 &&
            geometry.max_width >= geometry.base_width &&
            geometry.max_height >= geometry.base_height &&
            geometry.base_width <= max_dimension &&
            geometry.base_height <= max_dimension &&
            geometry.max_width <= max_dimension &&
            geometry.max_height <= max_dimension &&
            std::isfinite(geometry.aspect_ratio) && geometry.aspect_ratio >= 0.0f &&
            std::isfinite(timing.fps) && timing.fps > 0.0 &&
            std::isfinite(timing.sample_rate) && timing.sample_rate >= 0.0;
    };
    if (!valid_av_info(requested))
    {
        LogError("Core supplied invalid system AV info");
        return false;
    }

    m_handling_av_info = true;
    ScopeExit finish([this]() { m_handling_av_info = false; });

    const retro_system_av_info previous = m_system_av_info;
    const auto hw_max_dimensions_changed = [&](const retro_system_av_info& info)
    {
        return m_video_handler->UsesHardwareRendering() &&
            (info.geometry.max_width != previous.geometry.max_width ||
             info.geometry.max_height != previous.geometry.max_height);
    };
    if (hw_max_dimensions_changed(requested))
    {
        // These backends expose a surface at the base dimensions. Accepting a
        // larger maximum would promise SET_GEOMETRY can grow into capacity that
        // does not exist, while sizing the surface to max distorts base frames.
        LogError("Changing maximum video dimensions is unsupported for hardware rendering");
        return false;
    }

    bool video_reinitialized = false;
    const auto restore_video = [&]()
    {
        m_system_av_info = previous;
        if (video_reinitialized && !m_stop_requested.load() &&
            !m_video_handler->ReinitHwRenderContext(
                static_cast<int32_t>(previous.geometry.base_width),
                static_cast<int32_t>(previous.geometry.base_height)))
        {
            LogError("Failed to restore the previous hardware render context");
            StopEmulationThread(false);
        }
        // A context_reset may write through the persistent pointer. A failed
        // SET_SYSTEM_AV_INFO is transactional, so retain the pre-call values.
        m_system_av_info = previous;
    };

    // Some cores retain the retro_get_system_av_info destination and revise it
    // from context_reset. Stage the request there before invoking that callback,
    // then treat its post-reset values as the final declaration.
    m_system_av_info = requested;
    if (requested.geometry.base_width != previous.geometry.base_width ||
        requested.geometry.base_height != previous.geometry.base_height)
    {
        video_reinitialized = true;
        if (!m_video_handler->ReinitHwRenderContext(
                static_cast<int32_t>(requested.geometry.base_width),
                static_cast<int32_t>(requested.geometry.base_height)))
        {
            restore_video();
            return false;
        }

        if (!valid_av_info(m_system_av_info))
        {
            LogError("Core supplied invalid system AV info from context_reset");
            restore_video();
            return false;
        }

        if (hw_max_dimensions_changed(m_system_av_info))
        {
            LogError("context_reset changed unsupported maximum video dimensions");
            restore_video();
            return false;
        }

        // If context_reset revised the base geometry, rebuild once more at the
        // dimensions it actually settled on. Reject an unstable second change.
        const unsigned reset_width = m_system_av_info.geometry.base_width;
        const unsigned reset_height = m_system_av_info.geometry.base_height;
        if (reset_width != requested.geometry.base_width ||
            reset_height != requested.geometry.base_height)
        {
            if (!m_video_handler->ReinitHwRenderContext(
                    static_cast<int32_t>(reset_width),
                    static_cast<int32_t>(reset_height)) ||
                !valid_av_info(m_system_av_info) ||
                hw_max_dimensions_changed(m_system_av_info) ||
                m_system_av_info.geometry.base_width != reset_width ||
                m_system_av_info.geometry.base_height != reset_height)
            {
                LogError("Core supplied unstable geometry from context_reset");
                restore_video();
                return false;
            }
        }
    }

    if (!valid_av_info(m_system_av_info))
    {
        restore_video();
        return false;
    }

    if (m_system_av_info.timing.sample_rate != previous.timing.sample_rate)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_mutex_done = false;
        m_audio_reinit_success = false;
        m_audio_reinit_restore_failed = false;
        m_main_thread_commands_queue.enqueue(std::make_unique<ThreadCommandReinitAudio>(
            this, m_system_av_info.timing.sample_rate));
        m_condition_variable.wait(lock, [&]
            { return m_mutex_done || m_stop_requested.load(); });

        if (!m_mutex_done || !m_audio_reinit_success || m_stop_requested.load())
        {
            const bool restore_failed = m_audio_reinit_restore_failed;
            lock.unlock();
            restore_video();
            if (restore_failed)
            {
                LogError("Previous audio backend could not be restored; stopping the core");
                StopEmulationThread(false);
            }
            LogError("Failed to apply the core's new audio sample rate");
            return false;
        }
    }

    m_video_handler->SetGeometry(&m_system_av_info.geometry);
    const retro_game_geometry& applied_geometry = m_system_av_info.geometry;
    const retro_system_timing& applied_timing = m_system_av_info.timing;
    m_declared_fps.store(applied_timing.fps, std::memory_order_relaxed);
    m_declared_sample_rate.store(applied_timing.sample_rate, std::memory_order_relaxed);
    if (applied_timing.fps != previous.timing.fps)
        m_timing_revision.fetch_add(1, std::memory_order_release);

    Log("System AV Info: " + std::to_string(applied_geometry.base_width) + "x" +
        std::to_string(applied_geometry.base_height) + " @ " +
        std::to_string(applied_geometry.max_width) + "x" +
        std::to_string(applied_geometry.max_height) + " (aspect ratio: " +
        std::to_string(applied_geometry.aspect_ratio) + ") FPS: " +
        std::to_string(applied_timing.fps) + " Sample Rate: " +
        std::to_string(applied_timing.sample_rate));
    return true;
}

void Wrapper::_process(double delta)
{
    // Deferred stop: once the emulation thread has fully exited on its own,
    // the join is instant, so finish the teardown here without ever blocking.
    if (m_stopping.load(std::memory_order_acquire))
    {
        // While the stop is pending, keep DISCARDING queued main-thread
        // commands: a core hung in teardown (mupen gles3) can keep streaming
        // texture-update commands from its internal threads, and an undrained
        // queue grows a full frame's pixels per callback until OOM.
        std::unique_ptr<ThreadCommand> stale;
        while (m_main_thread_commands_queue.try_dequeue(stale))
            stale.reset();

        if (m_thread_exited.load(std::memory_order_acquire))
        {
            if (m_thread.joinable())
                m_thread.join();
            FinishTeardown();
        }
        return;
    }

    if (!m_running)
        return;

    // Drain to empty, but collapse each run of frame uploads to its last. The
    // emulation thread produces at the core's frame rate whatever the main
    // thread manages, so a main thread that falls behind has to chase a queue
    // that keeps refilling; once one upload costs more than a frame, executing
    // every one of them means _process stops returning at all. Ordering is
    // preserved: only uploads that a newer upload supersedes are dropped.
    std::unique_ptr<ThreadCommand> command;
    std::unique_ptr<ThreadCommand> newest_upload;
    while (m_main_thread_commands_queue.try_dequeue(command))
    {
        if (command->IsFrameUpload())
        {
            // Count what this supersedes. These are frames the core produced and
            // nobody ever saw, which is the one number that separates "the core is
            // slow" from "the room is too busy to show what the core produced".
            if (newest_upload)
                m_dropped_frames.fetch_add(1, std::memory_order_relaxed);
            newest_upload = std::move(command);
            continue;
        }
        if (newest_upload)
        {
            newest_upload->Execute();
            newest_upload.reset();
        }
        command->Execute();
    }
    if (newest_upload)
        newest_upload->Execute();

    auto input = godot::Input::get_singleton();

    // Only the instance the player is controlling should consume global input actions.
    if (!m_input_enabled)
        return;

    uint32_t joypad_buttons = 0;

    if (input->is_action_pressed("RETRO_JOYPAD_B"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_B);
    if (input->is_action_pressed("RETRO_JOYPAD_Y"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_Y);
    if (input->is_action_pressed("RETRO_JOYPAD_SELECT"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_SELECT);
    if (input->is_action_pressed("RETRO_JOYPAD_START"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_START);
    if (input->is_action_pressed("RETRO_JOYPAD_UP"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    if (input->is_action_pressed("RETRO_JOYPAD_DOWN"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (input->is_action_pressed("RETRO_JOYPAD_LEFT"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (input->is_action_pressed("RETRO_JOYPAD_RIGHT"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (input->is_action_pressed("RETRO_JOYPAD_A"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_A);
    if (input->is_action_pressed("RETRO_JOYPAD_X"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_X);
    if (input->is_action_pressed("RETRO_JOYPAD_L"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_L);
    if (input->is_action_pressed("RETRO_JOYPAD_R"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_R);
    if (input->is_action_pressed("RETRO_JOYPAD_L2"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_L2);
    if (input->is_action_pressed("RETRO_JOYPAD_R2"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_R2);
    if (input->is_action_pressed("RETRO_JOYPAD_L3"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_L3);
    if (input->is_action_pressed("RETRO_JOYPAD_R3"))
        joypad_buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_R3);

    m_input_handler->SetJoypadButtonStates(0, joypad_buttons);

    Vector2 analog_left = {};

    if (input->get_action_strength("RETRO_ANALOG_LEFT_X_NEGATIVE"))
        analog_left.x -= 1.0f;
    if (input->get_action_strength("RETRO_ANALOG_LEFT_X_POSITIVE"))
        analog_left.x += 1.0f;
    if (input->get_action_strength("RETRO_ANALOG_LEFT_Y_POSITIVE"))
        analog_left.y -= 1.0f;
    if (input->get_action_strength("RETRO_ANALOG_LEFT_Y_NEGATIVE"))
        analog_left.y += 1.0f;

    if (analog_left.length_squared() > 1.0f)
        analog_left = analog_left.normalized();

    m_input_handler->SetAnalogLeft(0, ToShort(analog_left.x) * 0x7fff, ToShort(analog_left.y) * 0x7fff);

    Vector2 analog_right = {};

    if (input->get_action_strength("RETRO_ANALOG_RIGHT_X_NEGATIVE"))
        analog_right.x -= 1.0f;
    if (input->get_action_strength("RETRO_ANALOG_RIGHT_X_POSITIVE"))
        analog_right.x += 1.0f;
    if (input->get_action_strength("RETRO_ANALOG_RIGHT_Y_POSITIVE"))
        analog_right.y -= 1.0f;
    if (input->get_action_strength("RETRO_ANALOG_RIGHT_Y_NEGATIVE"))
        analog_right.y += 1.0f;

    if (analog_right.length_squared() > 1.0f)
        analog_right = analog_right.normalized();

    m_input_handler->SetAnalogRight(0, ToShort(analog_right.x) * 0x7fff, ToShort(analog_right.y) * 0x7fff);
}

Wrapper::~Wrapper()
{
    // Destroying a joinable std::thread calls std::terminate, so always block here.
    StopEmulationThread(true);
}

void Wrapper::StopEmulationThread(bool blocking)
{
    if (!m_core)
    {
        return;
    }

    m_stop_requested = true;
    m_running = false;
    m_condition_variable.notify_all(); // wake emulation thread if blocked on InitAudio CV wait
    m_np_cv.notify_all();              // wake emulation thread if blocked on the netplay input gate
    // Same reason: an emulation thread parked on the link barrier is waiting
    // on a peer that is never going to publish again, and the join below would
    // block forever behind it.
    LinkCoordinator::Get().DropOwner(this);
    m_stopping = true;

    if (!blocking)
        return;   // _process() joins + finishes the teardown once the thread exits

    if (m_thread.joinable())
        m_thread.join();
    FinishTeardown();
}

void Wrapper::FinishTeardown()
{
    if (!m_core)
        return;   // already torn down
    m_stopping = false;
    m_thread_exited = false;

    // Before the core unloads: the memory regions point into its allocations, and
    // ReadMemory would follow them into freed pages. Hands the session back so the
    // next cabinet powered on can claim it.
    if (RetroAchievements* ra = RetroAchievements::GetSingleton())
        ra->ReleaseSession(this);

    // Set the thread-local pointer on the main thread so that handler DeInit
    // calls (which call GetCurrentThreadWrapper()) can find the right instance.
    SetCurrentThreadWrapper(this);
    m_video_handler->DeInit();
    m_audio_handler->DeInit();
    SetCurrentThreadWrapper(nullptr);

    ClearCoreIdentity();

    m_core->Unload();

    m_core = nullptr;
    m_trampolines = nullptr;
    m_environment_handler = nullptr;
    m_video_handler = nullptr;
    m_audio_handler = nullptr;
    m_input_handler = nullptr;
    m_options_handler = nullptr;
    m_message_handler = nullptr;
    m_log_handler = nullptr;

    // Discard commands the dying thread left queued; they must not execute
    // against the next content run's handlers.
    std::unique_ptr<ThreadCommand> stale;
    while (m_main_thread_commands_queue.try_dequeue(stale)) {}
    std::unique_ptr<EmuThreadCommand> stale_emu;
    while (m_emu_thread_commands_queue.try_dequeue(stale_emu)) {}
}

void Wrapper::EmulationThreadLoop()
{
    // Mark thread exit on EVERY path out of this function (including early
    // load-failure returns) so a deferred stop's join can never hang.
    struct ExitFlag
    {
        std::atomic<bool>& flag;
        ~ExitFlag() { flag.store(true, std::memory_order_release); }
    } exit_flag{m_thread_exited};

    t_current_wrapper = this;
    Log("Libretro Thread starting...");

    bool core_initialized = false;
    bool game_loaded = false;
    bool context_initialized = false;
    bool sram_active = false;
    ScopeExit lifecycle_cleanup([&]()
    {
        // Answer whatever is still queued before tearing anything down. Callers
        // of RequestSaveState/RequestLoadState/RequestDiskInfo are waiting on a
        // signal, and a core that failed to load — or one being stopped — leaves
        // no other thread that will ever reply to them.
        //
        // Clear the flags FIRST so nothing new is accepted, then drain. The gap
        // between a guard passing and the enqueue landing is not closed by this
        // and cannot be without a lock on the hot path; a caller still needs its
        // own timeout for that window.
        m_running = false;
        m_starting = false;
        {
            std::unique_ptr<EmuThreadCommand> orphan;
            while (m_emu_thread_commands_queue.try_dequeue(orphan))
                orphan->Abandon(*this);
        }

        // Every successful libretro lifecycle call has a matching teardown,
        // including failures before the frame loop begins.
        if (game_loaded)
        {
            if (sram_active)
                FlushSramIfDirty(true);
                FlushPackIfDirty(true);
            m_sram_pending = godot::PackedByteArray();
            if (context_initialized)
                m_video_handler->NotifyContextDestroy();
            m_core->retro_unload_game();
        }
        if (core_initialized)
            m_core->retro_deinit();

        t_current_wrapper = nullptr;
        Log("Libretro thread stopped.");
    });

    if (!m_core->Load(m_trampolines.get()))
    {
        NotifyContentLoadFailed("This core could not be loaded.");
        return;
    }
    core_initialized = true;

    // Before retro_load_game: options set while the core did not yet exist are
    // exactly the ones that decide how it loads.
    ApplyPendingCoreOptions();

    if (!m_subsystem_ident.empty())
    {
        // SET_SUBSYSTEM_INFO arrives during retro_set_environment, inside
        // Core::Load above, so the published table is already in hand here.
        const EnvironmentHandler::SubsystemInfo* subsystem = m_environment_handler->FindSubsystem(m_subsystem_ident);
        if (!subsystem)
        {
            LogError("Core does not publish subsystem '" + m_subsystem_ident + "'.");
            NotifyContentLoadFailed("This system cannot be started this way.");
            return;
        }

        // Resolved by name, so a core can publish a subsystem it did not export
        // an entry point for. Calling through the null pointer would be a crash
        // rather than a load failure.
        if (!m_core->retro_load_game_special)
        {
            LogError("Core publishes subsystem '" + m_subsystem_ident + "' but exports no retro_load_game_special.");
            NotifyContentLoadFailed("This system cannot be started this way.");
            return;
        }

        // A short list would leave a retro_game_info zeroed, and a core reading
        // it would fault. Hard stop rather than a best effort.
        if (m_subsystem_paths.size() != subsystem->roms.size())
        {
            LogError("Subsystem '" + m_subsystem_ident + "' expects " + std::to_string(subsystem->roms.size())
                     + " file(s) but " + std::to_string(m_subsystem_paths.size()) + " were given.");
            NotifyContentLoadFailed("The wrong number of files was given for this system.");
            return;
        }

        const size_t rom_count = m_subsystem_paths.size();

        // Sized ONCE, before a single data pointer is taken out of it. Growing
        // this later would invite a per-element resize after the game infos
        // already point into them.
        m_subsystem_buffers.clear();
        m_subsystem_buffers.resize(rom_count);

        std::vector<retro_game_info> game_infos(rom_count);

        // Shared across all the files rather than per file: the point of the cap
        // is the process memory budget, and N cartridges cost N times as much.
        constexpr size_t MAX_GAME_BUFFER_BYTES = 512ull * 1024ull * 1024ull;
        size_t total_buffered = 0;

        for (size_t i = 0; i < rom_count; ++i)
        {
            const std::string& path = m_subsystem_paths[i];
            const EnvironmentHandler::SubsystemRomInfo& rom = subsystem->roms[i];

            game_infos[i] = {};

            if (path.empty())
            {
                // An optional slot the player left empty is legal -- a console
                // with nothing in one of its bays. A required one is not.
                if (rom.required)
                {
                    LogError("Subsystem '" + m_subsystem_ident + "' rom " + std::to_string(i)
                             + " (" + rom.desc + ") is required but no file was given.");
                    NotifyContentLoadFailed("A required file for this system is missing.");
                    return;
                }
                continue;
            }

            if (!std::filesystem::is_regular_file(path))
            {
                LogError("Subsystem file not found (rom " + std::to_string(i) + ", " + rom.desc + "): " + path);
                NotifyContentLoadFailed("One of the files for this system is missing.");
                return;
            }

            // m_subsystem_paths is not touched again for the life of the run, so
            // this pointer stays valid as long as the core needs it.
            game_infos[i].path = path.c_str();
            game_infos[i].meta = nullptr;

            // PER ROM, not Core::GetNeedFullpath(). A 64DD run is exactly the
            // case that differs between the two.
            if (rom.need_fullpath)
            {
                Log("Subsystem rom " + std::to_string(i) + " (" + rom.desc + ") needs fullpath: " + path);
                continue;
            }

            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
            {
                LogError("Failed to open subsystem file: " + path);
                NotifyContentLoadFailed("One of the files for this system could not be opened.");
                return;
            }

            const size_t game_size = static_cast<size_t>(file.tellg());
            file.seekg(0, std::ios::beg);

            if (game_size > MAX_GAME_BUFFER_BYTES - total_buffered)
            {
                LogError("Subsystem content needs " + std::to_string((total_buffered + game_size) / (1024 * 1024))
                         + " MB in memory (limit " + std::to_string(MAX_GAME_BUFFER_BYTES / (1024 * 1024))
                         + " MB), at " + path);
                NotifyContentLoadFailed("These files are too large for this core to load.");
                return;
            }

            m_subsystem_buffers[i].resize(game_size);
            if (game_size != 0 && !file.read(reinterpret_cast<char*>(m_subsystem_buffers[i].data()), game_size))
            {
                LogError("Failed to read subsystem file: " + path);
                NotifyContentLoadFailed("One of the files for this system could not be read.");
                return;
            }

            total_buffered += game_size;
            // Taken after the only resize this element will ever see.
            game_infos[i].data = reinterpret_cast<const void*>(m_subsystem_buffers[i].data());
            game_infos[i].size = game_size;
        }

        Log("Loading subsystem '" + m_subsystem_ident + "' (id=" + std::to_string(subsystem->id) + ") with "
            + std::to_string(rom_count) + " file(s).");

        if (!m_core->retro_load_game_special(subsystem->id, game_infos.data(), rom_count))
        {
            LogError("retro_load_game_special failed for subsystem '" + m_subsystem_ident + "'.");
            NotifyContentLoadFailed("This core refused these files.");
            return;
        }
        game_loaded = true;
    }
    else if (m_game_path.empty())
    {
        // Kept as a floor. Measured 2026-08-20 over sixteen cores: not one stock
        // core starts without content, and six take the process down when asked
        // (mgba and parallel_n64 dereference a null game info; mednafen_saturn,
        // neocd, dolphin and same_cdi die on a zeroed one). A core reaching here
        // has to have said SET_SUPPORT_NO_GAME out loud, so a core that never
        // claimed it cannot be attempted by an over-eager BiosBoot row.
        if (!m_core->GetSupportsNoGame())
        {
            LogError("Game not set and this core does not support no game mode.");
            NotifyContentLoadFailed("This system cannot start without a game.");
            return;
        }

        // Which of the two conventions a core wants is not something it says out
        // loud, so it is measured per core and carried in the BiosBoot table. A
        // null pointer is libretro's own convention and what RetroArch passes,
        // which is what a core detecting no-content with `if (!info)` expects; a
        // zeroed struct is the one that does not fault on a core that reads the
        // argument without checking.
        const bool pass_null = GetNoContentPassesNull();
        Log(std::string("Starting with no content, passing ") +
            (pass_null ? "a null game info" : "a zeroed game info") + ".");

        retro_game_info game_info = {};

        if (!m_core->retro_load_game(pass_null ? nullptr : &game_info))
        {
            LogError("Failed to load game");
            NotifyContentLoadFailed("This system cannot start without a game.");
            return;
        }
        game_loaded = true;
    }
    else
    {
        if (!std::filesystem::is_regular_file(m_game_path))
        {
            LogError("Game not found: " + m_game_path);
            NotifyContentLoadFailed("The game file is missing.");
            return;
        }

        retro_game_info game_info = {};
        game_info.path = m_game_path.c_str();
        game_info.meta = nullptr;

        if (m_core->GetNeedFullpath())
        {
            // Disc cores (PS1/PS2/Saturn/Dreamcast/GC/PSP, MAME, ...) open the
            // image themselves from the path so they can seek, read tracks lazily
            // and hot-swap discs; they never touch game_info.data. Reading a 4 GB
            // ISO into a buffer nobody reads is an instant OOM kill on Quest.
            Log("Core needs fullpath, passing path only: " + m_game_path);
        }
        else
        {
            std::ifstream file(m_game_path, std::ios::binary | std::ios::ate);
            if (!file)
            {
                LogError("Failed to open game file: " + m_game_path);
                NotifyContentLoadFailed("The game file could not be opened.");
                return;
            }

            size_t game_size = static_cast<size_t>(file.tellg());
            file.seekg(0, std::ios::beg);

            // A core that wants the bytes in memory is a cartridge core; those top
            // out well under this. Anything larger is a mismatch (or a corrupt
            // download) and would blow the app's memory budget, so fail loudly
            // instead of attempting the allocation.
            constexpr size_t MAX_GAME_BUFFER_BYTES = 512ull * 1024ull * 1024ull;
            if (game_size > MAX_GAME_BUFFER_BYTES)
            {
                LogError("Game is " + std::to_string(game_size / (1024 * 1024)) +
                         " MB but this core requires the whole file in memory (limit " +
                         std::to_string(MAX_GAME_BUFFER_BYTES / (1024 * 1024)) + " MB): " + m_game_path);
                NotifyContentLoadFailed("This game is too large for this core to load.");
                return;
            }

            m_game_buffer.resize(game_size);
            if (!file.read(reinterpret_cast<char*>(m_game_buffer.data()), game_size))
            {
                LogError("Failed to read game file: " + m_game_path);
                NotifyContentLoadFailed("The game file could not be read.");
                return;
            }

            game_info.data = reinterpret_cast<const void*>(m_game_buffer.data());
            game_info.size = game_size;
        }

        if (!m_core->retro_load_game(&game_info))
        {
            LogError("Failed to load game");
            NotifyContentLoadFailed("This core refused the game.");
            return;
        }
        game_loaded = true;
    }

    // Apply device selections made before the core started (controller/mouse
    // plugged in while the system was off). retro_set_controller_port_device is
    // canonical right after retro_load_game; default joypad ports are skipped.
    {
        std::lock_guard<std::mutex> lock(m_port_device_mutex);
        for (const auto& [port, device] : m_pending_port_devices)
        {
            // Skip core defaults: plain joypad is what cores assume, and NONE
            // (recorded by an unplug while off) must not kill the port's
            // fallback global-input path. Subclassed devices (multitap 257,
            // mouse 2, lightgun…) pass through.
            if (device == RETRO_DEVICE_JOYPAD || device == RETRO_DEVICE_NONE)
                continue;
            Log("Applying pre-start port device: port=" + std::to_string(port)
                + " device=" + std::to_string(device));
            m_input_handler->SetPortDevice(port, device);
            m_core->retro_set_controller_port_device(port, device);
        }
    }

    m_system_av_info = {};
    m_core->retro_get_system_av_info(&m_system_av_info);

    Log("FPS: " + std::to_string(m_system_av_info.timing.fps) + " Sample Rate: " + std::to_string(m_system_av_info.timing.sample_rate));

    // Size the hardware render target by the core's BASE geometry, not its max.
    //
    // For a core that renders through a surface (Dolphin), the surface IS the
    // swapchain, and Vulkan requires a swapchain's extent to equal the surface's
    // currentExtent. An oversized surface therefore does not leave the core room
    // to grow; it forces the core to render bigger and present scaled into it
    // while video_refresh still reports the base size, so the readback takes
    // base rows off the top of a taller picture.
    //
    // SET_GEOMETRY cannot repair this: it is specified to complete in constant
    // time and to perform no video reinitialization, so it may not rebuild a
    // surface. A core that genuinely needs to grow past base has to say so
    // through SET_SYSTEM_AV_INFO, which is the call that does permit
    // reinitialization.
    const int32_t hw_width = static_cast<int32_t>(m_system_av_info.geometry.base_width);
    const int32_t hw_height = static_cast<int32_t>(m_system_av_info.geometry.base_height);

    if (!m_video_handler->InitHwRenderContext(hw_width, hw_height))
    {
        LogError("Failed to initialize video");
        return;
    }
    context_initialized = true;

    if (m_stop_requested)
        return;

    // Running before not-starting, never the other way round: a request landing
    // between the two must see one of them true or it is refused.
    m_running = true;
    m_starting = false;

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_mutex_done = false;
        m_main_thread_commands_queue.enqueue(std::make_unique<ThreadCommandInitAudio>(this, 0.1f, m_system_av_info.timing.sample_rate));
        m_condition_variable.wait(lock, [&]{ return m_mutex_done || m_stop_requested.load(); });
        if (m_stop_requested)
            return;
    }

    // Achievements. Deferred to here rather than straight after retro_load_game so
    // the core is fully initialized: the memory map arrives during load, and a
    // core may still be registering descriptors while av_info is being read.
    // Cartridge cores hand over the bytes already resident in m_game_buffer;
    // need_fullpath cores never read the file, so rc_hash opens the path itself.
    if (RetroAchievements* ra = RetroAchievements::GetSingleton())
    {
        // rc_hash takes one file. A subsystem run has several, and the identity
        // of the run -- what saves, states and netplay already key off -- is
        // m_game_path, so that is the one to hash. The bytes are deliberately
        // not passed: on a subsystem run m_game_buffer is empty by construction
        // (the images live in m_subsystem_buffers), and rc_hash opens the path
        // itself, which is the same arrangement disc cores already use.
        const bool subsystem_run = !m_subsystem_ident.empty();
        if (ra->HoldsSession(this))
            ra->BeginLoadGame(this, m_game_path,
                !subsystem_run && !m_core->GetNeedFullpath() && !m_game_buffer.empty() ? m_game_buffer.data() : nullptr,
                !subsystem_run && !m_core->GetNeedFullpath() ? m_game_buffer.size() : 0);
    }

    double frame_duration_ms = 1000.0 / m_system_av_info.timing.fps;
    m_timing_revision.store(0, std::memory_order_relaxed);
    uint64_t timing_revision = 0;

    // Publish what the core declared, from the one read the pacing loop itself
    // trusts. A later read is not the same value: a core may revise timing behind
    // the frontend's back, and the HUD compares measured against DECLARED, so it
    // has to be the figure this loop is actually pacing to.
    m_declared_fps.store(m_system_av_info.timing.fps, std::memory_order_relaxed);
    m_declared_sample_rate.store(m_system_av_info.timing.sample_rate, std::memory_order_relaxed);
    m_dropped_frames.store(0, std::memory_order_relaxed);

    // Name, version and API only. All three were read at load from
    // retro_get_system_info and cost no call into the core here, so this is
    // safe for every core AND available before a single frame has run - which
    // netplay requires, because its gate will not run frame 0 until every peer
    // has reported ready. The savestate size is the dangerous one and follows
    // the first frame instead.
    PublishCoreIdentity();

    auto last_time = std::chrono::steady_clock::now();
    double accumulator = 0.0;

    // Slack left before the next frame is due. sleep_for rounds up to the OS
    // timer granularity (coarse on Windows without timeBeginPeriod), so waking
    // early and taking the last stretch on the clock beats oversleeping frames.
    constexpr double SLEEP_MARGIN_MS = 1.5;

    // Pacing has two parts, and the declared fps is only one of them.
    //
    // The brake is the audio sink. Its mixer drains at the hardware's rate, so how
    // full it is measures real time, the one quantity in this loop a core cannot
    // misreport. Run a frame while the sink still wants audio; wait when it does
    // not. This needs neither a correct fps nor an honest sample count, and cores
    // falsify both: azahar returns when the *game* presents, so its declared 60
    // describes neither the call rate nor the time a call covers, and ScummVM can
    // bill 2.5 million ms of audio for a single retro_run.
    //
    // The declared fps is a ceiling on top of that, never a target: it caps how
    // fast the loop may run, and is the only brake left for a core that emits no
    // audio for the sink to measure.

    // When the next call may go out under the ceiling. Advanced by one frame per
    // call rather than reset to now, so a call that overruns is not paid for twice.
    auto next_call_due = std::chrono::steady_clock::now();

    // One wait is bounded: past this the frame runs anyway and samples drop.
    constexpr double MAX_BRAKE_WAIT_MS = 250.0;

    // Bounding one wait does not bound the loop of them: a sink nothing is
    // draining reports "still full" on every pass, and the loop would brake
    // forever without ever calling the core. Bound the silence instead. A sink
    // that has not asked for audio for longer than it could possibly hold (the
    // voice ring is 32768 frames, ~0.68 s at 48 kHz) is not draining and is
    // therefore measuring nothing, so ignore it and let the fps ceiling pace.
    // The brake resumes the moment the sink asks for a frame again.
    constexpr double SINK_SILENCE_LIMIT_MS = 1000.0;
    auto last_sink_want = std::chrono::steady_clock::now();

    // Battery save: fill SAVE_RAM from the cartridge/memory-card .srm (or the
    // netplay-injected bytes) before the first frame runs.
    LoadSramFromSource();
    sram_active = true;
    m_sram_flush_counter = m_frame_counter.load(std::memory_order_relaxed);

    // Writable content (a BS-X memory pack): the core already holds the bytes
    // the .bs was loaded with, so this only records them for the dirty check.
    SnapshotPack();

    // Fresh rollback bookkeeping for this content run (these members are
    // emulation-thread-only, so reset them here rather than in SetNetplay*).
    m_np_states.clear();
    m_np_used.clear();
    m_np_crc_pending.clear();
    m_np_watermark = m_frame_counter.load(std::memory_order_relaxed) - 1;
    m_np_verified = m_np_watermark;
    m_np_replaying = false;
    m_np_replay_mute_video = false;
    m_np_rollback_count.store(0, std::memory_order_relaxed);

    if (Libretro* node = LiveLibretroNode())
        node->NotifyOptionsReady();

    while (m_running)
    {
        if (!m_running)
            break;

        // Drain main→emu commands (savestates, etc.) strictly between frames
        // so they never re-enter the core mid-retro_run.
        {
            std::unique_ptr<EmuThreadCommand> emu_command;
            while (m_emu_thread_commands_queue.try_dequeue(emu_command))
                emu_command->Execute(*this);
        }

        const uint64_t current_timing_revision =
            m_timing_revision.load(std::memory_order_acquire);
        if (current_timing_revision != timing_revision)
        {
            timing_revision = current_timing_revision;
            frame_duration_ms = 1000.0 /
                m_declared_fps.load(std::memory_order_relaxed);
            accumulator = std::min(accumulator, frame_duration_ms * 4.0);
            const auto timing_now = std::chrono::steady_clock::now();
            last_time = timing_now;
            next_call_due = timing_now +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double, std::milli>(frame_duration_ms));
        }

        // The savestate size, once the core has actually produced a frame.
        // Here rather than beside a retro_run because the rollback path
        // continues past both of those, and it would never be measured at all
        // in the one mode that takes a state every single frame.
        //
        // Gated on THIS content having run a frame, not on a non-zero frame
        // counter: nothing resets that counter, so a restart on a reused Wrapper
        // carried the previous run's value in and fired the measurement before
        // the new core had run anything -- which is a segfault in Dolphin, not a
        // wrong number. See m_core_ran_frame.
        if (!m_core_serialize_size_published && m_core_ran_frame)
            PublishCoreSerializeSize();

        // Battery save: dirty-check flush roughly every 10 seconds of frames.
        //
        // The counter goes BACKWARDS: loading a savestate stores the state's own
        // frame, and netplay rollback rewinds on every correction. A plain
        // (fc - last >= 600) then reads hundreds of thousands of frames negative
        // and never fires again for the rest of the run — the battery save
        // silently stops being written. Restart the window at the rewound frame
        // instead of flushing, so a rollback that corrects every frame does not
        // pay for a dirty check every frame.
        {
            int64_t fc = m_frame_counter.load(std::memory_order_relaxed);
            if (fc < m_sram_flush_counter)
            {
                m_sram_flush_counter = fc;
            }
            else if (fc - m_sram_flush_counter >= 600)
            {
                m_sram_flush_counter = fc;
                FlushSramIfDirty();
                FlushPackIfDirty();
            }
        }

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(now - last_time).count();
        last_time = now;
        accumulator += elapsed;

        if (!m_netplay_enabled.load(std::memory_order_acquire))
        {
            const auto frame_dur = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double, std::milli>(frame_duration_ms));

            // Ceiling. Never call the core faster than it asked to be called.
            const double until_due =
                std::chrono::duration<double, std::milli>(next_call_due - now).count();
            if (until_due > SLEEP_MARGIN_MS)
            {
                std::this_thread::sleep_for(
                    std::chrono::duration<double, std::milli>(until_due - SLEEP_MARGIN_MS));
                continue;
            }

            // Brake. The sink drains at the mixer's rate, so waiting for it to want
            // audio is waiting on real time, whatever the core claims about either
            // its frame rate or its sample count.
            double brake_ms = m_audio_handler->MsUntilSinkWantsFrames();
            if (brake_ms > MAX_BRAKE_WAIT_MS)
                brake_ms = MAX_BRAKE_WAIT_MS;
            m_audio_handler->SetLastBrakeMs(brake_ms);
            if (brake_ms <= SLEEP_MARGIN_MS)
            {
                // The sink wants audio: it is draining, so it is still a clock.
                last_sink_want = now;
            }
            else if (std::chrono::duration<double, std::milli>(now - last_sink_want).count()
                     < SINK_SILENCE_LIMIT_MS)
            {
                std::this_thread::sleep_for(
                    std::chrono::duration<double, std::milli>(brake_ms - SLEEP_MARGIN_MS));
                continue;
            }

            m_audio_handler->CallAudioBufferStatusCallback();

            m_core->retro_run();
            m_core_ran_frame = true;
            m_frame_counter.fetch_add(1, std::memory_order_relaxed);

            // Achievements. Emulation thread, strictly after the frame the core
            // just produced, and only ever on a frame that is final; the rollback
            // paths deliberately do not call this (see NetplayRollbackIteration).
            if (RetroAchievements* ra = RetroAchievements::GetSingleton())
                ra->DoFrame(this);

            // Charge the ceiling one frame. A call that overran its slot must not
            // earn catch-up calls, so the arrears are floored a few frames back
            // rather than replayed as fast-forward.
            next_call_due += frame_dur;
            const auto arrears_floor = std::chrono::steady_clock::now() - frame_dur * 4;
            if (next_call_due < arrears_floor)
                next_call_due = arrears_floor;
            continue;
        }

        // ── Netplay rollback: run ahead on prediction, rewind on correction ──
        if (m_np_rollback.load(std::memory_order_acquire))
        {
            NetplayRollbackIteration(frame_duration_ms, accumulator);
            continue;
        }

        // ── Netplay lockstep: run frame N only once its inputs arrived ──────
        if (accumulator < frame_duration_ms)
            continue;
        // Cap catch-up debt after stalls to a few frames.
        if (accumulator > frame_duration_ms * 4.0)
            accumulator = frame_duration_ms * 4.0;

        NpFrame frame_inputs{};
        {
            std::unique_lock<std::mutex> lock(m_np_mutex);
            int64_t frame = m_frame_counter.load(std::memory_order_relaxed);
            bool ready = m_np_cv.wait_for(lock, std::chrono::milliseconds(4), [&]
                { return m_stop_requested.load() || m_np_inputs.count(frame) > 0; });
            if (m_stop_requested)
                break;
            if (!ready)
                continue;   // stall; loop again (drains commands, re-checks stop)
            frame_inputs = m_np_inputs[frame];
            m_np_inputs.erase(m_np_inputs.begin(), m_np_inputs.lower_bound(frame - 30));
        }

        // Netplay-scheduled disc ops, resets and link changes land strictly
        // before their frame runs, so every peer changes deterministic state on
        // the identical boundary.
        ApplyScheduledDiscOps(m_frame_counter.load(std::memory_order_relaxed));
        ApplyScheduledResets(m_frame_counter.load(std::memory_order_relaxed));
        ApplyScheduledLinkOps(m_frame_counter.load(std::memory_order_relaxed));

        // Apply the agreed inputs: in netplay mode the emulation thread is
        // the sole InputHandler writer for the masked ports. Device-aware
        // (mouse ports get deltas), plus the aux block (sensor/touch).
        ApplyNetplayInputs(frame_inputs, m_np_port_mask.load(std::memory_order_relaxed));
        ApplyNetplayAux(frame_inputs);

        m_audio_handler->CallAudioBufferStatusCallback();
        m_core->retro_run();
        m_core_ran_frame = true;
        int64_t frame_done = m_frame_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        accumulator -= frame_duration_ms;

        // Lockstep netplay frames are final (every peer has confirmed the inputs
        // before the frame runs), so they count, unlike rollback speculation.
        if (RetroAchievements* ra = RetroAchievements::GetSingleton())
            ra->DoFrame(this);

        if (m_np_crc_interval > 0 && frame_done % m_np_crc_interval == 0)
            EmitNetplayCrc(frame_done);
    }
}

void Wrapper::CreateTexture(Image::Format image_format, PackedByteArray pixel_data, int32_t width, int32_t height, bool flip_y)
{
    m_video_handler->SetImageFormat(image_format);
    m_main_thread_commands_queue.enqueue(std::make_unique<ThreadCommandCreateTexture>(this, image_format, pixel_data, width, height, flip_y));
}

void Wrapper::UpdateTexture(PackedByteArray pixel_data, int32_t width, int32_t height, bool flip_y)
{
    m_main_thread_commands_queue.enqueue(std::make_unique<ThreadCommandUpdateTexture>(this, pixel_data, width, height, flip_y));
}

bool Wrapper::Shutdown()
{
    // RETRO_ENVIRONMENT_SHUTDOWN arrives ON the emulation thread (inside
    // retro_run), and a blocking stop would join the thread from itself. Signal
    // only; _process() finishes the teardown after this thread exits.
    Log("Shutting down from core...");
    StopEmulationThread(false);
    return true;
}

void Wrapper::LedInterfaceSetLedState(int32_t led, int32_t state)
{
    // Reported by the core from the emulated machine's own register, not
    // inferred: the Satellaview's ACCESS lamp is $2194 bit 2. Edges only, so
    // this is not a per-frame signal.
    // Static libretro callback: the owning Wrapper comes from the thread-local,
    // never from a global -- two Satellaviews in one room are two instances.
    Wrapper* w = GetCurrentThreadWrapper();
    if (w == nullptr)
        return;
    if (Libretro* node = w->LiveLibretroNode())
        node->NotifyLedState(led, state != 0);
}
}
