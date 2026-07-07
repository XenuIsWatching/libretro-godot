#include "EmuThreadCommands.hpp"

#include "Wrapper.hpp"
#include "Debug.hpp"

namespace SK
{
void EmuThreadCommandSaveState::Execute(Wrapper& wrapper)
{
    godot::PackedByteArray buffer;
    bool ok = false;
    int64_t frame = wrapper.m_frame_counter.load(std::memory_order_relaxed);

    if (wrapper.m_core && wrapper.m_core->retro_serialize_size && wrapper.m_core->retro_serialize)
    {
        size_t size = wrapper.m_core->retro_serialize_size();
        if (size > 0)
        {
            buffer.resize(static_cast<int64_t>(size));
            ok = wrapper.m_core->retro_serialize(buffer.ptrw(), size);
        }
    }

    if (!ok)
    {
        LogWarning("SaveState failed (core does not support serialization?)");
        buffer = godot::PackedByteArray();
    }

    godot::Array args;
    args.append(buffer);
    args.append(frame);
    wrapper.EmitSignalOnMainThread("savestate_ready", args);
}

void EmuThreadCommandLoadState::Execute(Wrapper& wrapper)
{
    bool ok = false;
    if (wrapper.m_core && wrapper.m_core->retro_unserialize && m_data.size() > 0)
        ok = wrapper.m_core->retro_unserialize(m_data.ptr(), static_cast<size_t>(m_data.size()));

    if (ok)
    {
        // Restart the netplay schedule from the state's frame — including the
        // rollback bookkeeping (this runs on the emulation thread, which owns
        // the states/used/crc structures).
        wrapper.m_np_states.clear();
        wrapper.m_np_used.clear();
        wrapper.m_np_crc_pending.clear();
        wrapper.m_np_watermark = m_frame - 1;
        wrapper.m_np_verified = m_frame - 1;
        std::lock_guard<std::mutex> lock(wrapper.m_np_mutex);
        wrapper.m_np_inputs.clear();
        wrapper.m_np_local_records.clear();
        wrapper.m_frame_counter.store(m_frame, std::memory_order_relaxed);
    }
    else
    {
        LogWarning("LoadState failed");
    }

    godot::Array args;
    args.append(ok);
    wrapper.EmitSignalOnMainThread("savestate_loaded", args);
}

void EmuThreadCommandSetSram::Execute(Wrapper& wrapper)
{
    wrapper.ApplySramSwap(m_path);
}

void EmuThreadCommandFlushSram::Execute(Wrapper& wrapper)
{
    wrapper.FlushSramIfDirty();
}
}
