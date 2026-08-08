#include "Libretro.hpp"

#include "Wrapper.hpp"
#include "AudioHandler.hpp"
#include "CoreOptionsPeek.hpp"
#include "RetroAchievements.hpp"

using namespace godot;

namespace Xenu
{
Libretro::Libretro()
{
    m_wrapper = std::make_unique<Wrapper>();
    m_wrapper->m_libretro_node = this;
}

Libretro::~Libretro() = default;

void Libretro::ConnectOptionsReady(const godot::Callable& callable, uint32_t flags)
{
    connect("options_ready", callable, flags);
}

void Libretro::StartContent(MeshInstance3D* node, String root_directory, String core_name, String game_path)
{
    m_wrapper->StartContent(node, root_directory.utf8().get_data(), core_name.utf8().get_data(), game_path.utf8().get_data());
}

void Libretro::StopContent()
{
    m_wrapper->StopContent();
}

void Libretro::SetScreenMesh(MeshInstance3D* node)
{
    m_wrapper->SetScreenMesh(node);
}

void Libretro::SetCoreOption(const godot::String& key, const godot::String& value)
{
    m_wrapper->SetCoreOption(key.utf8().get_data(), value.utf8().get_data());
}

void Libretro::SetInputEnabled(bool enabled)
{
    m_wrapper->m_input_enabled = enabled;
}

void Libretro::SetPreferredHwRender(int context_type)
{
    VideoHandler::SetPreferredHwRender(static_cast<retro_hw_context_type>(context_type));
}

godot::PackedInt32Array Libretro::GetAudioVoiceIds()
{
    if (!m_wrapper || !m_wrapper->m_audio_handler)
        return godot::PackedInt32Array();
    return m_wrapper->m_audio_handler->GetVoiceIds();
}

void Libretro::SetAudioChannelMode(int mode)
{
    if (!m_wrapper || !m_wrapper->m_audio_handler)
        return;
    m_wrapper->m_audio_handler->SetChannelMode(mode);
}

godot::Array Libretro::GetControllerInfo()
{
    return m_wrapper->GetControllerInfo();
}

void Libretro::SetControllerPortDevice(int port, int device)
{
    m_wrapper->SetControllerPortDevice(static_cast<uint32_t>(port), static_cast<uint32_t>(device));
}

void Libretro::SetLightgunPosition(int port, int x, int y)
{
    m_wrapper->SetLightgunPosition(static_cast<uint32_t>(port), static_cast<int16_t>(x), static_cast<int16_t>(y));
}

void Libretro::SetLightgunIsOffscreen(int port, bool offscreen)
{
    m_wrapper->SetLightgunIsOffscreen(static_cast<uint32_t>(port), offscreen);
}

void Libretro::SetLightgunButton(int port, int button_id, bool pressed)
{
    m_wrapper->SetLightgunButton(static_cast<uint32_t>(port), button_id, pressed);
}

void Libretro::SetJoypadState(int port, int button_mask, int analog_lx, int analog_ly, int analog_rx, int analog_ry)
{
    m_wrapper->SetJoypadState(
        static_cast<uint32_t>(port),
        static_cast<uint16_t>(button_mask),
        static_cast<int16_t>(analog_lx),
        static_cast<int16_t>(analog_ly),
        static_cast<int16_t>(analog_rx),
        static_cast<int16_t>(analog_ry));
}

void Libretro::SetMouseState(int port, int dx, int dy, int buttons)
{
    m_wrapper->SetMouseState(static_cast<uint32_t>(port),
        static_cast<int32_t>(dx), static_cast<int32_t>(dy),
        static_cast<uint32_t>(buttons));
}

void Libretro::SetKeyState(int port, int keycode, bool down, int character)
{
    m_wrapper->SetKeyState(static_cast<uint32_t>(port),
        static_cast<uint32_t>(keycode), down, static_cast<uint32_t>(character));
}

int Libretro::GodotKeyToRetroKey(const Ref<InputEventKey>& event) const
{
    if (event.is_null())
        return 0;
    return static_cast<int>(Wrapper::GodotKeyToRetroKey(event));
}

void Libretro::SetSensorAccel(int port, float x, float y, float z)
{
    m_wrapper->SetSensorAccel(static_cast<uint32_t>(port), x, y, z);
}

void Libretro::SetPointerState(int port, int x, int y, bool pressed)
{
    m_wrapper->SetPointerState(static_cast<uint32_t>(port),
        static_cast<int16_t>(x), static_cast<int16_t>(y), pressed);
}

void Libretro::SetNetplayMode(bool enabled, int port_mask, int64_t start_frame)
{
    m_wrapper->SetNetplayMode(enabled, static_cast<uint32_t>(port_mask), start_frame);
}

void Libretro::PostNetplayInputs(int64_t frame, const godot::PackedInt32Array& inputs)
{
    m_wrapper->PostNetplayInputs(frame, inputs);
}

void Libretro::SetNetplayRollback(bool enabled, int local_mask, int max_ahead)
{
    m_wrapper->SetNetplayRollback(enabled, static_cast<uint32_t>(local_mask), max_ahead);
}

godot::PackedInt32Array Libretro::TakeNetplayLocalRecords()
{
    return m_wrapper->TakeNetplayLocalRecords();
}

void Libretro::RequestSaveState()
{
    m_wrapper->RequestSaveState();
}

void Libretro::RequestLoadState(const godot::PackedByteArray& data, int64_t frame)
{
    m_wrapper->RequestLoadState(data, frame);
}

void Libretro::SetSramPath(const godot::String& path)
{
    m_wrapper->SetSramPath(path);
}

void Libretro::SetSramData(const godot::PackedByteArray& data)
{
    m_wrapper->SetSramData(data);
}

void Libretro::SetRemovableStorage(bool removable)
{
    m_wrapper->SetRemovableStorage(removable);
}

void Libretro::RequestSramFlush()
{
    m_wrapper->RequestSramFlush();
}

void Libretro::RequestDiskInfo()
{
    m_wrapper->RequestDiskInfo();
}

void Libretro::SetDiskEjectState(bool ejected)
{
    m_wrapper->SetDiskEjectState(ejected);
}

void Libretro::ReplaceDiskImage(int64_t index, const godot::String& path)
{
    m_wrapper->ReplaceDiskImage(static_cast<uint32_t>(index < 0 ? 0 : index), path);
}

void Libretro::ScheduleDiscOp(int64_t frame, int64_t op, int64_t index, const godot::String& path)
{
    m_wrapper->ScheduleDiscOp(frame, static_cast<int32_t>(op),
        static_cast<uint32_t>(index < 0 ? 0 : index), path);
}

int64_t Libretro::GetFrameCount() const
{
    return m_wrapper->GetFrameCount();
}

int64_t Libretro::GetNetplayRollbackCount() const
{
    return m_wrapper->GetNetplayRollbackCount();
}

void Libretro::_exit_tree()
{
    // Blocking, unlike StopContent(): leaving the tree means this node can be
    // freed at any point after this returns, and the scene being torn down owns
    // the screen mesh VideoHandler::DeInit restores its material on. Deferring
    // the join to _process() (which will never run again) leaves that teardown
    // to ~Wrapper, by which time the mesh may already be gone. The frame hitch
    // this costs does not matter on the way out.
    m_wrapper->ShutdownForExit();
}

bool Libretro::RaClaimSession(int console_id)
{
    RetroAchievements* ra = RetroAchievements::GetSingleton();
    if (ra == nullptr || console_id <= 0)
        return false;
    return ra->ClaimSession(m_wrapper.get(), static_cast<uint32_t>(console_id));
}

bool Libretro::RaHoldsSession() const
{
    RetroAchievements* ra = RetroAchievements::GetSingleton();
    return ra != nullptr && ra->HoldsSession(m_wrapper.get());
}

void Libretro::_process(double delta)
{
    m_wrapper->_process(delta);

    // rc_client's pending queue (unlocks that failed to send, session pings) is
    // serviced by do_frame while a game runs and by idle when one does not. Once a
    // second is what rcheevos asks for; every frame would be wasted work.
    m_ra_idle_accumulator += delta;
    if (m_ra_idle_accumulator >= 1.0)
    {
        m_ra_idle_accumulator = 0.0;
        if (RetroAchievements* ra = RetroAchievements::GetSingleton())
            ra->Idle();
    }
}

void Libretro::NotifyOptionsReady()
{
    auto categories     = ConvertOptionCategories(m_wrapper->GetOptionCategories());
    auto definitions    = ConvertOptionDefinitions(m_wrapper->GetOptionDefinitions());
    auto current_values = ConvertOptionValues(m_wrapper->GetOptionValues());
    call_deferred("emit_signal", "options_ready", categories, definitions, current_values);
}

void Libretro::NotifySramFlushed(const String& path, int64_t size, bool final_flush)
{
    call_deferred("emit_signal", "sram_flushed", path, size, final_flush);
}

Dictionary Libretro::PeekCoreOptions(const String& root_directory, const String& core_name)
{
    Dictionary result;

    const std::string core_path = Wrapper::ResolveCorePath(
        std::string(root_directory.utf8().get_data()),
        std::string(core_name.utf8().get_data()));

    // Qualified: the member function name would otherwise hide the free one.
    OptionsHandler options;
    if (!Xenu::PeekCoreOptions(core_path, options))
        return result;

    result["categories"]  = ConvertOptionCategories(options.GetCategories());
    result["definitions"] = ConvertOptionDefinitions(options.GetDefinitions());
    result["values"]      = ConvertOptionValues(options.GetValues());
    return result;
}

Dictionary Libretro::ConvertOptionCategories(const std::unordered_map<std::string, OptionCategory>& categories)
{
    Dictionary result;
    for (const auto& [key, value] : categories)
    {
        Ref<LibretroOptionCategory> category = memnew(LibretroOptionCategory);
        category->m_desc = value.desc.c_str();
        category->m_info = value.info.c_str();
        result[String(key.c_str())] = category;
    }
    return result;
}

Dictionary Libretro::ConvertOptionDefinitions(const std::unordered_map<std::string, OptionDefinition>& definitions)
{
    Dictionary result;
    for (const auto& [key, value] : definitions)
    {
        Ref<LibretroOptionDefinition> definition = memnew(LibretroOptionDefinition);
        definition->m_desc = value.desc.c_str();
        definition->m_desc_categorized = value.desc_categorized.c_str();
        definition->m_info = value.info.c_str();
        definition->m_info_categorized = value.info_categorized.c_str();
        definition->m_category_key = value.category_key.c_str();
        definition->m_values = Array();
        for (const auto& val : value.values)
        {
            Ref<LibretroOptionValue> option_value = memnew(LibretroOptionValue);
            option_value->m_value = val.value.c_str();
            option_value->m_label = val.label.c_str();
            definition->m_values.append(option_value);
        }
        definition->m_default_value = value.default_value.c_str();
        result[String(key.c_str())] = definition;
    }
    return result;
}

Dictionary Libretro::ConvertOptionValues(const std::unordered_map<std::string, std::string>& values)
{
    Dictionary result;
    for (const auto& [key, value] : values)
        result[String(key.c_str())] = String(value.c_str());
    return result;
}

void Libretro::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("ConnectOptionsReady", "callable", "flags"), &Libretro::ConnectOptionsReady, DEFVAL(0u));
    ClassDB::bind_method(D_METHOD("StartContent", "node", "root_directory", "core_name", "game_path"), &Libretro::StartContent);
    ClassDB::bind_method(D_METHOD("StopContent"), &Libretro::StopContent);
    ClassDB::bind_method(D_METHOD("SetScreenMesh", "node"), &Libretro::SetScreenMesh);
    ClassDB::bind_method(D_METHOD("SetCoreOption", "key", "value"), &Libretro::SetCoreOption);
    ClassDB::bind_method(D_METHOD("PeekCoreOptions", "root_directory", "core_name"), &Libretro::PeekCoreOptions);
    ClassDB::bind_method(D_METHOD("SetInputEnabled", "enabled"), &Libretro::SetInputEnabled);
    ClassDB::bind_static_method("Libretro", D_METHOD("SetPreferredHwRender", "context_type"), &Libretro::SetPreferredHwRender);
    ClassDB::bind_method(D_METHOD("GetControllerInfo"), &Libretro::GetControllerInfo);
    ClassDB::bind_method(D_METHOD("GetAudioVoiceIds"), &Libretro::GetAudioVoiceIds);
    ClassDB::bind_method(D_METHOD("SetAudioChannelMode", "mode"), &Libretro::SetAudioChannelMode);
    ClassDB::bind_method(D_METHOD("SetControllerPortDevice", "port", "device"), &Libretro::SetControllerPortDevice);
    ClassDB::bind_method(D_METHOD("SetLightgunPosition", "port", "x", "y"), &Libretro::SetLightgunPosition);
    ClassDB::bind_method(D_METHOD("SetLightgunIsOffscreen", "port", "offscreen"), &Libretro::SetLightgunIsOffscreen);
    ClassDB::bind_method(D_METHOD("SetLightgunButton", "port", "button_id", "pressed"), &Libretro::SetLightgunButton);
    ClassDB::bind_method(D_METHOD("SetJoypadState", "port", "button_mask", "analog_lx", "analog_ly", "analog_rx", "analog_ry"), &Libretro::SetJoypadState);
    ClassDB::bind_method(D_METHOD("SetMouseState", "port", "dx", "dy", "buttons"), &Libretro::SetMouseState);
    ClassDB::bind_method(D_METHOD("SetKeyState", "port", "keycode", "down", "character"), &Libretro::SetKeyState);
    ClassDB::bind_method(D_METHOD("GodotKeyToRetroKey", "event"), &Libretro::GodotKeyToRetroKey);
    ClassDB::bind_method(D_METHOD("SetSensorAccel", "port", "x", "y", "z"), &Libretro::SetSensorAccel);
    ClassDB::bind_method(D_METHOD("SetPointerState", "port", "x", "y", "pressed"), &Libretro::SetPointerState);
    ClassDB::bind_method(D_METHOD("SetNetplayMode", "enabled", "port_mask", "start_frame"), &Libretro::SetNetplayMode);
    ClassDB::bind_method(D_METHOD("PostNetplayInputs", "frame", "inputs"), &Libretro::PostNetplayInputs);
    ClassDB::bind_method(D_METHOD("SetNetplayRollback", "enabled", "local_mask", "max_ahead"), &Libretro::SetNetplayRollback);
    ClassDB::bind_method(D_METHOD("TakeNetplayLocalRecords"), &Libretro::TakeNetplayLocalRecords);
    ClassDB::bind_method(D_METHOD("RequestSaveState"), &Libretro::RequestSaveState);
    ClassDB::bind_method(D_METHOD("RequestLoadState", "data", "frame"), &Libretro::RequestLoadState);
    ClassDB::bind_method(D_METHOD("GetFrameCount"), &Libretro::GetFrameCount);
    ClassDB::bind_method(D_METHOD("GetNetplayRollbackCount"), &Libretro::GetNetplayRollbackCount);
    ClassDB::bind_method(D_METHOD("SetSramPath", "path"), &Libretro::SetSramPath);
    ClassDB::bind_method(D_METHOD("SetSramData", "data"), &Libretro::SetSramData);
    ClassDB::bind_method(D_METHOD("SetRemovableStorage", "removable"), &Libretro::SetRemovableStorage);
    ClassDB::bind_method(D_METHOD("RequestSramFlush"), &Libretro::RequestSramFlush);
    ClassDB::bind_method(D_METHOD("RequestDiskInfo"), &Libretro::RequestDiskInfo);
    ClassDB::bind_method(D_METHOD("SetDiskEjectState", "ejected"), &Libretro::SetDiskEjectState);
    ClassDB::bind_method(D_METHOD("ReplaceDiskImage", "index", "path"), &Libretro::ReplaceDiskImage);
    ClassDB::bind_method(D_METHOD("ScheduleDiscOp", "frame", "op", "index", "path"), &Libretro::ScheduleDiscOp);
    ClassDB::bind_method(D_METHOD("RaClaimSession", "console_id"), &Libretro::RaClaimSession);
    ClassDB::bind_method(D_METHOD("RaHoldsSession"), &Libretro::RaHoldsSession);

    ADD_SIGNAL(MethodInfo("savestate_ready",
        PropertyInfo(Variant::PACKED_BYTE_ARRAY, "data"),
        PropertyInfo(Variant::INT, "frame")));
    ADD_SIGNAL(MethodInfo("savestate_loaded", PropertyInfo(Variant::BOOL, "ok")));
    ADD_SIGNAL(MethodInfo("disk_control_ready",
        PropertyInfo(Variant::BOOL, "has_control"),
        PropertyInfo(Variant::INT, "count"),
        PropertyInfo(Variant::INT, "current_index"),
        PropertyInfo(Variant::BOOL, "ejected")));
    ADD_SIGNAL(MethodInfo("netplay_crc",
        PropertyInfo(Variant::INT, "frame"),
        PropertyInfo(Variant::INT, "crc")));
    /// SAVE_RAM reached disk. Only fires when the dirty check found a change,
    /// so it is the real "the game saved" event, not a timer tick. `final` is
    /// the flush at core shutdown.
    ADD_SIGNAL(MethodInfo("sram_flushed",
        PropertyInfo(Variant::STRING, "path"),
        PropertyInfo(Variant::INT,    "size"),
        PropertyInfo(Variant::BOOL,   "final")));

    ADD_SIGNAL(MethodInfo("options_ready", PropertyInfo(Variant::DICTIONARY, "categories"), PropertyInfo(Variant::DICTIONARY, "definitions"), PropertyInfo(Variant::DICTIONARY, "current_values")));
    ADD_SIGNAL(MethodInfo("rumble_state_changed",
        PropertyInfo(Variant::INT,   "port"),
        PropertyInfo(Variant::FLOAT, "weak"),
        PropertyInfo(Variant::FLOAT, "strong")));
}
}
