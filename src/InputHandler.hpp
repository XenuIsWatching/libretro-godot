#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include <libretro.h>

namespace SK
{
class InputHandler
{
public:
    static void PollCallback();
    static int16_t StateCallback(uint32_t port, uint32_t device, uint32_t index, uint32_t id);
    
    struct RetroController
    {
        std::string name;
        uint32_t id;
    };

    // TODO: Handle this better when needed for UI
    struct RetroDevice
    {
        uint32_t port;
        uint32_t device;
        uint32_t index;
        uint32_t id;
        std::string description;
    };

    void SetJoypadButtonStates(uint32_t port, uint16_t states);
    uint16_t GetJoypadButtonStates(uint32_t port);

    void SetMousePosition(uint32_t port, int16_t x, int16_t y);
    uint16_t GetMouseX(uint32_t port);
    uint16_t GetMouseY(uint32_t port);
    void SetMouseButtons(uint32_t port, uint32_t buttons);
    uint32_t GetMouseButtons(uint32_t port);

    void SetKeyboardKeys(uint32_t port, uint32_t keys);
    uint32_t GetKeyboardKeys(uint32_t port);

    void SetLightgunPosition(uint32_t port, int16_t x, int16_t y);
    int16_t GetLightgunX(uint32_t port);
    int16_t GetLightgunY(uint32_t port);
    void SetLightgunIsOffscreen(uint32_t port, int16_t is_offscreen);
    int16_t GetLightgunIsOffscreen(uint32_t port);
    void SetLightgunButtons(uint32_t port, uint32_t buttons);
    uint32_t GetLightgunButtons(uint32_t port);

    void SetPointerPosition(uint32_t port, int16_t x, int16_t y);
    int16_t GetPointerX(uint32_t port);
    int16_t GetPointerY(uint32_t port);
    void SetPointerPressed(uint32_t port, int16_t pressed);
    int16_t GetPointerPressed(uint32_t port);
    void SetPointerCount(uint32_t port, int16_t count);
    int16_t GetPointerCount(uint32_t port);

    void SetAnalogLeft(uint32_t port, int16_t x, int16_t y);
    int16_t GetAnalogLeftX(uint32_t port);
    int16_t GetAnalogLeftY(uint32_t port);
    void SetAnalogRight(uint32_t port, int16_t x, int16_t y);
    int16_t GetAnalogRightX(uint32_t port);
    int16_t GetAnalogRightY(uint32_t port);

    bool SetKeyboardEventCallback(const retro_keyboard_callback* keyboard_callback);
    void CallKeyboardEventCallback(bool down, uint32_t keycode, uint32_t character, uint16_t keyModifiers);

    bool SetInputDescriptors(const retro_input_descriptor* input_descriptors);
    bool SetControllerInfo(const retro_controller_info* controller_info);
    bool GetRumbleInterface(retro_rumble_interface* rumble_interface);

    const std::vector<std::vector<RetroController>>& GetControllers() const { return m_controllers; }
    /// Track which device type is active on each port (defaults to RETRO_DEVICE_JOYPAD).
    void SetPortDevice(uint32_t port, uint32_t device) { m_port_devices[port] = device; }
    uint32_t GetPortDevice(uint32_t port) const
    {
        auto it = m_port_devices.find(port);
        return it != m_port_devices.end() ? it->second : RETRO_DEVICE_JOYPAD;
    }
    bool GetInputDeviceCapabilities(uint32_t* capabilities);
    bool GetInputBitmasks(bool* available);

private:
    std::unordered_map<uint32_t, uint16_t> m_joypad_buttons;

    std::unordered_map<uint32_t, int16_t> m_mouse_x;
    std::unordered_map<uint32_t, int16_t> m_mouse_y;
    std::unordered_map<uint32_t, uint32_t> m_mouse_buttons;

    std::unordered_map<uint32_t, uint32_t> m_keyboard_keys;

    std::unordered_map<uint32_t, int16_t> m_lightgun_x;
    std::unordered_map<uint32_t, int16_t> m_lightgun_y;
    std::unordered_map<uint32_t, int16_t> m_lightgun_is_offscreen;
    std::unordered_map<uint32_t, uint32_t> m_lightgun_buttons;

    std::unordered_map<uint32_t, int16_t> m_pointer_x;
    std::unordered_map<uint32_t, int16_t> m_pointer_y;
    std::unordered_map<uint32_t, int16_t> m_pointer_pressed;
    std::unordered_map<uint32_t, int16_t> m_pointer_count;

    std::unordered_map<uint32_t, int16_t> m_analog_left_x;
    std::unordered_map<uint32_t, int16_t> m_analog_left_y;
    std::unordered_map<uint32_t, int16_t> m_analog_right_x;
    std::unordered_map<uint32_t, int16_t> m_analog_right_y;

    std::vector<std::vector<RetroController>> m_controllers;
    std::unordered_map<uint32_t, uint32_t> m_port_devices;
    std::vector<RetroDevice> m_devices;
    retro_keyboard_event_t m_keyboard_event = nullptr;

    int16_t ProcessJoypadDevice(uint32_t port, uint32_t id);
    int16_t ProcessMouseDevice(uint32_t port, uint32_t id);
    int16_t ProcessKeyboardDevice(uint32_t port, uint32_t id);
    int16_t ProcessLightgunDevice(uint32_t port, uint32_t id);
    int16_t ProcessPointerDevice(uint32_t port, uint32_t id);
    int16_t ProcessAnalogDevice(uint32_t port, uint32_t index, uint32_t id);

    static bool RumbleInterfaceSetRumbleState(uint32_t port, retro_rumble_effect effect, uint16_t strength);
};
}
