#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/audio_stream_generator.hpp>
#include <godot_cpp/classes/audio_stream_generator_playback.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <libretro.h>

struct retro_resampler;

namespace Xenu
{
class AudioHandler
{
public:
    static void SampleCallback(int16_t left, int16_t right);
    static size_t SampleBatchCallback(const int16_t* data, size_t frames);

    void Init(float buffer_capacity_sec, double sample_rate);
    void DeInit();
    /// Change the core rate without replacing the player node or Meta XR voice
    /// ids. Transactional: failure leaves the previous backend running.
    bool ReinitSampleRate(double sample_rate);
    void SetPlaying(bool playing);

    /// Silence without touching the player node. ObjectDB still reports a node as
    /// alive while its own destructor runs, so a stop() during teardown walks an
    /// already-destroyed Vector<Ref<AudioStreamPlayback>> inside Godot. Nothing
    /// needs stopping on the way out — the node is being freed regardless.
    void SilenceForTeardown();
    void SetAudioStreamPlayer(godot::AudioStreamPlayer3D* player)
    {
        m_audio_stream_player_id = player ? static_cast<uint64_t>(player->get_instance_id()) : 0;
    }
    bool IsReady() const { return m_sink_ready.load(std::memory_order_acquire); }

    bool SetAudioBufferStatusCallback(const retro_audio_buffer_status_callback* callback);
    bool SetMinimumAudioLatency(const uint32_t* minimum_audio_latency);

    void CallAudioBufferStatusCallback();

    /// Audio frames the core has produced since Init, counted at the core's own
    /// declared sample rate (i.e. before resampling to the mixer's rate).
    ///
    /// This is the frontend's only measure of *emulated* time. retro_run covers
    /// a variable number of display refreshes in some cores, but every core emits
    /// samples in proportion to game time, so the sample count is what the
    /// emulation thread paces against.
    uint64_t FramesProduced() const { return m_frames_produced.load(std::memory_order_relaxed); }

    /// How long until the sink wants audio again, in milliseconds, and 0 when it
    /// wants some right now.
    ///
    /// This is the brake. The mixer drains at the hardware's rate, so how full the
    /// sink is measures *real* time, the only quantity here a core cannot
    /// misreport. Both of the alternatives are claims: timing.fps describes neither
    /// the call rate nor the time a call covers for cores that return on present,
    /// and the sample count is only proportional to game time if the core says so
    /// honestly.
    ///
    /// Returns 0 whenever there is no sink to measure, so a missing or stopped sink
    /// can never brake emulation to a halt.
    double MsUntilSinkWantsFrames() const;

    /// How full the sink is, 0-100, as a percentage of EffectiveTotalFrames.
    ///
    /// This is the same figure the core is handed every frame through the buffer
    /// status callback, where anything at or below 10 is reported as an underrun.
    /// Read from the main thread for the HUD, written on the emulation thread.
    uint32_t BufferOccupancy() const { return m_audio_buffer_occupancy.load(std::memory_order_relaxed); }

    /// The brake the pacing loop last acted on, in milliseconds.
    ///
    /// Published by the loop rather than recomputed on demand: MsUntilSinkWantsFrames
    /// calls into the sink, which belongs to the emulation thread. A value that sits
    /// at 0 means the core is never being held back, i.e. it cannot keep up.
    double LastBrakeMs() const { return m_last_brake_ms.load(std::memory_order_relaxed); }
    void SetLastBrakeMs(double ms) { m_last_brake_ms.store(ms, std::memory_order_relaxed); }

    /// The Meta XR Audio voice ids this core is being spatialized through, or
    /// empty when running on the fallback AudioStreamPlayer3D. GDScript
    /// positions these; it does not own their lifetime.
    godot::PackedInt32Array GetVoiceIds() const;

    /// Which source channel feeds each speaker: 0 stereo, 1 the left channel to
    /// both, 2 the right to both. A television's mono switch: a duplication, so
    /// both speakers keep radiating rather than the sound collapsing into one.
    /// Read on the emulation thread, written from the main one; a torn read costs
    /// at worst one buffer routed the old way.
    void SetChannelMode(int mode) { m_channel_mode.store(mode, std::memory_order_relaxed); }

private:
    // --- fallback: Godot's own 3D panning -----------------------------------
    godot::Ref<godot::AudioStreamGenerator> m_audio_stream_generator = nullptr;
    godot::Ref<godot::AudioStreamGeneratorPlayback> m_audio_stream_generator_playback = nullptr;
    /// The player is a child of the Libretro node, so the SceneTree owns it and is
    /// free to destroy it before this handler tears down — at which point a raw
    /// pointer is a use-after-free, intermittently, depending on teardown order.
    /// Held by id and resolved per use, the way Wrapper holds the screen mesh.
    godot::AudioStreamPlayer3D* LivePlayer() const;
    uint64_t m_audio_stream_player_id = 0;
    // Audio can arrive from a core-created worker while the main thread changes
    // the backend. Recursive because batch processing calls the sink helpers.
    mutable std::recursive_mutex m_sink_mutex;
    std::atomic<bool> m_accept_audio{false};
    std::atomic<bool> m_sink_ready{false};
    std::atomic<bool> m_playing{true};

    // --- Meta XR Audio path -------------------------------------------------
    /// The MetaXRAudio singleton belongs to another GDExtension, which memdeletes
    /// it when that extension deinitialises. Extension teardown order is not ours
    /// to choose, so hold it by id: once it is gone this resolves to null instead
    /// of calling into freed memory.
    godot::Object* LiveMx() const;
    uint64_t m_mx_id = 0;
    int    m_voice_l = -1;
    int    m_voice_r = -1;
    bool   m_use_sdk = false;
    double m_mix_rate = 48000.0;
    std::atomic<int> m_channel_mode{0};   ///< see SetChannelMode

    /// Cores declare their own rate (32040 Hz SNES, 44100 PSX, 48000 N64) while
    /// the SDK context runs at Godot's mix rate, so anything that does not match
    /// has to be resampled. Done here on the emulation thread, never on the
    /// audio thread.
    void* m_resampler = nullptr;
    const struct retro_resampler* m_resampler_backend = nullptr;
    double m_resample_ratio = 1.0;

    std::vector<float> m_in_float;          ///< s16 -> float, interleaved
    std::vector<float> m_out_float;         ///< resampled, interleaved
    godot::PackedVector2Array m_push_buf;   ///< hoisted so pushing allocates once

    float    m_audio_buffer_capacity_sec = 0;
    double   m_audio_sample_rate = 0.0;
    uint32_t m_audio_buffer_total_frames = 0;
    /// Written by the audio callback (emulation thread) and read both there and by
    /// the main thread's HUD; atomic for the same reason m_frames_produced is.
    std::atomic<uint32_t> m_audio_buffer_occupancy{0};
    /// Last brake the pacing loop computed. See LastBrakeMs.
    std::atomic<double> m_last_brake_ms{0.0};
    /// The sink's own target fill, cached at Init so the brake does not pay a
    /// cross-extension call for a constant on every pass of the pacing loop.
    uint32_t m_sink_target_frames = 0;
    /// Written by the audio callbacks (emulation thread, inside retro_run) and
    /// read by the pacing loop on that same thread; atomic only so exposing it
    /// elsewhere later cannot become a data race.
    std::atomic<uint64_t> m_frames_produced{0};
    retro_audio_buffer_status_callback_t m_audio_buffer_status_callback = nullptr;
    uint32_t m_minimum_audio_latency = 0;

    void PushFrames(const float* interleaved, size_t frames);
    uint32_t QueuedFrames() const;
    uint32_t EffectiveTotalFrames() const;
};
}
