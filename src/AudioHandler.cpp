#include "AudioHandler.hpp"

#include <godot_cpp/variant/vector2.hpp>

#include "Wrapper.hpp"
#include "Libretro.hpp"
#include "Debug.hpp"

using namespace godot;

namespace SK
{
void AudioHandler::SampleCallback(int16_t left, int16_t right)
{
    auto instance = Wrapper::GetCurrentThreadWrapper();
    if (!instance)
    {
        LogError("SampleCallback: Null Instance.");
        return;
    }

    if (!instance->m_audio_handler->m_audio_stream_generator_playback.is_valid())
        return;

    float l = left / 32768.0f;
    float r = right / 32768.0f;
    instance->m_audio_handler->m_audio_stream_generator_playback->push_frame(Vector2(l, r));
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

    if (instance->m_audio_handler->m_audio_stream_generator_playback.is_null())
        return frames;

    auto available_frames = instance->m_audio_handler->m_audio_stream_generator_playback->get_frames_available();

    if (available_frames > instance->m_audio_handler->m_audio_buffer_total_frames)
        instance->m_audio_handler->m_audio_buffer_total_frames = available_frames;

    auto total_frames = instance->m_audio_handler->m_audio_buffer_total_frames;
    uint32_t occupancy_percent = 0;
    if (total_frames > 0) {
        occupancy_percent = static_cast<uint32_t>(100.0f * (1.0f - static_cast<float>(available_frames) / static_cast<float>(total_frames)));
        if (occupancy_percent > 100)
            occupancy_percent = 100;
    }

    instance->m_audio_handler->m_audio_buffer_occupancy = occupancy_percent;

    for (size_t i = 0; i < frames; ++i)
    {
        float l = data[i * 2]     / 32768.0f;
        float r = data[i * 2 + 1] / 32768.0f;
        instance->m_audio_handler->m_audio_stream_generator_playback->push_frame(Vector2(l, r));
    }

    return frames;
}

void AudioHandler::Init(float buffer_capacity_sec, double sample_rate)
{
    m_audio_buffer_capacity_sec = buffer_capacity_sec;
    m_audio_sample_rate = sample_rate;

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
