#pragma once

namespace SK
{
class ThreadCommand
{
public:
    ThreadCommand() = default;
    virtual ~ThreadCommand() = default;

    virtual void Execute() = 0;
};
}
