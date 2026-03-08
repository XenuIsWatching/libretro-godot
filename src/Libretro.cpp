#include "Libretro.hpp"

#include "Wrapper.hpp"

using namespace godot;

namespace SK
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

godot::Array Libretro::GetControllerInfo()
{
    return m_wrapper->GetControllerInfo();
}

void Libretro::SetControllerPortDevice(int port, int device)
{
    m_wrapper->SetControllerPortDevice(static_cast<uint32_t>(port), static_cast<uint32_t>(device));
}

void Libretro::_exit_tree()
{
    m_wrapper->StopContent();
}

void Libretro::_input(const Ref<InputEvent>& event)
{
    m_wrapper->_input(event);
}

void Libretro::_process(double delta)
{
    m_wrapper->_process(delta);
}

void Libretro::NotifyOptionsReady()
{
    auto categories     = GetOptionCategories();
    auto definitions    = GetOptionDefinitions();
    auto current_values = GetOptionValues();
    call_deferred("emit_signal", "options_ready", categories, definitions, current_values);
}

Dictionary Libretro::GetOptionCategories()
{
    Dictionary result;
    const auto& categories = m_wrapper->GetOptionCategories();
    for (const auto& [key, value] : categories)
    {
        Ref<LibretroOptionCategory> category = memnew(LibretroOptionCategory);
        category->m_desc = value.desc.c_str();
        category->m_info = value.info.c_str();
        result[String(key.c_str())] = category;
    }
    return result;
}

Dictionary Libretro::GetOptionDefinitions()
{
    Dictionary result;
    const auto& definitions = m_wrapper->GetOptionDefinitions();
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

Dictionary Libretro::GetOptionValues()
{
    Dictionary result;
    const auto& values = m_wrapper->GetOptionValues();
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
    ClassDB::bind_method(D_METHOD("SetInputEnabled", "enabled"), &Libretro::SetInputEnabled);
    ClassDB::bind_method(D_METHOD("GetControllerInfo"), &Libretro::GetControllerInfo);
    ClassDB::bind_method(D_METHOD("SetControllerPortDevice", "port", "device"), &Libretro::SetControllerPortDevice);

    ADD_SIGNAL(MethodInfo("options_ready", PropertyInfo(Variant::DICTIONARY, "categories"), PropertyInfo(Variant::DICTIONARY, "definitions"), PropertyInfo(Variant::DICTIONARY, "current_values")));
}
}
