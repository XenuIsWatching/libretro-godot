#include "ThreadCommandEmitSignal.hpp"

#include "Wrapper.hpp"
#include "Libretro.hpp"

namespace SK
{
void ThreadCommandEmitSignal::Execute()
{
    if (m_wrapper == nullptr || m_wrapper->m_libretro_node == nullptr)
        return;

    godot::Array call_args;
    call_args.append(m_signal_name);
    for (int i = 0; i < m_args.size(); ++i)
        call_args.append(m_args[i]);
    m_wrapper->m_libretro_node->callv("emit_signal", call_args);
}
}
