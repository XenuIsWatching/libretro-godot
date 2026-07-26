#pragma once

namespace Xenu
{
class ThreadCommand
{
public:
    ThreadCommand() = default;
    virtual ~ThreadCommand() = default;

    virtual void Execute() = 0;
};
}
