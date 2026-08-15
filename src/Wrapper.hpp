#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/input_event_key.hpp>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include <vector>
#include <queue>
#include <map>
#include <array>
#include <deque>
#include <unordered_map>

#include <libretro.h>
#include <readerwriterqueue.h>

#include "ThreadCommand.hpp"
#include "EmuThreadCommands.hpp"
#include "Core.hpp"
#include "CallbackTrampolines.hpp"
#include "EnvironmentHandler.hpp"
#include "VideoHandler.hpp"
#include "AudioHandler.hpp"
#include "InputHandler.hpp"
#include "OptionsHandler.hpp"
#include "MessageHandler.hpp"
#include "LogHandler.hpp"

namespace Xenu
{
// Forward declaration to avoid circular include (Libretro.hpp includes Wrapper.hpp indirectly)
class Libretro;

class Wrapper
{
public:
    Wrapper() = default;
    ~Wrapper();

    Wrapper(const Wrapper&) = delete;
    Wrapper& operator=(const Wrapper&) = delete;
    Wrapper(Wrapper&&) = delete;
    Wrapper& operator=(Wrapper&&) = delete;

    /// Returns the Wrapper instance currently running on this thread. Per-wrapper
    /// callback trampolines set this before dispatch, including on core-created
    /// threads, so no process-global fallback is needed or safe.
    static Wrapper* GetCurrentThreadWrapper();

    /// Set or clear the current-thread Wrapper pointer. Called automatically by
    /// the emulation thread; also used by thread commands and cleanup code that
    /// run on the main thread and need access to the owning Wrapper.
    static void SetCurrentThreadWrapper(Wrapper* wrapper);

    /// Locate a core inside <root>/cores, trying every filename convention the
    /// platform can use. Shared with the pre-start option peek so both resolve
    /// the same file for a given core name.
    static std::string ResolveCorePath(const std::string& root_directory, const std::string& core_name);

    void StartContent(godot::MeshInstance3D* node, const std::string& root_directory, const std::string& core_name, const std::string& game_path);
    void StopContent();
    /// Silence, then stop the emulation thread, waiting at most `budget_ms` for it
    /// to leave the core. True when it exited and teardown finished. False means
    /// the core never unwound (Dolphin has managed this): the thread is still
    /// running inside this Wrapper, so the caller must Abandon it rather than
    /// destroy it.
    bool ShutdownForExit(uint32_t budget_ms);

    /// Same bounded stop without the silencing, for a restart.
    bool StopEmulationThreadBounded(uint32_t budget_ms);

    /// Give up on a thread that will not stop: silence it and detach it, leaving
    /// every handler it is still inside alive. A Wrapper this has been called on
    /// must never be destroyed.
    void AbandonThread();
    /// m_node re-resolved through ObjectDB, or null if it has been freed.
    godot::MeshInstance3D* LiveNode() const;
    void SetScreenMesh(godot::MeshInstance3D* node);
    /// The running core's picture. See VideoHandler::GetTexture.
    godot::Ref<godot::ImageTexture> GetVideoTexture() const;

    const std::unordered_map<std::string, OptionCategory>& GetOptionCategories() const { return m_options_handler->GetCategories(); }
    const std::unordered_map<std::string, OptionDefinition>& GetOptionDefinitions() const { return m_options_handler->GetDefinitions(); }
    const std::unordered_map<std::string, std::string>& GetOptionValues() const { return m_options_handler->GetValues(); }
    void SetCoreOption(const std::string& key, const std::string& value);

    /// Returns per-port controller info as an Array of Arrays of Dictionaries
    /// [{name, id}], indexed by port number. Consumed by Libretro::GetControllerInfo().
    godot::Array GetControllerInfo() const;

    /// Tell the running core which device type is active on a given port.
    /// Calls retro_set_controller_port_device and updates the local tracking map.
    void SetControllerPortDevice(uint32_t port, uint32_t device);

    /// Light gun input forwarding. Called from the Libretro node on the main thread.
    void SetLightgunPosition(uint32_t port, int16_t x, int16_t y);
    void SetLightgunIsOffscreen(uint32_t port, bool offscreen);
    void SetLightgunButton(uint32_t port, int button_id, bool pressed);

    /// Per-port joypad input. Replaces the hardcoded port-0 path in _process for
    /// physical controller objects that know their own port assignment.
    void SetJoypadState(uint32_t port, uint16_t button_mask, int16_t analog_lx, int16_t analog_ly, int16_t analog_rx, int16_t analog_ry);

    /// Keyboard input: update the RETRO_DEVICE_KEYBOARD poll bitset AND fire
    /// the core's keyboard event callback (modifiers derived from held keys).
    /// Keyboard state is global in practice, so feed port 0.
    void SetKeyState(uint32_t port, uint32_t keycode, bool down, uint32_t character);

    /// Translate a Godot key event to its RETROK_* code. Static and side-effect
    /// free: the application decides when a key should reach a core (see
    /// retro_keyboard.gd), this only does the lookup. Takes the whole event
    /// because get_location() selects the left/right modifier variant.
    static retro_key GodotKeyToRetroKey(const godot::Ref<godot::InputEventKey>& keyEvent);

    /// Per-port mouse input (RETRO_DEVICE_MOUSE) for physical mouse objects.
    /// dx/dy are relative deltas ACCUMULATED until the core's next read;
    /// buttons is a bitmask of (1 << RETRO_DEVICE_ID_MOUSE_*): LEFT bit 2,
    /// RIGHT bit 3, MIDDLE bit 6.
    void SetMouseState(uint32_t port, int32_t dx, int32_t dy, uint32_t buttons);

    /// Accelerometer feed (g units, at-rest flat ≈ (0,0,1)) for the libretro
    /// sensor interface, so a held handheld's physical tilt drives tilt carts.
    void SetSensorAccel(uint32_t port, float x, float y, float z);

    /// Gyroscope feed (radians/second about the device's own axes) for the same
    /// interface. Rotation rate, not orientation: a still device reads (0,0,0).
    void SetSensorGyro(uint32_t port, float x, float y, float z);

    /// Touch/pointer feed (RETRO_DEVICE_POINTER): x/y normalized to
    /// [-0x7FFF, 0x7FFF] across the WHOLE video output (the composite
    /// framebuffer for dual-screen cores; melonDS maps the bottom-screen
    /// region of it to DS touch).
    void SetPointerState(uint32_t port, int16_t x, int16_t y, bool pressed);
    void SetPointerIndexState(uint32_t port, uint32_t index, int16_t x, int16_t y, bool pressed);

    // ── Netplay (deterministic lockstep) ─────────────────────────────────────
    // In netplay mode the emulation thread runs frame N only once the inputs
    // for frame N have been posted (all masked ports at once), making every
    // peer's core execute an identical input timeline.

    /// Enable/disable lockstep gating. port_mask selects which of ports 0-3
    /// participate; start_frame (>= 0) resets the frame counter (use 0 for a
    /// cold start, the savestate frame for a late join). Safe to call before
    /// StartContent.
    void SetNetplayMode(bool enabled, uint32_t port_mask, int64_t start_frame);

    /// Post the agreed inputs for one frame: flat array of 4 ports × 5 values
    /// {button_mask, analog_lx, analog_ly, analog_rx, analog_ry}, followed by
    /// the optional sensor, pointer, and keyboard auxiliary block documented
    /// below. Legacy 20-value frames remain valid.
    /// Lockstep: releases the gate for `frame`. Rollback: confirms `frame`, and
    /// a mismatch against what was executed triggers rewind+replay.
    void PostNetplayInputs(int64_t frame, const godot::PackedInt32Array& flat);

    // ── Rollback (GGPO-style, layered on netplay mode) ───────────────────────
    // Locally-owned ports apply live with zero added delay; remote ports are
    // predicted (hold-last-input). Each frame is savestated into a ring before
    // it runs. When confirmed inputs contradict a prediction the emulation
    // thread reloads the state at the mispredicted frame and silently replays
    // (audio dropped, video skipped except the final frame), so the local
    // player never feels the network.

    /// Enable rollback within netplay mode. local_mask ⊆ port_mask marks the
    /// ports whose input is sampled live on this peer; max_ahead caps how far
    /// past the last confirmed frame the emulation may speculate before
    /// stalling. Call after SetNetplayMode, before StartContent.
    void SetNetplayRollback(bool enabled, uint32_t local_mask, int max_ahead);

    /// Drain the per-frame local-input records the emulation thread produced:
    /// flat groups of 7 ints {frame, port, buttons, alx, aly, arx, ary}. These
    /// are the authoritative "what this peer pressed on frame N" values that
    /// the session ships to the host for assembly.
    godot::PackedInt32Array TakeNetplayLocalRecords();

    // ── Battery saves (SRAM / RETRO_MEMORY_SAVE_RAM) ─────────────────────────
    // The frontend owns persistence: SRAM is loaded from m_sram_path right
    // after retro_load_game and flushed (dirty-checked) every ~10 s and at
    // shutdown. An empty path disables persistence entirely (e.g. a PSX with
    // no memory card seated).

    /// Set the .srm file backing this run. Call before StartContent; calling
    /// while running performs a hot-swap on the emulation thread (flush the
    /// old file, load the new one into SAVE_RAM): a real memory-card swap.
    void SetSramPath(const godot::String& path);

    /// Netplay: inject SRAM content directly (applied at load instead of the
    /// file, so every peer boots with identical SRAM). Never flushed.
    void SetSramData(const godot::PackedByteArray& data);

    /// Declare that this machine's battery save lives on REMOVABLE media, so
    /// an unbacked run means "nothing is plugged in" rather than "don't save".
    /// With this set and no path or injected bytes, SAVE_RAM is blanked at load
    /// instead of being left as the core initialized it. pcsx_rearmed hands back
    /// a fully FORMATTED card when the frontend supplies nothing, so without
    /// blanking a PSX with no card seated accepts a save and loses it at
    /// power-off; blanking makes the game report an unformatted card instead.
    ///
    /// Opt-in on purpose: a fixed-storage core may initialize SAVE_RAM to 0xFF
    /// (flash/EEPROM) and zeroing that would fake corrupt save data, and a
    /// netplay client legitimately runs with an empty path and empty bytes.
    void SetRemovableStorage(bool removable);

    /// Force a dirty-check flush now (emu-thread command).
    void RequestSramFlush();

    // Emulation-thread internals (SRAM).
    void LoadSramFromSource();
    /// `final_flush` marks the flush at core shutdown; it only labels the
    /// signal, the write itself is identical.
    void FlushSramIfDirty(bool final_flush = false);
    void ApplySramSwap(const std::string& new_path);

    /// Front-panel reset: retro_reset on the emulation thread, between frames.
    /// Nothing is unloaded and no thread is joined, so this cannot block the
    /// caller however the core manages its own threads.
    void RequestReset();

    /// Serialize the core on the emulation thread; result arrives via the
    /// savestate_ready(data, frame) signal (empty data on failure).
    void RequestSaveState();

    /// Unserialize a savestate on the emulation thread and reset the netplay
    /// schedule to `frame`; result arrives via savestate_loaded(ok).
    void RequestLoadState(const godot::PackedByteArray& data, int64_t frame);

    // Disk control (multi-disc games). All three enqueue emu-thread commands;
    // state comes back via the disk_control_ready(has_control, count,
    // current_index, ejected) signal. Safe no-ops when the core has no
    // disk-control interface (or nothing is running).

    /// Query the core's disk-control state.
    void RequestDiskInfo();

    /// Open (true) / close (false) the core's virtual disc tray.
    void SetDiskEjectState(bool ejected);

    /// Hand the core a new disc file at image `index` (tray must be open).
    void ReplaceDiskImage(uint32_t index, const godot::String& path);

    /// Netplay: schedule a disc op to apply deterministically right before
    /// running confirmed `frame`. op 0 = eject (open tray),
    /// op 1 = replace at `index` with `path` then close the tray.
    void ScheduleDiscOp(int64_t frame, int32_t op, uint32_t index, const godot::String& path);

    // Emulation-thread internals (disk control).
    void EmitDiskInfo();
    void ApplyScheduledDiscOps(int64_t frame);

    struct DiscOp
    {
        int32_t op = 0;
        uint32_t index = 0;
        std::string path;
    };

    int64_t GetFrameCount() const { return m_frame_counter.load(std::memory_order_relaxed); }

    /// How many rewind+replay corrections have happened (diagnostics/HUD).
    int64_t GetNetplayRollbackCount() const { return m_np_rollback_count.load(std::memory_order_relaxed); }

    /// What the core currently declares its machine runs at. Updated when a
    /// running core successfully changes its AV info. 0 until content is running.
    double GetDeclaredFps() const { return m_declared_fps.load(std::memory_order_relaxed); }
    double GetDeclaredSampleRate() const { return m_declared_sample_rate.load(std::memory_order_relaxed); }

    /// Frame uploads discarded because a newer one superseded them before the main
    /// thread drained the queue — i.e. emulated frames that were never displayed.
    /// Reset with each content run.
    int64_t GetDroppedFrameCount() const { return m_dropped_frames.load(std::memory_order_relaxed); }

    /// Audio sink fill 0-100, and the pacing brake in ms. Both forward to the audio
    /// handler, which may not exist yet; defined out of line because it is only
    /// forward declared here.
    uint32_t GetAudioBufferOccupancy() const;
    double GetAudioBrakeMs() const;

    /// Queue a signal emission on the owning Libretro node (main thread).
    /// Callable from the emulation thread.
    void EmitSignalOnMainThread(const godot::StringName& signal_name, const godot::Array& args);

    /// CRC32 of the core's system RAM, emitted as netplay_crc(frame, crc)
    /// every m_np_crc_interval frames while in netplay mode (desync detection).
    void EmitNetplayCrc(int64_t frame);
    uint32_t ComputeRamCrc(bool& ok) const;

    // Emulation-thread internals (rollback engine).
    void NetplayRollbackIteration(double frame_duration_ms, double& accumulator);
    bool NetplayRollbackReplay(int64_t to_frame, uint32_t mask, uint32_t local_mask);
    bool SaveRollbackState(int64_t frame);
    void FailNetplayRollback(const std::string& reason);
    bool IsNetplayPortManaged(uint32_t port) const;
    // Netplay frame payload: 4 ports × 5 ints (joypad btn/alx/aly/arx/ary, or
    // for RETRO_DEVICE_MOUSE ports: buttons/dx/dy/-/-), then the aux block:
    // [20] flags (bit0 sensor valid, bit1 pointer valid), [21..23] accel in
    // milli-g, [24..25] pointer x/y, [26] pointer pressed, then up to 4 key
    // events × 2 ints each: [keycode | down<<16, unicode character]
    // (keycode 0 = empty slot). Legacy shorter frames are accepted (rest
    // zeroed).
    static constexpr int NP_KEY_SLOTS = 4;
    static constexpr int NP_FRAME_INTS = 27 + NP_KEY_SLOTS * 2;
    static constexpr int NP_AUX_OFFSET = 20;
    static constexpr int64_t NP_ROLLBACK_HISTORY = 40;
    static constexpr int64_t NP_MAX_FUTURE_INPUTS = 600;
    using NpFrame = std::array<int32_t, NP_FRAME_INTS>;

    struct RollbackState
    {
        int64_t frame = 0;
        std::vector<uint8_t> core;
        InputHandler::NetplayState input;
    };

    void ApplyNetplayInputs(const NpFrame& inputs, uint32_t mask);
    void ApplyNetplayAux(const NpFrame& inputs);
    void FlushNetplayCrcs();

    void _process(double delta);

    godot::MeshInstance3D* m_node;
    /// Instance id of m_node. A raw node pointer outlives the node it points at
    /// during scene teardown; this is what makes the liveness check possible.
    uint64_t m_node_id = 0;

    const std::string& GetRootDirectory() const { return m_root_directory; }
    const std::string& GetTempDirectory() const { return m_temp_directory; }

    /// Descriptors handed over by RETRO_ENVIRONMENT_SET_MEMORY_MAPS, deep-copied.
    /// The core only guarantees the array and the addrspace strings for the
    /// duration of that one callback; the `ptr` values inside stay valid for the
    /// session, which is what makes copying the rest worthwhile. rcheevos needs
    /// this to translate a RetroAchievements address into a host pointer.
    void SetMemoryDescriptors(const retro_memory_map* memory_maps);
    /// A retro_memory_map view over the copies. Empty descriptors when the core
    /// never sent any, which rc_libretro_memory_init handles by falling back to
    /// retro_get_memory_data.
    retro_memory_map GetMemoryMap() const;
    /// Whether the core declared achievement support via
    /// RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS. Cores that never call it are
    /// assumed to support them; the callback exists to opt OUT.
    bool GetSupportsAchievements() const { return m_supports_achievements; }
    void SetSupportsAchievements(bool support) { m_supports_achievements = support; }
    /// retro_get_memory_data/size for a RETRO_MEMORY_* id, for callers that need
    /// the raw region rather than the CRC ComputeRamCrc returns. Emulation thread.
    void GetCoreMemory(uint32_t id, uint8_t*& out_data, size_t& out_size) const;

    /// Apply RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO synchronously while the core
    /// is inside retro_run. Reinitializes affected drivers before returning.
    bool SetSystemAvInfo(const retro_system_av_info* av_info);

    /// The core is handed this struct's address in retro_get_system_av_info and may write
    /// through that pointer long afterwards (ScummVM revises timing.fps from inside
    /// context_reset), so it must outlive the call and cannot be a stack local.
    /// Read it live at the point of use; do not copy the fields out and cache them, or a
    /// core that legitimately retimes is pinned to its opening declaration.
    retro_system_av_info m_system_av_info = {};

    std::unique_ptr<Core> m_core = nullptr;
    std::unique_ptr<CallbackTrampolines> m_trampolines = nullptr;
    std::unique_ptr<EnvironmentHandler> m_environment_handler = nullptr;
    std::unique_ptr<VideoHandler> m_video_handler = nullptr;
    std::unique_ptr<AudioHandler> m_audio_handler = nullptr;
    std::unique_ptr<InputHandler> m_input_handler = nullptr;
    std::unique_ptr<OptionsHandler> m_options_handler = nullptr;
    std::unique_ptr<MessageHandler> m_message_handler = nullptr;
    std::unique_ptr<LogHandler> m_log_handler = nullptr;

    std::thread m_thread;
    moodycamel::ReaderWriterQueue<std::unique_ptr<ThreadCommand>> m_main_thread_commands_queue;
    std::mutex m_mutex;
    bool m_mutex_done = false;
    bool m_audio_reinit_success = false;
    bool m_audio_reinit_restore_failed = false;
    std::condition_variable m_condition_variable;
    std::atomic<bool> m_running = false;
    std::atomic<bool> m_stop_requested = false; // set by main thread; never written by emulation thread
    // Deferred-stop bookkeeping: m_stopping = a stop was signalled and teardown
    // is pending; m_thread_exited = the emulation thread has fully exited (set
    // by an RAII guard covering every exit path, so a deferred join can't hang).
    std::atomic<bool> m_stopping = false;
    std::atomic<bool> m_thread_exited = false;
    bool m_input_enabled = false;   // only true for the actively-controlled instance

    // Desired device per port, surviving across content runs. A controller (or
    // mouse) plugged in while the system is OFF records its device type here;
    // the emulation thread applies the map right after retro_load_game so the
    // core polls the right device from frame one. Guarded by m_port_device_mutex
    // (main thread writes, emulation thread reads once at startup).
    std::mutex m_port_device_mutex;
    std::unordered_map<uint32_t, uint32_t> m_pending_port_devices;

    // Netplay state. The input schedule maps frame → 4 ports × 5 int32s and is
    // written by the main thread (PostNetplayInputs) and consumed by the
    // emulation thread under m_np_mutex. In netplay mode the emulation thread
    // is the ONLY InputHandler writer for the masked ports.
    moodycamel::ReaderWriterQueue<std::unique_ptr<EmuThreadCommand>> m_emu_thread_commands_queue;
    std::atomic<bool> m_netplay_enabled = false;
    std::atomic<int64_t> m_frame_counter = 0;
    // Diagnostics published to the main thread (see the getters above). Written on
    // the emulation thread, read from _process; relaxed is enough for all three
    // because nothing else is ordered against them.
    std::atomic<double> m_declared_fps{0.0};
    std::atomic<double> m_declared_sample_rate{0.0};
    std::atomic<uint64_t> m_timing_revision{0};
    std::atomic<int64_t> m_dropped_frames{0};
    std::atomic<uint32_t> m_np_port_mask = 0x1;
    std::mutex m_np_mutex;
    std::condition_variable m_np_cv;
    std::map<int64_t, NpFrame> m_np_inputs;
    int64_t m_np_crc_interval = 60;
    // Netplay-scheduled disc ops (eject / replace), applied on the emulation
    // thread right before running their frame. Guarded by m_np_mutex.
    std::map<int64_t, DiscOp> m_disc_schedule;

    // Rollback state. Everything below m_np_mutex-guarded unless noted.
    std::atomic<bool> m_np_rollback = false;
    std::atomic<uint32_t> m_np_local_mask = 0;
    std::atomic<int64_t> m_np_rollback_count = 0;   // rewind+replay corrections
    int m_np_max_ahead = 8;
    NpFrame m_np_live_local{};              // live local inputs (main thread writes)
    std::vector<int32_t> m_np_local_records;                // flat {frame,port,5 vals} drained by main thread
    // Emulation-thread-only rollback bookkeeping (no lock needed):
    std::map<int64_t, NpFrame> m_np_used;   // inputs each executed frame actually ran with
    std::deque<RollbackState> m_np_states;            // core + frontend input state before frame N
    std::map<int64_t, uint32_t> m_np_crc_pending;           // captured CRCs awaiting confirmation
    int64_t m_np_watermark = -1;                            // highest contiguous confirmed frame
    int64_t m_np_verified = -1;                             // highest frame verified/corrected against confirmations
    bool m_np_replaying = false;                            // true while re-running frames after a rewind
    bool m_np_replay_mute_video = false;                    // skip video uploads for this replayed frame
    bool m_handling_av_info = false;                        // reject recursive driver reinitialization

    /// True while the emulation thread is silently replaying frames after a
    /// rollback; audio/video callbacks drop their output. Emu thread only.
    bool IsNetplayReplaying() const { return m_np_replaying; }
    bool IsNetplayReplayVideoMuted() const { return m_np_replaying && m_np_replay_mute_video; }

    // SRAM persistence. m_sram_path/m_sram_pending are set from the main
    // thread BEFORE StartContent (or swapped via EmuThreadCommandSetSram);
    // the shadow copy is emulation-thread-only.
    std::string m_sram_path;
    godot::PackedByteArray m_sram_pending;
    std::vector<uint8_t> m_sram_shadow;
    int64_t m_sram_flush_counter = 0;
    bool m_removable_storage = false;

    std::string m_root_directory;
    std::string m_temp_directory;
    std::string m_username = "DefaultUser";
    retro_log_level m_log_level = RETRO_LOG_WARN;

    std::string m_game_path;

    std::vector<unsigned char> m_game_buffer;

    /// Backing store for SetMemoryDescriptors. The strings are held separately so
    /// the char* inside each descriptor keeps pointing at storage we own; a
    /// vector<string> would reallocate its elements' buffers on growth, so this is
    /// only ever filled in one pass and never appended to afterwards.
    std::vector<retro_memory_descriptor> m_memory_descriptors;
    std::vector<std::string> m_memory_addrspaces;
    bool m_supports_achievements = true;

    /// The Libretro node that owns this Wrapper (set by Libretro constructor).
    /// Held by id, not by pointer: the emulation thread signals back through this
    /// node, and it can be freed while that thread is still running (a scene change,
    /// or a core that will not unwind). Resolved per use, so a dead node is a
    /// no-op instead of a dangling call.
    Libretro* LiveLibretroNode() const;
    uint64_t m_libretro_node_id = 0;

    /// Signal the emulation thread to stop. blocking=true joins and tears down
    /// synchronously (StartContent restart, destructor). blocking=false returns
    /// immediately; _process() joins and finishes the teardown once the thread
    /// has exited on its own, so stopping a core never hitches the main thread.
    /// Also the only safe form callable FROM the emulation thread (core-initiated
    /// RETRO_ENVIRONMENT_SHUTDOWN must not join itself).
    void StopEmulationThread(bool blocking = true);
    /// Post-join teardown: handler DeInit, core unload, member reset. Main thread.
    void FinishTeardown();
    void EmulationThreadLoop();
    void CreateTexture(godot::Image::Format image_format, godot::PackedByteArray pixel_data, int32_t width, int32_t height, bool flip_y);
    void UpdateTexture(godot::PackedByteArray pixel_data, int32_t width, int32_t height, bool flip_y);

    bool Shutdown();

    static void LedInterfaceSetLedState(int32_t led, int32_t state);
};
}
