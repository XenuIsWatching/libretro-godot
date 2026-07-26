#pragma once

#include "ThreadCommand.hpp"

namespace Xenu
{
class Wrapper;

class ThreadCommandInitAudio : public ThreadCommand
{
public:
    ThreadCommandInitAudio(Wrapper* wrapper, float bufferCapacitySec, double sampleRate);
    ~ThreadCommandInitAudio() override = default;

    void Execute() override;

private:
    Wrapper* m_wrapper;
    float m_bufferCapacitySec;
    double m_sampleRate;
};
}
