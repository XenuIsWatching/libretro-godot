#pragma once

namespace Xenu
{
class ThreadCommand
{
public:
    ThreadCommand() = default;
    virtual ~ThreadCommand() = default;

    virtual void Execute() = 0;

    /// True for a command that paints the screen with one frame's pixels. Only
    /// the newest such command in a drain is worth executing: the rest are
    /// frames the player will never see, and the main thread cannot draw more
    /// of them per frame than it draws frames.
    virtual bool IsFrameUpload() const { return false; }
};
}
