#include "AudioHandler.hpp"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "Wrapper.hpp"
#include "Libretro.hpp"
#include "Debug.hpp"

extern "C" {
#include <audio/audio_resampler.h>
}

using namespace godot;

namespace Xenu
{
namespace
{
/// Widest the resampling ratio may be trimmed to steer the sink's depth. Half a
/// percent is far below an audible pitch change and is the same bound RetroArch
/// defaults its rate control to; it is a correction for buffer drift, not a
/// speed control, and the pacing brake still owns the coarse rate.
constexpr double k_drc_max_delta = 0.005;
}

void AudioHandler::SampleCallback(int16_t left, int16_t right)
{
    auto instance = Wrapper::GetCurrentThreadWrapper();
    if (!instance)
    {
        LogError("SampleCallback: Null Instance.");
        return;
    }

    // Rollback replay: this frame's audio already played on the first
    // (mispredicted) run — re-emitting it would double up.
    if (instance->IsNetplayReplaying())
        return;

    instance->m_audio_handler->m_frames_produced.fetch_add(1, std::memory_order_relaxed);

    // Single-sample path: too short to resample meaningfully, and cores that
    // use it are rare. Push straight through at the core's rate.
    const float frame[2] = { left / 32768.0f, right / 32768.0f };
    instance->m_audio_handler->PushFrames(frame, 1);
}

size_t AudioHandler::SampleBatchCallback(const int16_t* data, size_t frames)
{
    if (!data)
        return frames;

    auto instance = Wrapper::GetCurrentThreadWrapper();
    if (!instance)
    {
        LogError("SampleBatchCallback: Null Instance.");
        return frames;
    }

    AudioHandler* self = instance->m_audio_handler.get();

    // Rollback replay: drop re-run audio (already played on the first run).
    if (instance->IsNetplayReplaying())
        return frames;

    // Emulated-time clock — counted at the core's rate, so before resampling.
    self->m_frames_produced.fetch_add(frames, std::memory_order_relaxed);

    // Sink depth drives both the occupancy the core is told about and the rate trim
    // below, so read it once.
    const uint32_t queued = self->QueuedFrames();

    const uint32_t total = self->EffectiveTotalFrames();
    if (total > 0)
    {
        uint32_t occupancy = static_cast<uint32_t>(100.0f * static_cast<float>(queued) / static_cast<float>(total));
        if (occupancy > 100)
            occupancy = 100;
        self->m_audio_buffer_occupancy = occupancy;
        if (total > self->m_audio_buffer_total_frames)
            self->m_audio_buffer_total_frames = total;
    }

    // Dynamic rate control. The pacing brake is one-sided — it only ever holds the
    // core back when the sink is above target — so nothing stops the ring draining
    // when a heavy frame or a main-thread hitch outruns the target fill, and the
    // mixer gets a gap. Resampling fractionally fast while the sink is short and
    // fractionally slow while it is long corrects the depth continuously, instead of
    // only at the coarse grain of running or stalling a whole frame. It also absorbs
    // the drift between a core's nominal rate and the mixer's real one, which are
    // separate crystals and never exactly agree.
    double drc_adjust = 1.0;
    if (self->m_sink_target_frames > 0)
    {
        const double target = static_cast<double>(self->m_sink_target_frames);
        double direction = (target - static_cast<double>(queued)) / target;
        if (direction > 1.0)
            direction = 1.0;
        else if (direction < -1.0)
            direction = -1.0;
        drc_adjust = 1.0 + k_drc_max_delta * direction;
    }

    // s16 interleaved -> float interleaved.
    self->m_in_float.resize(frames * 2);
    for (size_t i = 0; i < frames * 2; ++i)
        self->m_in_float[i] = data[i] / 32768.0f;

    if (self->m_resampler_backend && self->m_resampler)
    {
        const double ratio = self->m_resample_ratio * drc_adjust;

        // Slack on the output: the sinc resampler can emit a frame or two more
        // than the ratio implies, depending on its internal phase. Sized off the
        // trimmed ratio, not the nominal one, or the trim can overrun the buffer.
        const size_t cap = static_cast<size_t>(frames * ratio) + 32;
        self->m_out_float.resize(cap * 2);

        struct resampler_data rd = {};
        rd.data_in      = self->m_in_float.data();
        rd.data_out     = self->m_out_float.data();
        rd.input_frames = frames;
        rd.ratio        = ratio;
        self->m_resampler_backend->process(self->m_resampler, &rd);
        self->PushFrames(self->m_out_float.data(), rd.output_frames);
    }
    else
    {
        self->PushFrames(self->m_in_float.data(), frames);
    }

    return frames;
}

void AudioHandler::PushFrames(const float* interleaved, size_t frames)
{
    if (frames == 0)
        return;

    if (m_use_sdk)
    {
        if (m_mx == nullptr || m_voice_l < 0)
            return;
        if (static_cast<size_t>(m_push_buf.size()) != frames)
            m_push_buf.resize(static_cast<int64_t>(frames));
        Vector2* dst = m_push_buf.ptrw();
        for (size_t i = 0; i < frames; ++i)
            dst[i] = Vector2(interleaved[i * 2], interleaved[i * 2 + 1]);
        m_mx->call("push_stereo_frames", m_voice_l, m_voice_r, m_push_buf,
                   m_channel_mode.load(std::memory_order_relaxed));
        return;
    }

    if (m_audio_stream_generator_playback.is_null())
        return;
    for (size_t i = 0; i < frames; ++i)
        m_audio_stream_generator_playback->push_frame(Vector2(interleaved[i * 2], interleaved[i * 2 + 1]));
}

uint32_t AudioHandler::QueuedFrames() const
{
    if (m_use_sdk)
    {
        if (m_mx == nullptr || m_voice_l < 0)
            return 0;
        const int q = static_cast<int>(m_mx->call("voice_frames_available", m_voice_l));
        return q > 0 ? static_cast<uint32_t>(q) : 0;
    }
    if (m_audio_stream_generator_playback.is_null())
        return 0;
    const int32_t avail = m_audio_stream_generator_playback->get_frames_available();
    const uint32_t total = m_audio_buffer_total_frames;
    return (avail >= 0 && total > static_cast<uint32_t>(avail)) ? total - static_cast<uint32_t>(avail) : 0;
}

double AudioHandler::MsUntilSinkWantsFrames() const
{
    if (m_mix_rate <= 0.0)
        return 0.0;

    if (m_use_sdk)
    {
        if (m_mx == nullptr || m_voice_l < 0)
            return 0.0;
        // The voice reports what it still wants against its own target fill, so the
        // target lives in one place rather than being restated here.
        if (static_cast<int>(m_mx->call("voice_frames_wanted", m_voice_l)) > 0)
            return 0.0;

        const int queued = static_cast<int>(m_mx->call("voice_frames_available", m_voice_l));
        const double over = static_cast<double>(queued) - static_cast<double>(m_sink_target_frames);
        return over > 0.0 ? 1000.0 * over / m_mix_rate : 0.0;
    }

    if (m_audio_stream_generator_playback.is_null())
        return 0.0;

    // The generator has no target of its own; treat its whole buffer as the target
    // and wait out whatever sits above the half mark.
    const int32_t avail = m_audio_stream_generator_playback->get_frames_available();
    if (avail > 0)
        return 0.0;
    return 500.0 * static_cast<double>(m_audio_buffer_total_frames) / m_mix_rate;
}

uint32_t AudioHandler::EffectiveTotalFrames() const
{
    // The voice ring is far larger than the generator ever was (0.68 s against
    // the 0.1 s this handler is initialised with). Pacing must NOT use the
    // physical ring size, or the core is allowed to run most of a second ahead
    // and the game feels laggy. Treat the queue as if it were still the old
    // buffer and let the rest of the ring serve as spare headroom.
    if (m_use_sdk)
        return static_cast<uint32_t>(m_audio_buffer_capacity_sec * m_mix_rate);
    return m_audio_buffer_total_frames;
}

void AudioHandler::Init(float buffer_capacity_sec, double sample_rate)
{
    m_audio_buffer_capacity_sec = buffer_capacity_sec;
    m_audio_sample_rate = sample_rate;
    m_frames_produced.store(0, std::memory_order_relaxed);

    AudioServer* audio = AudioServer::get_singleton();
    m_mix_rate = audio ? audio->get_mix_rate() : 48000.0;

    // Prefer Meta XR Audio; fall through silently when the extension or its
    // native library is absent, which is the point of having a fallback.
    m_mx = nullptr;
    m_use_sdk = false;
    Engine* engine = Engine::get_singleton();
    if (engine && engine->has_singleton("MetaXRAudio"))
    {
        Object* mx = engine->get_singleton("MetaXRAudio");
        if (mx && static_cast<bool>(mx->call("is_available")))
        {
            // Two voices: a console's sound comes out of a TV, and a TV has two
            // speakers. GDScript places them; this only owns their lifetime.
            const int l = static_cast<int>(mx->call("create_voice"));
            const int r = (l >= 0) ? static_cast<int>(mx->call("create_voice")) : -1;
            if (l >= 0)
            {
                m_mx = mx;
                m_voice_l = l;
                m_voice_r = r;
                m_use_sdk = true;
            }
        }
    }

    m_sink_target_frames = 0;
    if (m_use_sdk && m_mx)
    {
        const double target_ms = static_cast<double>(m_mx->call("get_target_latency_ms"));
        m_sink_target_frames = static_cast<uint32_t>(target_ms * m_mix_rate / 1000.0);
    }

    if (m_use_sdk)
    {
        // Allocated even when the rates already match. Rate control steers the sink's
        // depth by trimming this ratio, so a core running at the mixer's own rate
        // needs the resampler present at 1:1 or there is no knob to turn. It costs a
        // sinc pass those cores did not pay before.
        const bool rates_differ = m_audio_sample_rate > 0.0 && m_mix_rate > 0.0
                               && static_cast<int>(m_audio_sample_rate) != static_cast<int>(m_mix_rate);

        if (m_audio_sample_rate > 0.0 && m_mix_rate > 0.0)
        {
            m_resample_ratio = m_mix_rate / m_audio_sample_rate;
            if (!retro_resampler_realloc(&m_resampler, &m_resampler_backend, "sinc",
                                         RESAMPLER_QUALITY_NORMAL, m_resample_ratio))
            {
                m_resampler = nullptr;
                m_resampler_backend = nullptr;

                if (rates_differ)
                {
                    // Mismatched rates cannot be pushed straight through, so the SDK
                    // path has to be given up entirely, as it always was.
                    LogWarning("AudioHandler: resampler init failed, using Godot panning instead.");
                    m_mx->call("destroy_voice", m_voice_l);
                    if (m_voice_r >= 0)
                        m_mx->call("destroy_voice", m_voice_r);
                    m_voice_l = m_voice_r = -1;
                    m_mx = nullptr;
                    m_use_sdk = false;
                }
                else
                {
                    // At 1:1 the audio still plays correctly straight through; only
                    // the rate trim is lost. Not worth abandoning the spatial path for.
                    LogWarning("AudioHandler: resampler init failed at 1:1, running without rate control.");
                }
            }
        }
        if (m_use_sdk)
        {
            Log("AudioHandler: Meta XR Audio, core " + std::to_string(static_cast<int>(m_audio_sample_rate))
                + " Hz -> " + std::to_string(static_cast<int>(m_mix_rate)) + " Hz, voices "
                + std::to_string(m_voice_l) + "/" + std::to_string(m_voice_r));
            return;
        }
    }

    m_audio_stream_generator.instantiate();
    m_audio_stream_generator->set_mix_rate(m_audio_sample_rate);
    m_audio_stream_generator->set_buffer_length(m_audio_buffer_capacity_sec);

    m_audio_stream_player = Wrapper::GetCurrentThreadWrapper()->m_libretro_node->get_node<godot::AudioStreamPlayer3D>("AudioStreamPlayer3D");
    m_audio_stream_player->set_stream(m_audio_stream_generator);
    m_audio_stream_player->play();

    m_audio_stream_generator_playback = m_audio_stream_player->get_stream_playback();
}

void AudioHandler::DeInit()
{
    if (m_use_sdk && m_mx)
    {
        if (m_voice_l >= 0)
            m_mx->call("destroy_voice", m_voice_l);
        if (m_voice_r >= 0)
            m_mx->call("destroy_voice", m_voice_r);
    }
    m_voice_l = m_voice_r = -1;
    m_mx = nullptr;
    m_use_sdk = false;

    if (m_resampler && m_resampler_backend)
        m_resampler_backend->free(m_resampler);
    m_resampler = nullptr;
    m_resampler_backend = nullptr;

    if (m_audio_stream_player)
    {
        m_audio_stream_player->stop();
        Wrapper::GetCurrentThreadWrapper()->m_libretro_node->remove_child(m_audio_stream_player);
        m_audio_stream_player = nullptr;
    }

    if (m_audio_stream_generator_playback.is_valid())
    {
        m_audio_stream_generator_playback->stop();
        m_audio_stream_generator_playback.unref();
    }

    if (m_audio_stream_generator.is_valid())
        m_audio_stream_generator.unref();
}

void AudioHandler::SetPlaying(bool playing)
{
    if (m_use_sdk)
    {
        // Nothing to start or stop — a voice with an empty ring is silent. Only
        // flush, so a stale tail cannot replay when the core resumes.
        if (!playing && m_mx && m_voice_l >= 0)
        {
            m_mx->call("flush_voice", m_voice_l);
            if (m_voice_r >= 0)
                m_mx->call("flush_voice", m_voice_r);
        }
        return;
    }

    if (!m_audio_stream_player)
        return;
    if (playing)
    {
        m_audio_stream_player->play();
        m_audio_stream_generator_playback = m_audio_stream_player->get_stream_playback();
    }
    else
    {
        m_audio_stream_player->stop();
        m_audio_stream_generator_playback.unref();
    }
}

PackedInt32Array AudioHandler::GetVoiceIds() const
{
    PackedInt32Array ids;
    if (m_use_sdk && m_voice_l >= 0)
    {
        ids.push_back(m_voice_l);
        if (m_voice_r >= 0)
            ids.push_back(m_voice_r);
    }
    return ids;
}

bool AudioHandler::SetAudioBufferStatusCallback(const retro_audio_buffer_status_callback* callback)
{
    m_audio_buffer_status_callback = callback ? callback->callback : nullptr;
    return true;
}

bool AudioHandler::SetMinimumAudioLatency(const uint32_t* minimum_audio_latency)
{
    if (minimum_audio_latency)
        m_minimum_audio_latency = *minimum_audio_latency;
    return true;
}

void AudioHandler::CallAudioBufferStatusCallback()
{
    if (m_audio_buffer_status_callback)
        m_audio_buffer_status_callback(true, m_audio_buffer_occupancy, m_audio_buffer_occupancy <= 10);
}
}
