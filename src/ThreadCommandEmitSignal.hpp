#pragma once

#include "ThreadCommand.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace SK
{
class Wrapper;

/// Emits a signal on the owning Libretro node from the main thread. Queued by
/// the emulation thread (savestate results, netplay CRCs) and executed during
/// Wrapper::_process's command drain — the same marshaling pattern as
/// NotifyOptionsReady.
class ThreadCommandEmitSignal : public ThreadCommand
{
public:
    ThreadCommandEmitSignal(Wrapper* wrapper, const godot::StringName& signal_name, const godot::Array& args)
        : m_wrapper(wrapper)
        , m_signal_name(signal_name)
        , m_args(args)
    {
    }

    void Execute() override;

private:
    Wrapper* m_wrapper;
    godot::StringName m_signal_name;
    godot::Array m_args;
};
}
