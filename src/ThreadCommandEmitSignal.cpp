#include "ThreadCommandEmitSignal.hpp"

#include "Wrapper.hpp"
#include "Libretro.hpp"

namespace Xenu
{
void ThreadCommandEmitSignal::Execute()
{
    Libretro* node = m_wrapper ? m_wrapper->LiveLibretroNode() : nullptr;
    if (node == nullptr)
        return;

    godot::Array call_args;
    call_args.append(m_signal_name);
    for (int i = 0; i < m_args.size(); ++i)
        call_args.append(m_args[i]);
    node->callv("emit_signal", call_args);
}
}
