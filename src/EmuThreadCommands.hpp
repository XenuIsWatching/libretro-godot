#pragma once

#include <cstdint>
#include <string>
#include <godot_cpp/variant/packed_byte_array.hpp>

namespace Xenu
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

/// retro_set_controller_port_device on the emulation thread: a live plug/
/// unplug while the core runs must not call into the core from the main
/// thread mid-retro_run.
class EmuThreadCommandSetPortDevice : public EmuThreadCommand
{
public:
    EmuThreadCommandSetPortDevice(uint32_t port, uint32_t device)
        : m_port(port)
        , m_device(device)
    {
    }

    void Execute(Wrapper& wrapper) override;

private:
    uint32_t m_port;
    uint32_t m_device;
};

/// Hot-swap the SRAM backing file mid-run (a physical memory-card swap):
/// flush the old file, point at the new path, load its content into SAVE_RAM.
class EmuThreadCommandSetSram : public EmuThreadCommand
{
public:
    explicit EmuThreadCommandSetSram(std::string path)
        : m_path(std::move(path))
    {
    }

    void Execute(Wrapper& wrapper) override;

private:
    std::string m_path;
};

/// Dirty-check flush of SRAM to its backing file, on demand.
class EmuThreadCommandFlushSram : public EmuThreadCommand
{
public:
    void Execute(Wrapper& wrapper) override;
};

/// Read the core's disk-control state and emit
/// disk_control_ready(has_control, count, current_index, ejected).
class EmuThreadCommandDiskInfo : public EmuThreadCommand
{
public:
    void Execute(Wrapper& wrapper) override;
};

/// Open/close the core's virtual disc tray (set_eject_state), then re-emit
/// disk_control_ready with the updated state.
class EmuThreadCommandSetDiskEjected : public EmuThreadCommand
{
public:
    explicit EmuThreadCommandSetDiskEjected(bool ejected)
        : m_ejected(ejected)
    {
    }

    void Execute(Wrapper& wrapper) override;

private:
    bool m_ejected;
};

/// Hand the core a brand-new disc file at image `index`
/// (replace_image_index, RetroArch's "Load New Disc"), then re-emit
/// disk_control_ready. The tray must be open; the caller closes it after.
class EmuThreadCommandReplaceDisk : public EmuThreadCommand
{
public:
    EmuThreadCommandReplaceDisk(uint32_t index, std::string path)
        : m_index(index)
        , m_path(std::move(path))
    {
    }

    void Execute(Wrapper& wrapper) override;

private:
    uint32_t m_index;
    std::string m_path;
};
}
