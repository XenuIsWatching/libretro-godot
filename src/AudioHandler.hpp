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
    void SetPlaying(bool playing);

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

    /// The Meta XR Audio voice ids this core is being spatialised through, or
    /// empty when running on the fallback AudioStreamPlayer3D. GDScript
    /// positions these; it does not own their lifetime.
    godot::PackedInt32Array GetVoiceIds() const;

private:
    // --- fallback: Godot's own 3D panning -----------------------------------
    godot::Ref<godot::AudioStreamGenerator> m_audio_stream_generator = nullptr;
    godot::Ref<godot::AudioStreamGeneratorPlayback> m_audio_stream_generator_playback = nullptr;
    godot::AudioStreamPlayer3D* m_audio_stream_player = nullptr;

    // --- Meta XR Audio path -------------------------------------------------
    godot::Object* m_mx = nullptr;          ///< the MetaXRAudio singleton, or null
    int    m_voice_l = -1;
    int    m_voice_r = -1;
    bool   m_use_sdk = false;
    double m_mix_rate = 48000.0;

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
    uint32_t m_audio_buffer_occupancy = 0;
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
