#include "Wrapper.hpp"

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
#include <atomic>
#include <algorithm>
#include <cstring>

#include "Libretro.hpp"
#include "Debug.hpp"
#include "ThreadCommandInitAudio.hpp"
#include "ThreadCommandCreateTexture.hpp"
#include "ThreadCommandUpdateTexture.hpp"
#include "ThreadCommandEmitSignal.hpp"

using namespace godot;

namespace Xenu
{
thread_local Wrapper* t_current_wrapper = nullptr;

// Fallback for callbacks from threads spawned by cores (e.g. Dolphin's audio
// thread).  When the thread-local is null, we try this atomic pointer instead.
// This works correctly for single-instance and is best-effort for multi-instance.
static std::atomic<Wrapper*> s_fallback_wrapper{ nullptr };

Wrapper* Wrapper::GetCurrentThreadWrapper()
{
    Wrapper* w = t_current_wrapper;
    if (!w)
        w = s_fallback_wrapper.load(std::memory_order_acquire);
    return w;
}

void Wrapper::SetCurrentThreadWrapper(Wrapper* wrapper)
{
    t_current_wrapper = wrapper;
    if (wrapper)
        s_fallback_wrapper.store(wrapper, std::memory_order_release);
}

static retro_key GodotToLibretroKeycode(const Ref<InputEventKey>& keyEvent)
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
// "<name>_libretro_android.so", but not universally — azahar's CMake never set
// an Android OUTPUT_NAME, so the buildbot ships it as "azahar_libretro.so".
// Try every convention the platform can use and take the first that exists;
// with none present, return the canonical name so the error names that file.
static std::filesystem::path ResolveCorePath(const std::string& root_directory, const std::string& core_name)
{
    const std::filesystem::path cores_dir = std::filesystem::path(root_directory).append("cores");

    static const char* const suffixes[] = {
#ifdef __ANDROID__
        "_libretro_android.so", "_libretro.so",
#elif defined(__linux__)
        "_libretro.so",
#else
        "_libretro.dll",
#endif
    };

    for (const char* suffix : suffixes)
    {
        std::filesystem::path candidate = cores_dir / (core_name + suffix);
        if (std::filesystem::is_regular_file(candidate))
            return candidate;
    }
    return cores_dir / (core_name + suffixes[0]);
}

void Wrapper::StartContent(MeshInstance3D* node, const std::string& root_directory, const std::string& core_name, const std::string& game_path)
{
    // node may be null: a console powered on with no TV connected still runs; the
    // core renders to its texture with no bound mesh until SetScreenMesh() attaches
    // one (e.g. when a TV is plugged in). VideoHandler::Init tolerates a null mesh.

    StopEmulationThread();

    m_node = node;
    m_node_id = node ? node->get_instance_id() : 0;

    auto audio_stream_player = memnew(AudioStreamPlayer3D);
    audio_stream_player->set_name("AudioStreamPlayer3D");
    // Configure spatial audio for better immersion
    audio_stream_player->set_attenuation_model(AudioStreamPlayer3D::ATTENUATION_INVERSE_DISTANCE);
    audio_stream_player->set_panning_strength(1.0f);
    audio_stream_player->set_max_db(0.0f);
    m_libretro_node->add_child(audio_stream_player);

    std::filesystem::path core_path = ResolveCorePath(root_directory, core_name);

    m_core = std::make_unique<Core>(core_path.string());
    m_trampolines = std::make_unique<CallbackTrampolines>(this);
    m_environment_handler = std::make_unique<EnvironmentHandler>();
    m_video_handler = std::make_unique<VideoHandler>();
    m_audio_handler = std::make_unique<AudioHandler>();
    m_input_handler = std::make_unique<InputHandler>();
    m_options_handler = std::make_unique<OptionsHandler>();
    m_message_handler = std::make_unique<MessageHandler>();
    m_log_handler = std::make_unique<LogHandler>();

    m_video_handler->Init(node);

    m_root_directory = root_directory;
    m_temp_directory = std::filesystem::path(root_directory).append("temp").string();
    m_game_path = game_path;
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

    m_stop_requested = false;
    m_thread_exited = false;
    m_thread = std::thread(&Wrapper::EmulationThreadLoop, this);
}

void Wrapper::StopContent()
{
    // Silence output immediately (main-thread entry point, so touching the
    // AudioStreamPlayer3D is safe here): a core whose teardown hangs (mupen
    // gles3) can keep firing audio callbacks from its internal threads long
    // after the stop — the player must not keep voicing them.
    if (m_audio_handler)
        m_audio_handler->SetPlaying(false);
    // Non-blocking: the join + teardown (SRAM flush, retro_unload_game,
    // retro_deinit, DLL unload) all happen off the main thread / deferred to
    // _process, so powering a system off no longer hitches the frame.
    StopEmulationThread(false);
}

void Wrapper::ShutdownForExit()
{
    if (m_audio_handler)
        m_audio_handler->SetPlaying(false);
    StopEmulationThread(true);
}

MeshInstance3D* Wrapper::LiveNode() const
{
    if (m_node_id == 0)
        return nullptr;
    return Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(m_node_id));
}

void Wrapper::SetScreenMesh(MeshInstance3D* new_mesh)
{
    if (!m_video_handler)
        return;
    m_video_handler->SetMesh(LiveNode(), new_mesh);
    m_node = new_mesh;
    m_node_id = new_mesh ? new_mesh->get_instance_id() : 0;
    if (m_audio_handler)
        m_audio_handler->SetPlaying(new_mesh != nullptr);
}

godot::Array Wrapper::GetControllerInfo() const
{
    Array result;
    if (!m_input_handler)
        return result;

    const auto& controllers = m_input_handler->GetControllers();
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
    // Always remember the selection — a controller/mouse plugged in while the
    // system is OFF has no core yet; the emulation thread applies this map
    // right after retro_load_game so the core polls the right device.
    {
        std::lock_guard<std::mutex> lock(m_port_device_mutex);
        m_pending_port_devices[port] = device;
    }
    if (!m_core || !m_input_handler)
    {
        Log("SetControllerPortDevice: no core running — port=" + std::to_string(port)
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
    if (m_input_handler)
        m_input_handler->SetLightgunPosition(port, x, y);
}

void Wrapper::SetLightgunIsOffscreen(uint32_t port, bool offscreen)
{
    if (m_input_handler)
        m_input_handler->SetLightgunIsOffscreen(port, offscreen ? 1 : 0);
}

void Wrapper::SetLightgunButton(uint32_t port, int button_id, bool pressed)
{
    if (!m_input_handler)
        return;
    uint32_t buttons = m_input_handler->GetLightgunButtons(port);
    if (pressed)
        buttons |= (1u << button_id);
    else
        buttons &= ~(1u << button_id);
    m_input_handler->SetLightgunButtons(port, buttons);
}

void Wrapper::SetJoypadState(uint32_t port, uint16_t button_mask, int16_t analog_lx, int16_t analog_ly, int16_t analog_rx, int16_t analog_ry)
{
    if (!m_input_handler)
        return;
    // In netplay mode the emulation thread applies the agreed per-frame inputs
    // for the masked ports — live main-thread writes would desync peers.
    if (m_netplay_enabled.load(std::memory_order_acquire) &&
        (m_np_port_mask.load(std::memory_order_relaxed) & (1u << port)))
    {
        // Rollback: locally-owned masked ports feed the live-input slot that
        // the emulation thread samples each frame (zero added delay). Remote
        // masked ports stay blocked (prediction + confirmations own them).
        if (m_np_rollback.load(std::memory_order_acquire) &&
            (m_np_local_mask.load(std::memory_order_relaxed) & (1u << port)) && port < 4)
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

void Wrapper::SetMouseState(uint32_t port, int32_t dx, int32_t dy, uint32_t buttons)
{
    if (!m_input_handler)
        return;
    // Mouse input is not part of the deterministic netplay input schedule
    // (same policy as sensors/pointer) — block writes to masked ports so
    // peers can't diverge on it.
    if (m_netplay_enabled.load(std::memory_order_acquire) &&
        (m_np_port_mask.load(std::memory_order_relaxed) & (1u << port)))
        return;
    auto clamp16 = [](int32_t v) -> int16_t
    {
        return static_cast<int16_t>(v < -0x7fff ? -0x7fff : (v > 0x7fff ? 0x7fff : v));
    };
    m_input_handler->SetMousePosition(port, clamp16(dx), clamp16(dy));
    m_input_handler->SetMouseButtons(port, buttons);
}

void Wrapper::SetKeyState(uint32_t port, uint32_t keycode, bool down, uint32_t character)
{
    if (!m_input_handler)
        return;
    // During netplay, keyboard input rides the deterministic frame schedule
    // (key-event slots) — live writes would desync peers. Keyboard state is
    // global (port 0), so gate on port 0 being masked.
    if (m_netplay_enabled.load(std::memory_order_acquire) &&
        (m_np_port_mask.load(std::memory_order_relaxed) & 1u))
        return;
    m_input_handler->SetKeyState(port, keycode, down);
    m_input_handler->CallKeyboardEventCallback(down, keycode, character,
        m_input_handler->GetKeyModifiers(port));
}

void Wrapper::SetSensorAccel(uint32_t port, float x, float y, float z)
{
    if (m_input_handler)
        m_input_handler->SetSensorAccel(port, x, y, z);
}

void Wrapper::SetPointerState(uint32_t port, int16_t x, int16_t y, bool pressed)
{
    if (m_input_handler)
    {
        m_input_handler->SetPointerPosition(port, x, y);
        m_input_handler->SetPointerPressed(port, pressed ? 1 : 0);
        m_input_handler->SetPointerCount(port, pressed ? 1 : 0);
    }
}

// ── Netplay (deterministic lockstep) ─────────────────────────────────────────

void Wrapper::SetNetplayMode(bool enabled, uint32_t port_mask, int64_t start_frame)
{
    {
        std::lock_guard<std::mutex> lock(m_np_mutex);
        m_np_inputs.clear();
        m_disc_schedule.clear();
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
        // Lockstep: inputs for already-run frames are stale. Rollback: they are
        // CONFIRMATIONS for speculatively-run frames — exactly the point.
        if (!rollback && frame < m_frame_counter.load(std::memory_order_relaxed))
            return;
        if (m_np_inputs.size() > 600)
            return;                     // runaway guard (~10 s of frames)
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
    }
    m_np_local_mask.store(local_mask, std::memory_order_relaxed);
    m_np_max_ahead = std::clamp(max_ahead, 2, 24);
    m_np_rollback.store(enabled, std::memory_order_release);
    m_np_cv.notify_all();
    Log("Netplay rollback " + std::string(enabled ? "ON" : "OFF") +
        " local_mask=" + std::to_string(local_mask) +
        " max_ahead=" + std::to_string(max_ahead));
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

void Wrapper::RequestSaveState()
{
    if (!m_core || !m_running)
    {
        if (m_libretro_node)
            m_libretro_node->call_deferred("emit_signal", "savestate_ready",
                godot::PackedByteArray(), static_cast<int64_t>(-1));
        return;
    }
    m_emu_thread_commands_queue.enqueue(std::make_unique<EmuThreadCommandSaveState>());
}

void Wrapper::RequestLoadState(const godot::PackedByteArray& data, int64_t frame)
{
    if (!m_core || !m_running)
    {
        if (m_libretro_node)
            m_libretro_node->call_deferred("emit_signal", "savestate_loaded", false);
        return;
    }
    m_emu_thread_commands_queue.enqueue(std::make_unique<EmuThreadCommandLoadState>(data, frame));
}

// ── Disk control (multi-disc games) ──────────────────────────────────────────

void Wrapper::RequestDiskInfo()
{
    if (!m_core || !m_running)
    {
        if (m_libretro_node)
            m_libretro_node->call_deferred("emit_signal", "disk_control_ready",
                false, static_cast<int64_t>(0), static_cast<int64_t>(0), false);
        return;
    }
    m_emu_thread_commands_queue.enqueue(std::make_unique<EmuThreadCommandDiskInfo>());
}

void Wrapper::SetDiskEjectState(bool ejected)
{
    if (!m_core || !m_running)
        return;
    m_emu_thread_commands_queue.enqueue(std::make_unique<EmuThreadCommandSetDiskEjected>(ejected));
}

void Wrapper::ReplaceDiskImage(uint32_t index, const godot::String& path)
{
    if (!m_core || !m_running)
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
    m_disc_schedule[frame] = DiscOp{ op, index, std::string(path.utf8().get_data()) };
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
/// arrived — strictly before running `frame`, so every lockstep peer swaps
/// on the identical frame.
void Wrapper::ApplyScheduledDiscOps(int64_t frame)
{
    DiscOp op;
    bool pending = false;
    {
        std::lock_guard<std::mutex> lock(m_np_mutex);
        auto it = m_disc_schedule.begin();
        while (it != m_disc_schedule.end() && it->first <= frame)
        {
            op = it->second;
            pending = true;
            it = m_disc_schedule.erase(it);
            // Apply at most one op per frame boundary; ops land on distinct
            // frames in practice (eject and replace are separate schedules).
            break;
        }
    }
    if (!pending || !m_environment_handler)
        return;
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
    EmitDiskInfo();
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

void Wrapper::RequestSramFlush()
{
    if (m_core && m_running)
        m_emu_thread_commands_queue.enqueue(std::make_unique<EmuThreadCommandFlushSram>());
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

    m_sram_shadow.assign(static_cast<uint8_t*>(sram), static_cast<uint8_t*>(sram) + size);
}

/// Emu thread: write SAVE_RAM to the backing file iff it changed since the
/// last flush. Never deletes or truncates an existing file to nothing.
void Wrapper::FlushSramIfDirty()
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
    m_sram_shadow.assign(static_cast<uint8_t*>(sram), static_cast<uint8_t*>(sram) + size);
    Log("SRAM: flushed " + std::to_string(size) + " bytes to " + m_sram_path);
}

/// Emu thread: memory-card hot-swap — flush the old card, adopt the new one.
void Wrapper::ApplySramSwap(const std::string& new_path)
{
    FlushSramIfDirty();
    m_sram_path = new_path;
    m_sram_pending = godot::PackedByteArray();
    LoadSramFromSource();
    Log("SRAM: swapped to " + (new_path.empty() ? std::string("<none>") : new_path));
}

void Wrapper::EmitSignalOnMainThread(const godot::StringName& signal_name, const godot::Array& args)
{
    m_main_thread_commands_queue.enqueue(
        std::make_unique<ThreadCommandEmitSignal>(this, signal_name, args));
}

// Self-contained CRC32 (polynomial 0xEDB88320) — libretro-common's crc32.c is
// not part of this build, and the table init must be thread-safe (multiple
// emulation threads may race the first call).
static uint32_t Crc32(const uint8_t* data, size_t size)
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
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

uint32_t Wrapper::ComputeRamCrc(bool& ok) const
{
    ok = false;
    if (!m_core || !m_core->retro_get_memory_data || !m_core->retro_get_memory_size)
        return 0;
    void* ram = m_core->retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t size = m_core->retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    if (ram == nullptr || size == 0)
        return 0;
    ok = true;
    return Crc32(static_cast<const uint8_t*>(ram), size);
}

void Wrapper::EmitNetplayCrc(int64_t frame)
{
    bool ok = false;
    uint32_t crc = ComputeRamCrc(ok);
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
        // frame's delta — deterministic under both lockstep and rollback).
        uint32_t device = m_input_handler->GetPortDevice(port) & RETRO_DEVICE_MASK;
        if (device == RETRO_DEVICE_MOUSE)
        {
            m_input_handler->SetMousePosition(port, static_cast<int16_t>(v[1]), static_cast<int16_t>(v[2]));
            m_input_handler->SetMouseButtons(port, static_cast<uint32_t>(v[0]));
            continue;
        }
        m_input_handler->SetJoypadButtonStates(port, static_cast<uint16_t>(v[0]));
        m_input_handler->SetAnalogLeft(port, static_cast<int16_t>(v[1]), static_cast<int16_t>(v[2]));
        m_input_handler->SetAnalogRight(port, static_cast<int16_t>(v[3]), static_cast<int16_t>(v[4]));
    }
}

/// Apply the aux block of a netplay frame (sensor + pointer, port 0). Runs on
/// the emulation thread strictly before the frame executes, so tilt and touch
/// are part of the deterministic timeline on every peer.
void Wrapper::ApplyNetplayAux(const NpFrame& inputs)
{
    const int32_t flags = inputs[20];
    if (flags & 1)
        m_input_handler->SetSensorAccel(0,
            inputs[21] / 1000.0f, inputs[22] / 1000.0f, inputs[23] / 1000.0f);
    if (flags & 2)
    {
        m_input_handler->SetPointerPosition(0,
            static_cast<int16_t>(inputs[24]), static_cast<int16_t>(inputs[25]));
        m_input_handler->SetPointerPressed(0, inputs[26] ? 1 : 0);
        m_input_handler->SetPointerCount(0, inputs[26] ? 1 : 0);
    }
    // Key events: applied in slot order on the emulation thread, so the poll
    // bitset AND the core's keyboard event callback see the identical sequence
    // on every peer.
    for (int slot = 0; slot < NP_KEY_SLOTS; ++slot)
    {
        int32_t packed = inputs[27 + slot * 2];
        uint32_t keycode = static_cast<uint32_t>(packed) & 0xFFFF;
        if (keycode == 0)
            continue;
        bool down = (static_cast<uint32_t>(packed) >> 16) & 1;
        uint32_t character = static_cast<uint32_t>(inputs[27 + slot * 2 + 1]);
        m_input_handler->SetKeyState(0, keycode, down);
        m_input_handler->CallKeyboardEventCallback(down, keycode, character,
            m_input_handler->GetKeyModifiers(0));
    }
}

/// Serialize the machine state at the START of `frame` into the ring.
void Wrapper::SaveRollbackState(int64_t frame)
{
    if (!m_np_states.empty() && m_np_states.back().first >= frame)
        return;   // already have it (rollback re-entry at the anchor frame)
    size_t size = m_core->retro_serialize_size();
    if (size == 0)
        return;
    std::vector<uint8_t> buffer(size);
    if (!m_core->retro_serialize(buffer.data(), size))
        return;
    m_np_states.emplace_back(frame, std::move(buffer));
    while (m_np_states.size() > 40)
        m_np_states.pop_front();
}

/// Emit captured RAM CRCs once their frame is confirmed-verified — a CRC of a
/// speculative frame would false-positive against peers.
void Wrapper::FlushNetplayCrcs()
{
    for (auto it = m_np_crc_pending.begin(); it != m_np_crc_pending.end();)
    {
        if (it->first - 1 > m_np_verified)
            break;   // ordered map — nothing later qualifies either
        godot::Array args;
        args.append(it->first);
        args.append(static_cast<int64_t>(it->second));
        EmitSignalOnMainThread("netplay_crc", args);
        it = m_np_crc_pending.erase(it);
    }
}

/// Rewind to `to_frame` and silently replay up to the current frame with
/// corrected inputs. Audio from replayed frames is dropped (it already played
/// from the mispredicted run); video is skipped except for the final frame.
void Wrapper::NetplayRollbackReplay(int64_t to_frame, uint32_t mask, uint32_t local_mask)
{
    auto state_it = std::find_if(m_np_states.begin(), m_np_states.end(),
        [&](const auto& s) { return s.first == to_frame; });
    if (state_it == m_np_states.end())
    {
        LogWarning("Rollback: no saved state for frame " + std::to_string(to_frame));
        return;
    }
    if (!m_core->retro_unserialize(state_it->second.data(), state_it->second.size()))
    {
        LogError("Rollback: unserialize failed at frame " + std::to_string(to_frame));
        return;
    }
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
            auto confirmed = m_np_inputs.find(x);
            auto& prev_used = m_np_used;
            for (uint32_t port = 0; port < 4; ++port)
            {
                if (!(mask & (1u << port)))
                    continue;
                int32_t* dst = inputs.data() + port * 5;
                if (local_mask & (1u << port))
                {
                    // Local input is ground truth — replay exactly what was pressed.
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
        }
        m_np_used[x] = inputs;
        SaveRollbackState(x);   // no-op for the anchor, re-saves the rest
        m_np_replay_mute_video = (x != current - 1);
        ApplyNetplayInputs(inputs, mask);
        m_core->retro_run();
        if (m_np_crc_interval > 0 && (x + 1) % m_np_crc_interval == 0)
        {
            bool ok = false;
            uint32_t crc = ComputeRamCrc(ok);
            if (ok)
                m_np_crc_pending[x + 1] = crc;
        }
    }
    m_np_replaying = false;
    m_np_replay_mute_video = false;
    // Every replayed frame ≤ watermark ran with confirmed inputs.
    m_np_verified = std::min(m_np_watermark, current - 1);
    m_np_rollback_count.fetch_add(1, std::memory_order_relaxed);
}

void Wrapper::NetplayRollbackIteration(double frame_duration_ms, double& accumulator)
{
    const uint32_t mask = m_np_port_mask.load(std::memory_order_relaxed);
    const uint32_t local_mask = m_np_local_mask.load(std::memory_order_relaxed) & mask;
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
                if (!(mask & (1u << port)))
                    continue;
                match = std::equal(confirmed->second.data() + port * 5,
                                   confirmed->second.data() + port * 5 + 5,
                                   used->second.data() + port * 5);
            }
            if (!match)
            {
                rollback_to = x;
                break;
            }
            m_np_verified = x;
        }
    }
    if (rollback_to >= 0)
        NetplayRollbackReplay(rollback_to, mask, local_mask);

    // 2. Speculation throttle: never run more than max_ahead past the last
    //    confirmed frame (waits briefly; the outer loop re-enters and keeps
    //    draining commands / re-checking stop, so shutdown can't deadlock).
    frame = m_frame_counter.load(std::memory_order_relaxed);
    {
        std::unique_lock<std::mutex> lock(m_np_mutex);
        while (m_np_inputs.count(m_np_watermark + 1))
            ++m_np_watermark;
        if (frame > m_np_watermark + m_np_max_ahead)
        {
            m_np_cv.wait_for(lock, std::chrono::milliseconds(4), [&]
            {
                while (m_np_inputs.count(m_np_watermark + 1))
                    ++m_np_watermark;
                return m_stop_requested.load() || frame <= m_np_watermark + m_np_max_ahead;
            });
            if (m_stop_requested.load() || frame > m_np_watermark + m_np_max_ahead)
                return;   // still stalled — outer loop spins us back in
        }
    }

    // 3. Pace to the core's fps.
    if (accumulator < frame_duration_ms)
        return;
    if (accumulator > frame_duration_ms * 4.0)
        accumulator = frame_duration_ms * 4.0;
    accumulator -= frame_duration_ms;

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
        // Prune consumed confirmations well behind the verified point.
        m_np_inputs.erase(m_np_inputs.begin(), m_np_inputs.lower_bound(m_np_verified - 40));
    }
    m_np_used[frame] = inputs;
    m_np_used.erase(m_np_used.begin(), m_np_used.lower_bound(m_np_verified - 40));

    // 5. Snapshot, run, count.
    SaveRollbackState(frame);
    ApplyNetplayInputs(inputs, mask);
    m_audio_handler->CallAudioBufferStatusCallback();
    m_core->retro_run();
    const int64_t frame_done = m_frame_counter.fetch_add(1, std::memory_order_relaxed) + 1;

    if (m_np_crc_interval > 0 && frame_done % m_np_crc_interval == 0)
    {
        bool ok = false;
        uint32_t crc = ComputeRamCrc(ok);
        if (ok)
            m_np_crc_pending[frame_done] = crc;
    }
    FlushNetplayCrcs();
}

void Wrapper::SetCoreOption(const std::string& key, const std::string& value)
{
    Log("SetCoreOption: key=" + key + " value=" + value);

    if (!m_options_handler)
    {
        Log("SetCoreOption: m_options_handler is null, skipping");
        return;
    }

    // SerializeToFile (called by SetVariable) uses GetCurrentThreadWrapper() to
    // resolve the root directory and core name. SetCoreOption runs on the main
    // thread where the thread-local pointer is null, so we set it here — the
    // same pattern used in StopEmulationThread for DeInit calls.
    Log("SetCoreOption: setting thread-local wrapper and calling SetVariable");
    SetCurrentThreadWrapper(this);
    m_options_handler->SetVariable(key, value);
    SetCurrentThreadWrapper(nullptr);
    Log("SetCoreOption: done");
}

void Wrapper::_input(const godot::Ref<godot::InputEvent>& event)
{
    if (!m_running)
        return;

    Ref<InputEventMouseMotion> mouseMotion = event;
    if (mouseMotion.is_valid())
    {
        auto mouseMotionValue = mouseMotion->get_relative();
        m_input_handler->SetMousePosition(0, ToShort(mouseMotionValue.x), ToShort(mouseMotionValue.y));
    }

    Ref<InputEventMouseButton> mouseButton = event;
    if (mouseButton.is_valid())
    {
        int button_index = mouseButton->get_button_index();
        bool pressed     = mouseButton->is_pressed();

        auto buttons = m_input_handler->GetMouseButtons(0);

        switch (button_index)
        {
            case MouseButton::MOUSE_BUTTON_LEFT:
                if (pressed)
                    buttons |= (1 << RETRO_DEVICE_ID_MOUSE_LEFT);
                else
                    buttons &= ~(1 << RETRO_DEVICE_ID_MOUSE_LEFT);
                break;
            case MouseButton::MOUSE_BUTTON_RIGHT:
                if (pressed)
                    buttons |= (1 << RETRO_DEVICE_ID_MOUSE_RIGHT);
                else
                    buttons &= ~(1 << RETRO_DEVICE_ID_MOUSE_RIGHT);
                break;
            case MouseButton::MOUSE_BUTTON_MIDDLE:
                if (pressed)
                    buttons |= (1 << RETRO_DEVICE_ID_MOUSE_MIDDLE);
                else
                    buttons &= ~(1 << RETRO_DEVICE_ID_MOUSE_MIDDLE);
                break;
            case MouseButton::MOUSE_BUTTON_WHEEL_UP:
                if (pressed)
                    buttons |= (1 << RETRO_DEVICE_ID_MOUSE_WHEELUP);
                else
                    buttons &= ~(1 << RETRO_DEVICE_ID_MOUSE_WHEELUP);
                break;
            case MouseButton::MOUSE_BUTTON_WHEEL_DOWN:
                if (pressed)
                    buttons |= (1 << RETRO_DEVICE_ID_MOUSE_WHEELDOWN);
                else
                    buttons &= ~(1 << RETRO_DEVICE_ID_MOUSE_WHEELDOWN);
                break;
            case MouseButton::MOUSE_BUTTON_WHEEL_LEFT:
                if (pressed)
                    buttons |= (1 << RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELUP);
                else
                    buttons &= ~(1 << RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELUP);
                break;
            case MouseButton::MOUSE_BUTTON_WHEEL_RIGHT:
                if (pressed)
                    buttons |= (1 << RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELDOWN);
                else
                    buttons &= ~(1 << RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELDOWN);
                break;
            case MouseButton::MOUSE_BUTTON_XBUTTON1:
                if (pressed)
                    buttons |= (1 << RETRO_DEVICE_ID_MOUSE_BUTTON_4);
                else
                    buttons &= ~(1 << RETRO_DEVICE_ID_MOUSE_BUTTON_4);
                break;
            case MouseButton::MOUSE_BUTTON_XBUTTON2:
                if (pressed)
                    buttons |= (1 << RETRO_DEVICE_ID_MOUSE_BUTTON_5);
                else
                    buttons &= ~(1 << RETRO_DEVICE_ID_MOUSE_BUTTON_5);
                break;
            default:
                print_line_rich("[color=red][libretro-godot][/color] Unhandled mouse button: " + String::num_int64(button_index));
                break;
        }

        m_input_handler->SetMouseButtons(0, buttons);
    }

    Ref<InputEventKey> keyEvent = event;
    if (keyEvent.is_valid())
    {
        // A held RetroKeyboard object owns the OS-keyboard feed (it routes
        // keys itself, including into the netplay schedule) — skip the raw
        // path so events aren't double-fed.
        if (m_os_keyboard_captured.load(std::memory_order_relaxed))
            return;
        bool down          = keyEvent->is_pressed();
        uint32_t keycode   = GodotToLibretroKeycode(keyEvent);
        uint32_t character = keyEvent->get_unicode();
        // SetKeyState maintains the full RETROK bitset (the old uint32 mask
        // overflowed for keycodes >= 32), derives modifiers from held keys,
        // fires the core's keyboard event callback, and blocks itself during
        // netplay (keyboard rides the deterministic schedule there).
        SetKeyState(0, keycode, down, character);
    }
}

void Wrapper::_process(double delta)
{
    // Deferred stop: once the emulation thread has fully exited on its own,
    // the join is instant — finish the teardown here without ever blocking.
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

    std::unique_ptr<ThreadCommand> command;
    while (m_main_thread_commands_queue.try_dequeue(command))
        command->Execute();

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
    // Destroying a joinable std::thread is std::terminate — always block here.
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

    // Set the thread-local pointer on the main thread so that handler DeInit
    // calls (which call GetCurrentThreadWrapper()) can find the right instance.
    SetCurrentThreadWrapper(this);
    m_video_handler->DeInit();
    m_audio_handler->DeInit();
    SetCurrentThreadWrapper(nullptr);

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

    m_node = nullptr;
    m_node_id = 0;

    // Discard commands the dying thread left queued — they must not execute
    // against the next content run's handlers.
    std::unique_ptr<ThreadCommand> stale;
    while (m_main_thread_commands_queue.try_dequeue(stale)) {}
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

    if (!m_core->Load(m_trampolines.get()))
        return;

    if (m_game_path.empty())
    {
        if (!m_core->GetSupportsNoGame())
        {
            LogError("Game not set and this core does not support no game mode.");
            return;
        }

        retro_game_info game_info = {};

        if (!m_core->retro_load_game(&game_info))
        {
            LogError("Failed to load game");
            return;
        }
    }
    else
    {
        if (!std::filesystem::is_regular_file(m_game_path))
        {
            LogError("Game not found: " + m_game_path);
            return;
        }

        retro_game_info game_info = {};
        game_info.path = m_game_path.c_str();
        game_info.meta = nullptr;

        if (m_core->GetNeedFullpath())
        {
            // Disc cores (PS1/PS2/Saturn/Dreamcast/GC/PSP, MAME, ...) open the
            // image themselves from the path so they can seek, read tracks lazily
            // and hot-swap discs — they never touch game_info.data. Reading a 4 GB
            // ISO into a buffer nobody reads is an instant OOM kill on Quest.
            Log("Core needs fullpath — passing path only: " + m_game_path);
        }
        else
        {
            std::ifstream file(m_game_path, std::ios::binary | std::ios::ate);
            if (!file)
            {
                LogError("Failed to open game file: " + m_game_path);
                return;
            }

            size_t game_size = static_cast<size_t>(file.tellg());
            file.seekg(0, std::ios::beg);

            // A core that wants the bytes in memory is a cartridge core; those top
            // out well under this. Anything larger is a mismatch (or a corrupt
            // download) and would blow the app's memory budget — fail loudly
            // instead of attempting the allocation.
            constexpr size_t MAX_GAME_BUFFER_BYTES = 512ull * 1024ull * 1024ull;
            if (game_size > MAX_GAME_BUFFER_BYTES)
            {
                LogError("Game is " + std::to_string(game_size / (1024 * 1024)) +
                         " MB but this core requires the whole file in memory (limit " +
                         std::to_string(MAX_GAME_BUFFER_BYTES / (1024 * 1024)) + " MB): " + m_game_path);
                return;
            }

            m_game_buffer.resize(game_size);
            if (!file.read(reinterpret_cast<char*>(m_game_buffer.data()), game_size))
            {
                LogError("Failed to read game file: " + m_game_path);
                return;
            }

            game_info.data = reinterpret_cast<const void*>(m_game_buffer.data());
            game_info.size = game_size;
        }

        if (!m_core->retro_load_game(&game_info))
        {
            LogError("Failed to load game");
            return;
        }
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

    retro_system_av_info systemAvInfo = {};
    m_core->retro_get_system_av_info(&systemAvInfo);

    Log("FPS: " + std::to_string(systemAvInfo.timing.fps) + " Sample Rate: " + std::to_string(systemAvInfo.timing.sample_rate));

    if (!m_video_handler->InitHwRenderContext(systemAvInfo.geometry.base_width, systemAvInfo.geometry.base_height))
    {
        LogError("Failed to initialize video");
        return;
    }

    if (m_stop_requested)
        return;

    m_running = true;

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_mutex_done = false;
        m_main_thread_commands_queue.enqueue(std::make_unique<ThreadCommandInitAudio>(this, 0.1f, systemAvInfo.timing.sample_rate));
        m_condition_variable.wait(lock, [&]{ return m_mutex_done || m_stop_requested.load(); });
        if (m_stop_requested)
            return;
    }

    double frame_duration_ms = 1000.0 / systemAvInfo.timing.fps;
    auto last_time = std::chrono::steady_clock::now();
    double accumulator = 0.0;

    // Slack left before the next frame is due. sleep_for rounds up to the OS
    // timer granularity (coarse on Windows without timeBeginPeriod), so waking
    // early and taking the last stretch on the clock beats oversleeping frames.
    constexpr double SLEEP_MARGIN_MS = 1.5;

    // Battery save: fill SAVE_RAM from the cartridge/memory-card .srm (or the
    // netplay-injected bytes) before the first frame runs.
    LoadSramFromSource();
    m_sram_flush_counter = m_frame_counter.load(std::memory_order_relaxed);

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

    if (m_libretro_node)
        m_libretro_node->NotifyOptionsReady();

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

        // Battery save: dirty-check flush roughly every 10 seconds of frames.
        {
            int64_t fc = m_frame_counter.load(std::memory_order_relaxed);
            if (fc - m_sram_flush_counter >= 600)
            {
                m_sram_flush_counter = fc;
                FlushSramIfDirty();
            }
        }

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(now - last_time).count();
        last_time = now;
        accumulator += elapsed;

        if (!m_netplay_enabled.load(std::memory_order_acquire))
        {
            // Cap catch-up debt after stalls, as the netplay path below does.
            // Unclamped, a stall's whole duration is replayed back-to-back: the
            // picture fast-forwards and a burst of frames' worth of samples
            // overflows the audio buffer, which drops them mid-waveform.
            if (accumulator > frame_duration_ms * 4.0)
                accumulator = frame_duration_ms * 4.0;

            // Sync to audio. A core does not necessarily advance one display
            // refresh per retro_run: azahar returns when the *game* presents, so
            // a 30fps title advances two 60Hz frames per call and, driven at the
            // declared 60fps, runs at double speed — audible first as the audio
            // buffer overflowing. Letting the mixer set the pace throttles the
            // core to real time whatever its internal frame rate.
            if (m_audio_handler->IsBufferSaturated())
                accumulator = 0.0;

            while (accumulator >= frame_duration_ms)
            {
                m_audio_handler->CallAudioBufferStatusCallback();

                m_core->retro_run();
                m_frame_counter.fetch_add(1, std::memory_order_relaxed);

                accumulator -= frame_duration_ms;
            }


            // Wait out the rest of the frame instead of spinning the clock. The
            // busy-wait cost a whole core, which on Quest is one the main thread
            // needs — the spin helped cause the stalls it then raced to catch up
            // on. Leave a margin for coarse sleep granularity; whatever the sleep
            // overshoots lands in the accumulator, so the frame rate stays honest.
            const double remaining = frame_duration_ms - accumulator;
            if (remaining > SLEEP_MARGIN_MS)
                std::this_thread::sleep_for(
                    std::chrono::duration<double, std::milli>(remaining - SLEEP_MARGIN_MS));
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
                continue;   // stall — loop again (drains commands, re-checks stop)
            frame_inputs = m_np_inputs[frame];
            m_np_inputs.erase(m_np_inputs.begin(), m_np_inputs.lower_bound(frame - 30));
        }

        // Netplay-scheduled disc ops land strictly before their frame runs,
        // so every peer's core swaps discs on the identical frame.
        ApplyScheduledDiscOps(m_frame_counter.load(std::memory_order_relaxed));

        // Apply the agreed inputs — in netplay mode the emulation thread is
        // the sole InputHandler writer for the masked ports. Device-aware
        // (mouse ports get deltas), plus the aux block (sensor/touch).
        ApplyNetplayInputs(frame_inputs, m_np_port_mask.load(std::memory_order_relaxed));
        ApplyNetplayAux(frame_inputs);

        m_audio_handler->CallAudioBufferStatusCallback();
        m_core->retro_run();
        int64_t frame_done = m_frame_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        accumulator -= frame_duration_ms;

        if (m_np_crc_interval > 0 && frame_done % m_np_crc_interval == 0)
            EmitNetplayCrc(frame_done);
    }
    // Final battery-save flush while the core memory is still valid. Injected
    // netplay SRAM is one-shot — clear it so a later offline run loads its own.
    FlushSramIfDirty();
    m_sram_pending = godot::PackedByteArray();

    m_video_handler->NotifyContextDestroy();
    m_core->retro_unload_game();
    m_core->retro_deinit();

    m_running = false;
    t_current_wrapper = nullptr;
    Wrapper* expected = this;
    s_fallback_wrapper.compare_exchange_strong(expected, nullptr, std::memory_order_release);
    Log("Libretro thread stopped.");
}

void Wrapper::CreateTexture(Image::Format image_format, PackedByteArray pixel_data, int32_t width, int32_t height, bool flip_y)
{
    m_video_handler->SetImageFormat(image_format);
    m_main_thread_commands_queue.enqueue(std::make_unique<ThreadCommandCreateTexture>(this, image_format, pixel_data, width, height, flip_y));
}

void Wrapper::UpdateTexture(PackedByteArray pixel_data, bool flip_y)
{
    m_main_thread_commands_queue.enqueue(std::make_unique<ThreadCommandUpdateTexture>(this, pixel_data, flip_y));
}

bool Wrapper::Shutdown()
{
    // RETRO_ENVIRONMENT_SHUTDOWN arrives ON the emulation thread (inside
    // retro_run) — a blocking stop would join the thread from itself. Signal
    // only; _process() finishes the teardown after this thread exits.
    Log("Shutting down from core...");
    StopEmulationThread(false);
    return true;
}

void Wrapper::LedInterfaceSetLedState(int32_t led, int32_t state)
{
    print_line_rich("[color=cyan][LedInterfaceSetLedState][/color] LED " + String::num_int64(led) + " set to " + (state ? "on" : "off"));
}
}
