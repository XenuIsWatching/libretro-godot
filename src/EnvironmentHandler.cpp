#include "EnvironmentHandler.hpp"

#include <godot_cpp/variant/string.hpp>

#include "Wrapper.hpp"
#include "Debug.hpp"
#include "InputHandler.hpp"

#include <filesystem>

#define VFS_FRONTEND
#include <vfs/vfs_implementation.h>
#include <features/features_cpu.h>

using namespace godot;

namespace SK
{
#define RETRO_ENVIRONMENT_RETROARCH_START_BLOCK 0x800000
#define RETRO_ENVIRONMENT_SET_SAVE_STATE_IN_BACKGROUND (2 | RETRO_ENVIRONMENT_RETROARCH_START_BLOCK)
#define RETRO_ENVIRONMENT_GET_CLEAR_ALL_THREAD_WAITS_CB (3 | RETRO_ENVIRONMENT_RETROARCH_START_BLOCK)
#define RETRO_ENVIRONMENT_POLL_TYPE_OVERRIDE (4 | RETRO_ENVIRONMENT_RETROARCH_START_BLOCK)

static retro_time_t retro_perf_get_time_usec()
{
    return cpu_features_get_time_usec();
}

static uint64_t retro_get_cpu_features()
{
    return cpu_features_get();
}

static retro_perf_tick_t retro_perf_get_counter()
{
    return cpu_features_get_perf_counter();
}

static void retro_perf_register(retro_perf_counter* counter)
{
}

static void retro_perf_start(retro_perf_counter* counter)
{
}

static void retro_perf_stop(retro_perf_counter* counter)
{
}

static void retro_perf_log()
{
}

static bool runloop_clear_all_thread_waits(uint32_t clear_threads, void* data)
{
    if (clear_threads > 0)
    {
        // start audio
    }
    else
    {
        // stop audio
    }

    return true;
}

#define ENVIRONMENT_COMMANDS \
    X(RETRO_ENVIRONMENT_EXPERIMENTAL) \
    X(RETRO_ENVIRONMENT_PRIVATE) \
    X(RETRO_ENVIRONMENT_SET_ROTATION) \
    X(RETRO_ENVIRONMENT_GET_OVERSCAN) \
    X(RETRO_ENVIRONMENT_GET_CAN_DUPE) \
    X(RETRO_ENVIRONMENT_SET_MESSAGE) \
    X(RETRO_ENVIRONMENT_SHUTDOWN) \
    X(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL) \
    X(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY) \
    X(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) \
    X(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS) \
    X(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK) \
    X(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE) \
    X(RETRO_ENVIRONMENT_SET_HW_RENDER) \
    X(RETRO_ENVIRONMENT_GET_VARIABLE) \
    X(RETRO_ENVIRONMENT_SET_VARIABLES) \
    X(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE) \
    X(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME) \
    X(RETRO_ENVIRONMENT_GET_LIBRETRO_PATH) \
    X(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK) \
    X(RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK) \
    X(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE) \
    X(RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES) \
    X(RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE) \
    X(RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE) \
    X(RETRO_ENVIRONMENT_GET_LOG_INTERFACE) \
    X(RETRO_ENVIRONMENT_GET_PERF_INTERFACE) \
    X(RETRO_ENVIRONMENT_GET_LOCATION_INTERFACE) \
    X(RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY) \
    X(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY) \
    X(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO) \
    X(RETRO_ENVIRONMENT_SET_PROC_ADDRESS_CALLBACK) \
    X(RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO) \
    X(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO) \
    X(RETRO_ENVIRONMENT_SET_MEMORY_MAPS) \
    X(RETRO_ENVIRONMENT_SET_GEOMETRY) \
    X(RETRO_ENVIRONMENT_GET_USERNAME) \
    X(RETRO_ENVIRONMENT_GET_LANGUAGE) \
    X(RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER) \
    X(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE) \
    X(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS) \
    X(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE) \
    X(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS) \
    X(RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT) \
    X(RETRO_ENVIRONMENT_GET_VFS_INTERFACE) \
    X(RETRO_ENVIRONMENT_GET_LED_INTERFACE) \
    X(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE) \
    X(RETRO_ENVIRONMENT_GET_MIDI_INTERFACE) \
    X(RETRO_ENVIRONMENT_GET_FASTFORWARDING) \
    X(RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE) \
    X(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS) \
    X(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION) \
    X(RETRO_ENVIRONMENT_SET_CORE_OPTIONS) \
    X(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL) \
    X(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY) \
    X(RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER) \
    X(RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION) \
    X(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE) \
    X(RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION) \
    X(RETRO_ENVIRONMENT_SET_MESSAGE_EXT) \
    X(RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS) \
    X(RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK) \
    X(RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY) \
    X(RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE) \
    X(RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE) \
    X(RETRO_ENVIRONMENT_GET_GAME_INFO_EXT) \
    X(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2) \
    X(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL) \
    X(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK) \
    X(RETRO_ENVIRONMENT_SET_VARIABLE) \
    X(RETRO_ENVIRONMENT_GET_THROTTLE_STATE) \
    X(RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT) \
    X(RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT) \
    X(RETRO_ENVIRONMENT_GET_JIT_CAPABLE) \
    X(RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE) \
    X(RETRO_ENVIRONMENT_GET_DEVICE_POWER) \
    X(RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE) \
    X(RETRO_ENVIRONMENT_GET_PLAYLIST_DIRECTORY) \
    X(RETRO_ENVIRONMENT_GET_FILE_BROWSER_START_DIRECTORY) \
    X(RETRO_ENVIRONMENT_RETROARCH_START_BLOCK) \
    X(RETRO_ENVIRONMENT_SET_SAVE_STATE_IN_BACKGROUND) \
    X(RETRO_ENVIRONMENT_GET_CLEAR_ALL_THREAD_WAITS_CB) \
    X(RETRO_ENVIRONMENT_POLL_TYPE_OVERRIDE)

static std::string EnvironmentString(uint32_t cmd)
{
    switch (cmd)
    {
#define X(name) case name: return #name;
        ENVIRONMENT_COMMANDS
#undef X
    default:
        return "Unknown Environment Command: " + std::to_string(cmd);
    }
}

static bool EnvironmentNotImplemented(uint32_t cmd, bool log = true, bool default_returns = false)
{
    if (!log)
        return default_returns;

    if (default_returns)
        LogWarning("Environment not implemented: " + EnvironmentString(cmd));
    else
        LogError("Environment not implemented: " + EnvironmentString(cmd));
    return default_returns;
}

static bool EnvironmentUnknown(uint32_t cmd)
{
    LogError("Environment unknown: " + EnvironmentString(cmd));
    return false;
}

EnvironmentHandler::EnvironmentHandler()
{
    m_vfs_interface =
    {
        retro_vfs_file_get_path_impl,
        retro_vfs_file_open_impl,
        retro_vfs_file_close_impl,
        retro_vfs_file_size_impl,
        retro_vfs_file_tell_impl,
        retro_vfs_file_seek_impl,
        retro_vfs_file_read_impl,
        retro_vfs_file_write_impl,
        retro_vfs_file_flush_impl,
        retro_vfs_file_remove_impl,
        retro_vfs_file_rename_impl,
        retro_vfs_file_truncate_impl,
        retro_vfs_stat_impl,
        retro_vfs_mkdir_impl,
        retro_vfs_opendir_impl,
        retro_vfs_readdir_impl,
        retro_vfs_dirent_get_name_impl,
        retro_vfs_dirent_is_dir_impl,
        retro_vfs_closedir_impl
    };    
}

bool EnvironmentHandler::Callback(uint32_t cmd, void* data)
{
    auto instance = Wrapper::GetCurrentThreadWrapper();
    if (!instance)
    {
        LogError("Callback: Null Instance.");
        return false;
    }

    switch (cmd)
    {
    case RETRO_ENVIRONMENT_SET_ROTATION:                                        return instance->m_video_handler->SetRotation(*static_cast<uint32_t*>(data));
    case RETRO_ENVIRONMENT_GET_OVERSCAN:                                        return instance->m_video_handler->GetOverscan(static_cast<int32_t*>(data));
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:                                        return instance->m_video_handler->GetCanDupe(static_cast<bool*>(data));
    case RETRO_ENVIRONMENT_SET_MESSAGE:                                         return instance->m_message_handler->SetMessage(static_cast<const retro_message*>(data));
    case RETRO_ENVIRONMENT_SHUTDOWN:                                            return instance->Shutdown();
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:                               return instance->m_environment_handler->SetPerformanceLevel(static_cast<uint32_t*>(data));
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:                                return instance->m_environment_handler->GetSystemDirectory(static_cast<const char**>(data));
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:                                    return instance->m_video_handler->SetPixelFormat(static_cast<const retro_pixel_format*>(data));
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:                               return instance->m_input_handler->SetInputDescriptors(static_cast<const retro_input_descriptor*>(data));
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:                               return instance->m_input_handler->SetKeyboardEventCallback(static_cast<const retro_keyboard_callback*>(data));
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:                          return instance->m_environment_handler->SetDiskControlInterface(static_cast<const retro_disk_control_callback*>(data));
    case RETRO_ENVIRONMENT_SET_HW_RENDER:                                       return instance->m_video_handler->SetHwRender(static_cast<retro_hw_render_callback*>(data));
    case RETRO_ENVIRONMENT_GET_VARIABLE:                                        return instance->m_options_handler->GetVariable(static_cast<retro_variable*>(data));
    case RETRO_ENVIRONMENT_SET_VARIABLES:                                       return instance->m_options_handler->SetVariables(static_cast<const retro_variable*>(data));
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:                                 return instance->m_options_handler->GetVariableUpdate(static_cast<bool*>(data));
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:                                 return instance->m_core->SetSupportsNoGame(static_cast<bool*>(data));
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:                                   return instance->m_core->GetLibretroPath(static_cast<const char**>(data));
    case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK:                             return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK:                                  return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:                                return instance->m_input_handler->GetRumbleInterface(static_cast<retro_rumble_interface*>(data));
    case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:                       return instance->m_input_handler->GetInputDeviceCapabilities(static_cast<uint32_t*>(data));
    case RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE:                                return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE:                                return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:                                   return instance->m_log_handler->GetLogInterface(static_cast<retro_log_callback*>(data));
    case RETRO_ENVIRONMENT_GET_PERF_INTERFACE:                                  return instance->m_environment_handler->GetPerfInterface(static_cast<retro_perf_callback*>(data));
    case RETRO_ENVIRONMENT_GET_LOCATION_INTERFACE:                              return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:                           return instance->m_environment_handler->GetCoreAssetsDirectory(static_cast<const char**>(data));
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:                                  return instance->m_environment_handler->GetSaveDirectory(static_cast<const char**>(data));
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:                                  return instance->m_environment_handler->SetSystemAvInfo(static_cast<const retro_system_av_info*>(data));
    case RETRO_ENVIRONMENT_SET_PROC_ADDRESS_CALLBACK:                           return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:                                  return instance->m_environment_handler->SetSubsystemInfo(static_cast<const retro_subsystem_info*>(data));
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:                                 return instance->m_input_handler->SetControllerInfo(static_cast<const retro_controller_info*>(data));
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:                                     return instance->m_environment_handler->SetMemoryMaps(static_cast<const retro_memory_map*>(data));
    case RETRO_ENVIRONMENT_SET_GEOMETRY:                                        return instance->m_video_handler->SetGeometry(static_cast<const retro_game_geometry*>(data));
    case RETRO_ENVIRONMENT_GET_USERNAME:                                        return instance->m_environment_handler->GetUsername(static_cast<const char**>(data));
    case RETRO_ENVIRONMENT_GET_LANGUAGE:                                        return instance->m_environment_handler->GetLanguage(static_cast<retro_language*>(data));
    case RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER:                    return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:                             return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:                            return instance->m_environment_handler->SetSupportAchievements(static_cast<bool*>(data));
    case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:         return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:                            return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT:                               return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:                                   return instance->m_environment_handler->GetVfsInterface(static_cast<retro_vfs_interface_info*>(data));
    case RETRO_ENVIRONMENT_GET_LED_INTERFACE:                                   return instance->m_environment_handler->GetLedInterface(static_cast<retro_led_interface*>(data));
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:                              return instance->m_environment_handler->GetAudioVideoEnable(static_cast<retro_av_enable_flags*>(data));
    case RETRO_ENVIRONMENT_GET_MIDI_INTERFACE:                                  return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING:                                  return instance->m_environment_handler->GetFastForwarding(static_cast<bool*>(data));
    case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE:                             return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:                                  return instance->m_input_handler->GetInputBitmasks(static_cast<bool*>(data));
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:                            return instance->m_options_handler->GetCoreOptionsVersion(static_cast<uint32_t*>(data));
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:                                    return instance->m_options_handler->SetCoreOptions(static_cast<const retro_core_option_definition*>(data));
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:                               return instance->m_options_handler->SetCoreOptions(static_cast<const retro_core_options_intl*>(data));
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:                            return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:                             return instance->m_video_handler->GetPreferredHwRender(static_cast<retro_hw_context_type*>(data));
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:                  return instance->m_environment_handler->GetDiskControlInterfaceVersion(static_cast<uint32_t*>(data));
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:                      return instance->m_environment_handler->SetDiskControlExtInterface(static_cast<const retro_disk_control_ext_callback*>(data));
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:                       return instance->m_message_handler->GetMessageInterfaceVersion(static_cast<uint32_t*>(data));
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:                                     return instance->m_message_handler->SetMessageExt(static_cast<const retro_message_ext*>(data));
    case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS:                                 return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:                    return instance->m_audio_handler->SetAudioBufferStatusCallback(static_cast<const retro_audio_buffer_status_callback*>(data));
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:                           return instance->m_audio_handler->SetMinimumAudioLatency(static_cast<const uint32_t*>(data));
    case RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE:                         return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:                           return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:                                   return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:                                 return instance->m_options_handler->SetCoreOptionsV2(static_cast<const retro_core_options_v2*>(data));
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:                            return instance->m_options_handler->SetCoreOptionsV2Intl(static_cast<const retro_core_options_v2_intl*>(data));
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:            return instance->m_options_handler->SetCoreOptionsUpdateDisplayCallback(static_cast<const retro_core_options_update_display_callback*>(data));
    case RETRO_ENVIRONMENT_SET_VARIABLE:                                        return instance->m_options_handler->SetVariable(static_cast<const retro_variable*>(data));
    case RETRO_ENVIRONMENT_GET_THROTTLE_STATE:                                  return instance->m_environment_handler->GetThrottleState(static_cast<retro_throttle_state*>(data));
    case RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT:                               return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT: return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_JIT_CAPABLE:                                     return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE:                            return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_DEVICE_POWER:                                    return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:                             return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_PLAYLIST_DIRECTORY:                              return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_FILE_BROWSER_START_DIRECTORY:                    return EnvironmentNotImplemented(cmd);
    // custom environment commands
    case RETRO_ENVIRONMENT_SET_SAVE_STATE_IN_BACKGROUND:                        return EnvironmentNotImplemented(cmd);
    case RETRO_ENVIRONMENT_GET_CLEAR_ALL_THREAD_WAITS_CB:                       return instance->m_environment_handler->GetClearAllThreadWaitsCb(static_cast<retro_environment_t*>(data));
    case RETRO_ENVIRONMENT_POLL_TYPE_OVERRIDE:                                  return EnvironmentNotImplemented(cmd);
    default:                                                                    return EnvironmentUnknown(cmd);
    }

    return true;
}

void EnvironmentHandler::SetDirectories(const std::string& system_directory, const std::string& save_directory, const std::string& core_assets_directory)
{
    m_system_directory = system_directory;
    m_save_directory = save_directory;
    m_core_assets_directory = core_assets_directory;
}

bool EnvironmentHandler::SetPerformanceLevel(uint32_t* level)
{
    if (!level)
        return true;

    m_performance_level = *static_cast<uint32_t*>(level);
    return true;
}

bool EnvironmentHandler::GetSystemDirectory(const char** directory)
{
    if (!directory)
    {
        LogError("Passed null directory.");
        return false;
    }

    if (!std::filesystem::is_directory(m_system_directory))
    {
        std::error_code ec;
        std::filesystem::create_directories(m_system_directory, ec);
        if (ec)
        {
            LogError("Failed to create system directory: " + m_system_directory + " - " + ec.message());
            return false;
        }
    }

    *directory = m_system_directory.c_str();
    return true;
}

bool EnvironmentHandler::GetSaveDirectory(const char** directory)
{
    if (!directory)
    {
        LogError("Passed null directory.");
        return false;
    }

    if (!std::filesystem::is_directory(m_save_directory))
    {
        std::error_code ec;
        std::filesystem::create_directories(m_save_directory, ec);
        if (ec)
        {
            LogError("Failed to create save directory: " + m_save_directory + " - " + ec.message());
            return false;
        }
    }

    *directory = m_save_directory.c_str();
    return true;
}

bool EnvironmentHandler::GetCoreAssetsDirectory(const char** directory)
{
    if (!directory)
    {
        LogError("Passed null directory.");
        return false;
    }

    if (!std::filesystem::is_directory(m_core_assets_directory))
    {
        std::error_code ec;
        std::filesystem::create_directories(m_core_assets_directory, ec);
        if (ec)
        {
            LogError("Failed to create core assets directory: " + m_core_assets_directory + " - " + ec.message());
            return false;
        }
    }

    *directory = m_core_assets_directory.c_str();
    return true;
}

bool EnvironmentHandler::SetDiskControlInterface(const retro_disk_control_callback* callback)
{
    if (!callback)
    {
        m_disk_control_callback = {};
        return true;
    }

    m_disk_control_callback = *callback;
    return true;
}

bool EnvironmentHandler::GetPerfInterface(retro_perf_callback* callback)
{
    if (!callback)
        return true;

    callback->get_time_usec = retro_perf_get_time_usec;
    callback->get_cpu_features = retro_get_cpu_features;
    callback->get_perf_counter = retro_perf_get_counter;
    callback->perf_register = retro_perf_register;
    callback->perf_start = retro_perf_start;
    callback->perf_stop = retro_perf_stop;
    callback->perf_log = retro_perf_log;

    return true;
}

bool EnvironmentHandler::SetSystemAvInfo(const retro_system_av_info* av_info)
{
    if (!av_info)
        return true;

    Log("System AV Info: " + std::to_string(av_info->geometry.base_width) + "x" + std::to_string(av_info->geometry.base_height) +
        " @ " + std::to_string(av_info->geometry.max_width) + "x" + std::to_string(av_info->geometry.max_height) +
        " (aspect ratio: " + std::to_string(av_info->geometry.aspect_ratio) + ")" +
        "FPS: " + std::to_string(av_info->timing.fps) + " Sample Rate: " + std::to_string(av_info->timing.sample_rate));

    return true;
}

bool EnvironmentHandler::SetSubsystemInfo(const retro_subsystem_info* subsystem_info)
{
    if (!subsystem_info)
        return false;

    // auto subsystemInfo = static_cast<const retro_subsystem_info*>(data);
    // for (int i = 0; subsystemInfo[i].desc; ++i)
    // {
    //     Log("desc: " + String(subsystemInfo[i].desc));
    //     Log("ident: " + String(subsystemInfo[i].ident));
    //     for (int j = 0; j < subsystemInfo[i].num_roms; ++j)
    //     {
    //         Log("rom desc: " + String(subsystemInfo[i].roms[j].desc));
    //         Log("rom valid_extensions: " + String(subsystemInfo[i].roms[j].valid_extensions));
    //         Log("rom need_fullpath: " + String::num_int64(subsystemInfo[i].roms[j].need_fullpath));
    //         Log("rom block_extract: " + String::num_int64(subsystemInfo[i].roms[j].block_extract));
    //         Log("rom required: " + String::num_int64(subsystemInfo[i].roms[j].required));
    //         for (size_t k = 0; k < subsystemInfo[i].roms[j].num_memory; ++k)
    //         {
    //             Log("rom memory extension: " + String(subsystemInfo[i].roms[j].memory[k].extension));
    //             Log("rom memory type: " + String::num_int64(subsystemInfo[i].roms[j].memory[k].type));
    //         }
    //     }
    //     Log("id: " + String::num_int64(subsystemInfo[i].id));
    // }

    return false;
}

bool EnvironmentHandler::SetMemoryMaps(const retro_memory_map* memory_maps)
{
    if (memory_maps)
        for (size_t i = 0; i < memory_maps->num_descriptors; ++i)
        {
            const retro_memory_descriptor& descriptor = memory_maps->descriptors[i];
            //Log("Memory Descriptor: flags = " + std::to_string(descriptor.flags));
        }
    return true;
}

bool EnvironmentHandler::GetUsername(const char** username) const
{
    if (username)
        *username = Wrapper::GetCurrentThreadWrapper()->m_username.c_str();
    return true;
}

bool EnvironmentHandler::GetLanguage(retro_language* language) const
{
    if (language)
        *language = RETRO_LANGUAGE_ENGLISH;
    return true;
}

bool EnvironmentHandler::SetSupportAchievements(bool* support)
{
    if (support)
        return true;

    // Log("Support: " + (support ? "enabled" : "disabled"));
    return true;
}

bool EnvironmentHandler::GetVfsInterface(retro_vfs_interface_info* vfs_interface_info)
{
    if (!vfs_interface_info)
    {
        LogError("Passed null retro_vfs_interface_info.");
        return true;
    }

    if (vfs_interface_info->required_interface_version > s_supported_vfs_version)
    {
        LogError("Requested VFS interface not implemented: " + std::to_string(vfs_interface_info->required_interface_version));
        return false;
    }

    vfs_interface_info->required_interface_version = s_supported_vfs_version;
    vfs_interface_info->iface                      = &m_vfs_interface;

    return true;
}

bool EnvironmentHandler::GetLedInterface(retro_led_interface* led_interface)
{
    if (led_interface)
        led_interface->set_led_state = Wrapper::LedInterfaceSetLedState;
    return true;
}

bool EnvironmentHandler::GetAudioVideoEnable(retro_av_enable_flags* audio_video_enable)
{
    if (audio_video_enable)
        *audio_video_enable = static_cast<retro_av_enable_flags>(RETRO_AV_ENABLE_AUDIO | RETRO_AV_ENABLE_VIDEO);
    return true;
}

bool EnvironmentHandler::GetFastForwarding(bool* fast_forwarding)
{
    if (fast_forwarding)
        *fast_forwarding = false;
    return true;
}

bool EnvironmentHandler::GetDiskControlInterfaceVersion(uint32_t* version)
{
    if (version)
        *version = 1;
    return true;
}

bool EnvironmentHandler::SetDiskControlExtInterface(const retro_disk_control_ext_callback* callback)
{
    if (!callback)
    {
        m_disk_control_ext_callback = {};
        return true;
    }

    m_disk_control_ext_callback = *callback;
    return true;
}

bool EnvironmentHandler::GetThrottleState(retro_throttle_state* state)
{
    if (state)
    {
        state->mode = RETRO_THROTTLE_NONE;
        state->rate = 0.0f;
    }

    return true;
}

bool EnvironmentHandler::GetClearAllThreadWaitsCb(retro_environment_t* env)
{
    if (env)
        *env = runloop_clear_all_thread_waits;
    return true;
}
}
