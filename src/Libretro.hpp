#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <memory>
#include <string>
#include <unordered_map>

#include "OptionsHandler.hpp"

namespace Xenu
{
class LibretroOptionCategory : public godot::RefCounted
{
    GDCLASS(LibretroOptionCategory, godot::RefCounted);

    friend class Libretro;

public:
    const godot::String& GetDescription() const { return m_desc; }
    const godot::String& GetInfo() const { return m_info; }

private:
    godot::String m_desc;
    godot::String m_info;

protected:
    static void _bind_methods()
    {
        godot::ClassDB::bind_method(godot::D_METHOD("GetDescription"), &LibretroOptionCategory::GetDescription);
        godot::ClassDB::bind_method(godot::D_METHOD("GetInfo"), &LibretroOptionCategory::GetInfo);
    }
};

class LibretroOptionValue : public godot::RefCounted
{
    GDCLASS(LibretroOptionValue, godot::RefCounted);

    friend class Libretro;

public:
    const godot::String& GetValue() const { return m_value; }
    const godot::String& GetLabel() const { return m_label; }

private:
    godot::String m_value;
    godot::String m_label;

protected:
    static void _bind_methods()
    {
        godot::ClassDB::bind_method(godot::D_METHOD("GetValue"), &LibretroOptionValue::GetValue);
        godot::ClassDB::bind_method(godot::D_METHOD("GetLabel"), &LibretroOptionValue::GetLabel);
    }
};

class LibretroOptionDefinition : public godot::RefCounted
{
    GDCLASS(LibretroOptionDefinition, godot::RefCounted);

    friend class Libretro;

public:
    const godot::String& GetDescription() const { return m_desc; }
    const godot::String& GetDescriptionCategorized() const { return m_desc_categorized; }
    const godot::String& GetInfo() const { return m_info; }
    const godot::String& GetInfoCategorized() const { return m_info_categorized; }
    const godot::String& GetCategoryKey() const { return m_category_key; }
    const godot::Array& GetValues() const { return m_values; }
    const godot::String& GetDefaultValue() const { return m_default_value; }

private:
    godot::String m_desc;
    godot::String m_desc_categorized;
    godot::String m_info;
    godot::String m_info_categorized;
    godot::String m_category_key;
    godot::Array m_values;
    godot::String m_default_value;
    
protected:
    static void _bind_methods()
    {
        godot::ClassDB::bind_method(godot::D_METHOD("GetDescription"), &LibretroOptionDefinition::GetDescription);
        godot::ClassDB::bind_method(godot::D_METHOD("GetDescriptionCategorized"), &LibretroOptionDefinition::GetDescriptionCategorized);
        godot::ClassDB::bind_method(godot::D_METHOD("GetInfo"), &LibretroOptionDefinition::GetInfo);
        godot::ClassDB::bind_method(godot::D_METHOD("GetInfoCategorized"), &LibretroOptionDefinition::GetInfoCategorized);
        godot::ClassDB::bind_method(godot::D_METHOD("GetCategoryKey"), &LibretroOptionDefinition::GetCategoryKey);
        godot::ClassDB::bind_method(godot::D_METHOD("GetValues"), &LibretroOptionDefinition::GetValues);
        godot::ClassDB::bind_method(godot::D_METHOD("GetDefaultValue"), &LibretroOptionDefinition::GetDefaultValue);
    }
};

class Wrapper;

// Node3D, not Node: the AudioStreamPlayer3D created per-instance in
// Wrapper::StartContent is parented here, and a Node3D under a plain Node roots
// its own transform hierarchy, so the emitter would sit at the world origin and
// never follow the cabinet/handheld when it is picked up and carried around.
class Libretro : public godot::Node3D
{
    GDCLASS(Libretro, godot::Node3D);

    friend class Wrapper;
    
public:
    Libretro();
    ~Libretro();

    void StartContent(godot::MeshInstance3D* node, godot::String root_directory, godot::String core_name, godot::String game_path);
    void StopContent();

    /// Ask to track RetroAchievements for the content about to be started.
    /// `console_id` is an RC_CONSOLE_* value (see RaConsoles.for_systemid); 0 means
    /// the system has no RetroAchievements equivalent. Call BEFORE StartContent,
    /// since identification happens as the core comes up. False when another machine
    /// already holds the session, nobody is signed in, or achievements are off.
    bool RaClaimSession(int console_id);
    /// True while this node's core is the one being tracked.
    bool RaHoldsSession() const;
    void SetScreenMesh(godot::MeshInstance3D* node);
    void SetCoreOption(const godot::String& key, const godot::String& value);
    void SetInputEnabled(bool enabled);

    /// Which hardware API GET_PREFERRED_HW_RENDER advertises, as a
    /// retro_hw_context_type (6 = Vulkan, 7 = D3D11, 9 = D3D12). Applies to
    /// every core started afterwards, so set it before StartContent.
    static void SetPreferredHwRender(int context_type);

    /// Returns per-port controller info as Array[Dictionary{port, controllers: Array[{name,id}], current_id}].
    godot::Array GetControllerInfo();
    /// Meta XR Audio voice ids this core's sound is spatialized through, or
    /// empty when it is running on the fallback AudioStreamPlayer3D.
    godot::PackedInt32Array GetAudioVoiceIds();

    /// 0 stereo, 1 the left channel to both speakers, 2 the right to both.
    void SetAudioChannelMode(int mode);

    /// Tell the running core which device type is active on a given port.
    void SetControllerPortDevice(int port, int device);

    /// Light gun input. Called from GDScript each frame when the gun is plugged in.
    void SetLightgunPosition(int port, int x, int y);
    void SetLightgunIsOffscreen(int port, bool offscreen);
    void SetLightgunButton(int port, int button_id, bool pressed);

    /// Per-port joypad input. Called from GDScript by physical retro controller objects.
    void SetJoypadState(int port, int button_mask, int analog_lx, int analog_ly, int analog_rx, int analog_ry);
    /// Relative mouse deltas (accumulated until the core's next read) + button
    /// bitmask of (1 << RETRO_DEVICE_ID_MOUSE_*) for a RETRO_DEVICE_MOUSE port.
    void SetMouseState(int port, int dx, int dy, int buttons);
    /// Keyboard: RETROK_* keycode down/up + unicode character. Updates the
    /// RETRO_DEVICE_KEYBOARD poll bitset and fires the core's keyboard event
    /// callback (modifiers derived from held keys). Feed port 0.
    void SetKeyState(int port, int keycode, bool down, int character);
    /// True while a physical RetroKeyboard object owns the OS-keyboard feed.
    /// Godot key event -> RETROK_* code. The application decides when to send a
    /// key (retro_keyboard.gd); this is only the lookup table.
    int GodotKeyToRetroKey(const godot::Ref<godot::InputEventKey>& event) const;

    /// Accelerometer feed for tilt-sensor games (g units, at-rest ≈ (0,0,1)).
    /// Fed each frame from a held handheld's physical orientation.
    void SetSensorAccel(int port, float x, float y, float z);

    /// Gyroscope feed in radians/second about the device's own axes, fed each
    /// frame from how fast a held handheld is turning. This is what drives the
    /// Wii MotionPlus; a device sitting still reads (0, 0, 0).
    void SetSensorGyro(int port, float x, float y, float z);

    /// Touch/pointer feed (RETRO_DEVICE_POINTER): x/y in [-32767, 32767]
    /// across the whole video output. Drives the DS/3DS touch screen.
    /// Writes touch index 0 and drops any other index, so a single-point caller
    /// never inherits leftovers from SetPointerIndexState.
    void SetPointerState(int port, int x, int y, bool pressed);

    /// One touch index of the pointer device (0-3), for cores that read more than
    /// the first. Dolphin's Wiimote IR passthrough is the reason this exists: it
    /// takes one camera object per index, so the frontend can hand it the real
    /// view of the sensor bar instead of a cursor position. Same [-32767, 32767]
    /// range; that core wants the positive half, 0 = 0.0 and 32767 = 1.0.
    /// Unlike SetPointerState this leaves the other indices alone, so send every
    /// index you own each frame, visible or not.
    void SetPointerIndexState(int port, int index, int x, int y, bool pressed);

    // ── Netplay (deterministic lockstep) ─────────────────────────────────────
    /// Gate the emulation loop: frame N runs only once PostNetplayInputs(N,…)
    /// arrived. port_mask selects participating ports; start_frame resets the
    /// frame counter. Call before StartContent for a cold start.
    void SetNetplayMode(bool enabled, int port_mask, int64_t start_frame);
    /// Agreed inputs for one frame: flat PackedInt32Array of 4 ports × 5 values
    /// {button_mask, alx, aly, arx, ary}, optionally followed by the
    /// sensor/pointer/keyboard auxiliary block. In rollback mode these are the
    /// CONFIRMATIONS: a mismatch with what already ran triggers rewind+replay.
    void PostNetplayInputs(int64_t frame, const godot::PackedInt32Array& inputs);

    /// Enable GGPO-style rollback within netplay mode: locally-owned ports
    /// (local_mask) apply live with zero delay, remote ports are predicted and
    /// corrected via invisible rewind+replay. max_ahead caps speculation.
    void SetNetplayRollback(bool enabled, int local_mask, int max_ahead);

    /// Drain per-frame local-input records: flat groups of 7 ints
    /// {frame, port, buttons, alx, aly, arx, ary}: what this peer actually
    /// pressed each frame, to be shipped to the host for assembly.
    godot::PackedInt32Array TakeNetplayLocalRecords();
    /// Async savestate → savestate_ready(data: PackedByteArray, frame: int).
    void RequestSaveState();
    /// Async state load + schedule reset → savestate_loaded(ok: bool).
    void RequestLoadState(const godot::PackedByteArray& data, int64_t frame);
    // ── Battery saves (SRAM) ─────────────────────────────────────────────────
    /// Backing .srm file for this run. Call before StartContent; while running
    /// it hot-swaps (flush old, load new): a physical memory-card swap.
    /// Empty path = no persistence (PSX with no card seated).
    void SetSramPath(const godot::String& path);
    /// Netplay: inject exact SRAM bytes applied at load instead of the file.
    void SetSramData(const godot::PackedByteArray& data);
    /// This machine saves to REMOVABLE media (a PSX memory card). With nothing
    /// seated, SAVE_RAM is blanked so the game reports unformatted media rather
    /// than accepting a save the core invented and losing it at power-off.
    void SetRemovableStorage(bool removable);
    /// Force a dirty-check flush of SRAM to its backing file now.
    void RequestSramFlush();
    /// Front-panel reset (retro_reset), applied between frames. The core stays
    /// loaded, so the screen, audio voices and port bindings all survive.
    void RequestReset();
    // ── Disk control (multi-disc games) ──────────────────────────────────────
    /// Query the core's disk-control state → disk_control_ready(has_control,
    /// count, current_index, ejected). Emits has_control=false when the core
    /// lacks the interface or nothing is running.
    void RequestDiskInfo();
    /// Open (true) / close (false) the core's virtual disc tray.
    void SetDiskEjectState(bool ejected);
    /// Hand the core a new disc file at image `index` (tray must be open).
    void ReplaceDiskImage(int64_t index, const godot::String& path);
    /// Netplay: apply a disc op right before running confirmed `frame` on every peer.
    /// op 0 = eject; op 1 = replace at `index` with `path` + close tray.
    void ScheduleDiscOp(int64_t frame, int64_t op, int64_t index, const godot::String& path);

    /// Frames executed since content start (or since the last state load).
    int64_t GetFrameCount() const;

    /// Rewind+replay corrections performed so far (rollback diagnostics).
    int64_t GetNetplayRollbackCount() const;

    /// Performance HUD readings. Every one is a relaxed atomic load, so they are
    /// safe to poll from _process on any number of instances at once — each node
    /// answers only for its own core.
    ///
    /// The core's declared timing, and how far behind the picture is:
    double GetDeclaredFps() const;
    double GetDeclaredSampleRate() const;
    /// Frames produced but never shown, because a newer one replaced them before
    /// the main thread drained the queue. Resets with each content run.
    int64_t GetDroppedFrameCount() const;
    /// Audio sink fill 0-100 (<= 10 is what the core is told is an underrun), and
    /// how long the pacing loop is holding the core back. A brake pinned at 0 with
    /// a low fill is a core that cannot keep up.
    int64_t GetAudioBufferOccupancy() const;
    double GetAudioBrakeMs() const;

    void ConnectOptionsReady(const godot::Callable& callable, uint32_t flags = 0u);

    void _exit_tree() override;
    void _process(double delta) override;

    /// Called from the emulation thread (via Wrapper::LiveLibretroNode) when options are ready.
    void NotifyOptionsReady();

    /// Called from the emulation thread after SAVE_RAM was actually written to
    /// disk, i.e. only when the dirty check found a change. `final` marks the
    /// flush at core shutdown, the last one for this run.
    void NotifySramFlushed(const godot::String& path, int64_t size, bool final_flush);

    /// Read a core's option set without starting it, for menus that let the
    /// player set options before launch. Returns a dictionary shaped like the
    /// options_ready signal ("categories", "definitions", "values"), with each
    /// option sitting at its core-declared default. Empty if the core could not
    /// be read. Blocking, but only as long as a dlopen plus the core's static
    /// initializers; no emulation is started.
    godot::Dictionary PeekCoreOptions(const godot::String& root_directory, const godot::String& core_name);

private:
    /// Hand a Wrapper whose core will not stop to a store that outlives this node,
    /// and take a fresh one. The abandoned thread is still running inside it, so it
    /// can be neither joined nor destroyed; leaking it is the only way out that is
    /// not a hang or a use-after-free.
    void AbandonWrapper();

    std::unique_ptr<Wrapper> m_wrapper;

    /// Seconds since the last rc_client_idle. Every node runs this, so the tick
    /// costs one mutexed no-op per node per second when nothing is signed in.
    double m_ra_idle_accumulator = 0.0;

    static godot::Dictionary ConvertOptionCategories(const std::unordered_map<std::string, OptionCategory>& categories);
    static godot::Dictionary ConvertOptionDefinitions(const std::unordered_map<std::string, OptionDefinition>& definitions);
    static godot::Dictionary ConvertOptionValues(const std::unordered_map<std::string, std::string>& values);

    static void _bind_methods();
};
}
