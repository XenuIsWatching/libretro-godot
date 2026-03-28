#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <memory>

namespace SK
{
class LibretroOptionCategory : public godot::RefCounted
{
    GDCLASS(LibretroOptionCategory, godot::RefCounted);

    friend class Libretro;

public:
    const godot::String& GetDescription() const { return m_desc; }
    const godot::String& GetInfo() const { return m_info; }

private:
    godot::String m_desc;
    godot::String m_info;

protected:
    static void _bind_methods()
    {
        godot::ClassDB::bind_method(godot::D_METHOD("GetDescription"), &LibretroOptionCategory::GetDescription);
        godot::ClassDB::bind_method(godot::D_METHOD("GetInfo"), &LibretroOptionCategory::GetInfo);
    }
};

class LibretroOptionValue : public godot::RefCounted
{
    GDCLASS(LibretroOptionValue, godot::RefCounted);

    friend class Libretro;

public:
    const godot::String& GetValue() const { return m_value; }
    const godot::String& GetLabel() const { return m_label; }

private:
    godot::String m_value;
    godot::String m_label;

protected:
    static void _bind_methods()
    {
        godot::ClassDB::bind_method(godot::D_METHOD("GetValue"), &LibretroOptionValue::GetValue);
        godot::ClassDB::bind_method(godot::D_METHOD("GetLabel"), &LibretroOptionValue::GetLabel);
    }
};

class LibretroOptionDefinition : public godot::RefCounted
{
    GDCLASS(LibretroOptionDefinition, godot::RefCounted);

    friend class Libretro;

public:
    const godot::String& GetDescription() const { return m_desc; }
    const godot::String& GetDescriptionCategorized() const { return m_desc_categorized; }
    const godot::String& GetInfo() const { return m_info; }
    const godot::String& GetInfoCategorized() const { return m_info_categorized; }
    const godot::String& GetCategoryKey() const { return m_category_key; }
    const godot::Array& GetValues() const { return m_values; }
    const godot::String& GetDefaultValue() const { return m_default_value; }

private:
    godot::String m_desc;
    godot::String m_desc_categorized;
    godot::String m_info;
    godot::String m_info_categorized;
    godot::String m_category_key;
    godot::Array m_values;
    godot::String m_default_value;
    
protected:
    static void _bind_methods()
    {
        godot::ClassDB::bind_method(godot::D_METHOD("GetDescription"), &LibretroOptionDefinition::GetDescription);
        godot::ClassDB::bind_method(godot::D_METHOD("GetDescriptionCategorized"), &LibretroOptionDefinition::GetDescriptionCategorized);
        godot::ClassDB::bind_method(godot::D_METHOD("GetInfo"), &LibretroOptionDefinition::GetInfo);
        godot::ClassDB::bind_method(godot::D_METHOD("GetInfoCategorized"), &LibretroOptionDefinition::GetInfoCategorized);
        godot::ClassDB::bind_method(godot::D_METHOD("GetCategoryKey"), &LibretroOptionDefinition::GetCategoryKey);
        godot::ClassDB::bind_method(godot::D_METHOD("GetValues"), &LibretroOptionDefinition::GetValues);
        godot::ClassDB::bind_method(godot::D_METHOD("GetDefaultValue"), &LibretroOptionDefinition::GetDefaultValue);
    }
};

class Wrapper;

class Libretro : public godot::Node
{
    GDCLASS(Libretro, godot::Node);

    friend class Wrapper;
    
public:
    Libretro();
    ~Libretro();

    void StartContent(godot::MeshInstance3D* node, godot::String root_directory, godot::String core_name, godot::String game_path);
    void StopContent();
    void SetScreenMesh(godot::MeshInstance3D* node);
    void SetCoreOption(const godot::String& key, const godot::String& value);
    void SetInputEnabled(bool enabled);

    /// Returns per-port controller info as Array[Dictionary{port, controllers: Array[{name,id}], current_id}].
    godot::Array GetControllerInfo();

    /// Tell the running core which device type is active on a given port.
    void SetControllerPortDevice(int port, int device);

    /// Light gun input — called from GDScript each frame when the gun is plugged in.
    void SetLightgunPosition(int port, int x, int y);
    void SetLightgunIsOffscreen(int port, bool offscreen);
    void SetLightgunButton(int port, int button_id, bool pressed);

    /// Per-port joypad input — called from GDScript by physical retro controller objects.
    void SetJoypadState(int port, int button_mask, int analog_lx, int analog_ly, int analog_rx, int analog_ry);

    void ConnectOptionsReady(const godot::Callable& callable, uint32_t flags = 0u);

    void _exit_tree() override;
    void _input(const godot::Ref<godot::InputEvent>& event) override;
    void _process(double delta) override;

    /// Called from the emulation thread (via Wrapper::m_libretro_node) when options are ready.
    void NotifyOptionsReady();

private:
    std::unique_ptr<Wrapper> m_wrapper;

    godot::Dictionary GetOptionCategories();
    godot::Dictionary GetOptionDefinitions();
    godot::Dictionary GetOptionValues();

    static void _bind_methods();
};
}
