#pragma once

#include <cstdint>
#include <godot_cpp/variant/packed_byte_array.hpp>

namespace SK
{
class Wrapper;

/// Commands posted by the main thread and executed on the EMULATION thread,
/// strictly between retro_run() calls (drained at the top of each loop
/// iteration). Mirror of ThreadCommand, which flows the other direction.
class EmuThreadCommand
{
public:
    EmuThreadCommand() = default;
    virtual ~EmuThreadCommand() = default;

    virtual void Execute(Wrapper& wrapper) = 0;
};

/// retro_serialize the core on the emulation thread, then emit
/// savestate_ready(data, frame) on the main thread (empty data on failure).
class EmuThreadCommandSaveState : public EmuThreadCommand
{
public:
    void Execute(Wrapper& wrapper) override;
};

/// retro_unserialize a savestate on the emulation thread, reset the netplay
/// input schedule and frame counter, then emit savestate_loaded(ok).
class EmuThreadCommandLoadState : public EmuThreadCommand
{
public:
    EmuThreadCommandLoadState(godot::PackedByteArray data, int64_t frame)
        : m_data(std::move(data))
        , m_frame(frame)
    {
    }

    void Execute(Wrapper& wrapper) override;

private:
    godot::PackedByteArray m_data;
    int64_t m_frame;
};
}
