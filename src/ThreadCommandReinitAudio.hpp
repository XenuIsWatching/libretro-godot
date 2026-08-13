#pragma once

#include "ThreadCommand.hpp"

namespace Xenu
{
class Wrapper;

/// Reconfigure the audio backend on Godot's main thread for
/// RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO without replacing its player/voices.
class ThreadCommandReinitAudio : public ThreadCommand
{
public:
    ThreadCommandReinitAudio(Wrapper* wrapper, double new_sample_rate)
        : m_wrapper(wrapper)
        , m_new_sample_rate(new_sample_rate)
    {
    }

    void Execute() override;

private:
    Wrapper* m_wrapper;
    double m_new_sample_rate;
};
}
