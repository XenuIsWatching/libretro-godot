#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <libretro.h>

namespace SK
{
class Wrapper;

class EnvironmentHandler
{
public:
    EnvironmentHandler();
    ~EnvironmentHandler() = default;

    static bool Callback(uint32_t cmd, void* data);

    void SetDirectories(const std::string& system_directory, const std::string& save_directory, const std::string& core_assets_directory);
    
private:
    static const uint32_t s_supported_vfs_version = 3;

    uint32_t m_performance_level = 0;
    std::string m_system_directory;
    std::string m_save_directory;
    std::string m_core_assets_directory;
    retro_vfs_interface m_vfs_interface;
    retro_disk_control_callback m_disk_control_callback = {};
    retro_disk_control_ext_callback m_disk_control_ext_callback = {};

    bool SetPerformanceLevel(uint32_t* level);
    bool GetSystemDirectory(const char** directory);
    bool GetSaveDirectory(const char** directory);
    bool GetCoreAssetsDirectory(const char** directory);
    bool SetDiskControlInterface(const retro_disk_control_callback* callback);
    bool GetPerfInterface(retro_perf_callback* callback);
    bool SetSystemAvInfo(const retro_system_av_info* av_info);
    bool SetSubsystemInfo(const retro_subsystem_info* subsystem_info);
    bool SetMemoryMaps(const retro_memory_map* memory_maps);
    bool GetUsername(const char** username) const;
    bool GetLanguage(retro_language* language) const;
    bool SetSupportAchievements(bool* support);
    bool GetVfsInterface(retro_vfs_interface_info* vfs_interface_info);
    bool GetLedInterface(retro_led_interface* led_interface);
    bool GetAudioVideoEnable(retro_av_enable_flags* flags);
    bool GetFastForwarding(bool* fast_forwarding);
    bool GetDiskControlInterfaceVersion(uint32_t* version);
    bool SetDiskControlExtInterface(const retro_disk_control_ext_callback* callback);
    bool GetThrottleState(retro_throttle_state* state);
    bool GetClearAllThreadWaitsCb(retro_environment_t* env);
};
}
