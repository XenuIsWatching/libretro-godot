#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/input_event.hpp>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include <vector>
#include <queue>
#include <unordered_map>

#include <libretro.h>
#include <readerwriterqueue.h>

#include "ThreadCommand.hpp"
#include "Core.hpp"
#include "EnvironmentHandler.hpp"
#include "VideoHandler.hpp"
#include "AudioHandler.hpp"
#include "InputHandler.hpp"
#include "OptionsHandler.hpp"
#include "MessageHandler.hpp"
#include "LogHandler.hpp"

namespace SK
{
// Forward declaration to avoid circular include (Libretro.hpp includes Wrapper.hpp indirectly)
class Libretro;

class Wrapper
{
public:
    Wrapper() = default;
    ~Wrapper() = default;

    Wrapper(const Wrapper&) = delete;
    Wrapper& operator=(const Wrapper&) = delete;
    Wrapper(Wrapper&&) = delete;
    Wrapper& operator=(Wrapper&&) = delete;

    /// Returns the Wrapper instance currently running on this thread (set at the
    /// start of each emulation thread loop). Returns nullptr on the main thread
    /// unless explicitly set via SetCurrentThreadWrapper.
    static Wrapper* GetCurrentThreadWrapper();

    /// Set or clear the current-thread Wrapper pointer. Called automatically by
    /// the emulation thread; also used by thread commands and cleanup code that
    /// run on the main thread and need access to the owning Wrapper.
    static void SetCurrentThreadWrapper(Wrapper* wrapper);

    void StartContent(godot::MeshInstance3D* node, const std::string& root_directory, const std::string& core_name, const std::string& game_path);
    void StopContent();
    void SetScreenMesh(godot::MeshInstance3D* node);

    const std::unordered_map<std::string, OptionCategory>& GetOptionCategories() const { return m_options_handler->GetCategories(); }
    const std::unordered_map<std::string, OptionDefinition>& GetOptionDefinitions() const { return m_options_handler->GetDefinitions(); }
    const std::unordered_map<std::string, std::string>& GetOptionValues() const { return m_options_handler->GetValues(); }
    void SetCoreOption(const std::string& key, const std::string& value);

    /// Returns per-port controller info as an Array of Arrays of Dictionaries
    /// [{name, id}], indexed by port number. Consumed by Libretro::GetControllerInfo().
    godot::Array GetControllerInfo() const;

    /// Tell the running core which device type is active on a given port.
    /// Calls retro_set_controller_port_device and updates the local tracking map.
    void SetControllerPortDevice(uint32_t port, uint32_t device);

    void _input(const godot::Ref<godot::InputEvent>& event);
    void _process(double delta);

    godot::MeshInstance3D* m_node;

    const std::string& GetRootDirectory() const { return m_root_directory; }
    const std::string& GetTempDirectory() const { return m_temp_directory; }

    std::unique_ptr<Core> m_core = nullptr;
    std::unique_ptr<EnvironmentHandler> m_environment_handler = nullptr;
    std::unique_ptr<VideoHandler> m_video_handler = nullptr;
    std::unique_ptr<AudioHandler> m_audio_handler = nullptr;
    std::unique_ptr<InputHandler> m_input_handler = nullptr;
    std::unique_ptr<OptionsHandler> m_options_handler = nullptr;
    std::unique_ptr<MessageHandler> m_message_handler = nullptr;
    std::unique_ptr<LogHandler> m_log_handler = nullptr;

    std::thread m_thread;
    moodycamel::ReaderWriterQueue<std::unique_ptr<ThreadCommand>> m_main_thread_commands_queue;
    std::mutex m_mutex;
    bool m_mutex_done = false;
    std::condition_variable m_condition_variable;
    std::atomic<bool> m_running = false;
    bool m_input_enabled = false;   // only true for the actively-controlled instance

    std::string m_root_directory;
    std::string m_temp_directory;
    std::string m_username = "DefaultUser";
    retro_log_level m_log_level = RETRO_LOG_WARN;

    std::string m_game_path;

    std::vector<unsigned char> m_game_buffer;

    /// The Libretro node that owns this Wrapper (set by Libretro constructor).
    Libretro* m_libretro_node = nullptr;

    void StopEmulationThread();
    void EmulationThreadLoop();
    void CreateTexture(godot::Image::Format image_format, godot::PackedByteArray pixel_data, int32_t width, int32_t height, bool flip_y);
    void UpdateTexture(godot::PackedByteArray pixel_data, bool flip_y);

    bool Shutdown();

    static void LedInterfaceSetLedState(int32_t led, int32_t state);
};
}
