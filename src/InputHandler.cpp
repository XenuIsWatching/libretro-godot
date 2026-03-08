#include "InputHandler.hpp"

#include "Wrapper.hpp"
#include "Debug.hpp"

namespace SK
{
void InputHandler::PollCallback()
{
}

int16_t InputHandler::StateCallback(uint32_t port, uint32_t device, uint32_t index, uint32_t id)
{
    auto instance = Wrapper::GetCurrentThreadWrapper();
    if (!instance)
    {
        LogError("Libretro instance is null.");
        return 0;
    }

    device &= RETRO_DEVICE_MASK;

    switch (device)
    {
    case RETRO_DEVICE_JOYPAD:
        return instance->m_input_handler->ProcessJoypadDevice(port, id);
    case RETRO_DEVICE_MOUSE:
        return instance->m_input_handler->ProcessMouseDevice(port, id);
    case RETRO_DEVICE_KEYBOARD:
        return instance->m_input_handler->ProcessKeyboardDevice(port, id);
    case RETRO_DEVICE_LIGHTGUN:
        return instance->m_input_handler->ProcessLightgunDevice(port, id);
    case RETRO_DEVICE_POINTER:
        return instance->m_input_handler->ProcessPointerDevice(port, id);
    case RETRO_DEVICE_ANALOG:
        return instance->m_input_handler->ProcessAnalogDevice(port, index, id);
    default:
        LogError("Unhandled input device: " + std::to_string(device) + " for port: " + std::to_string(port) + " and id: " + std::to_string(id));
        break;
    }

    return 0;
}

bool InputHandler::RumbleInterfaceSetRumbleState(uint32_t port, retro_rumble_effect effect, uint16_t strength)
{
    Log("Port: " + std::to_string(port) +
        " Effect: " + (effect == RETRO_RUMBLE_STRONG ? "strong" : "weak") +
        " Strength: " + std::to_string(strength));
    return true;
}

void InputHandler::SetJoypadButtonStates(uint32_t port, uint16_t states)
{
    m_joypad_buttons[port] = states;
}
uint16_t InputHandler::GetJoypadButtonStates(uint32_t port)
{
    return m_joypad_buttons[port];
}

void InputHandler::SetMousePosition(uint32_t port, int16_t x, int16_t y)
{
    m_mouse_x[port] += x;
    m_mouse_y[port] += y;
}
void InputHandler::SetMouseButtons(uint32_t port, uint32_t buttons)
{
    m_mouse_buttons[port] = buttons;
}
uint16_t InputHandler::GetMouseX(uint32_t port)
{
    return m_mouse_x[port];
}
uint16_t InputHandler::GetMouseY(uint32_t port)
{
    return m_mouse_y[port];
}
uint32_t InputHandler::GetMouseButtons(uint32_t port)
{
    return m_mouse_buttons[port];
}

void InputHandler::SetKeyboardKeys(uint32_t port, uint32_t keys)
{
    m_keyboard_keys[port] = keys;
}
uint32_t InputHandler::GetKeyboardKeys(uint32_t port)
{
    return m_keyboard_keys[port];
}

void InputHandler::SetLightgunPosition(uint32_t port, int16_t x, int16_t y)
{
    m_lightgun_x[port] = x;
    m_lightgun_y[port] = y;
}
void InputHandler::SetLightgunIsOffscreen(uint32_t port, int16_t is_offscreen)
{
    m_lightgun_is_offscreen[port] = is_offscreen;
}
void InputHandler::SetLightgunButtons(uint32_t port, uint32_t buttons)
{
    m_lightgun_buttons[port] = buttons;
}
int16_t InputHandler::GetLightgunX(uint32_t port)
{
    return m_lightgun_x[port];
}
int16_t InputHandler::GetLightgunY(uint32_t port)
{
    return m_lightgun_y[port];
}
int16_t InputHandler::GetLightgunIsOffscreen(uint32_t port)
{
    return m_lightgun_is_offscreen[port];
}
uint32_t InputHandler::GetLightgunButtons(uint32_t port)
{
    return m_lightgun_buttons[port];
}

void InputHandler::SetPointerPosition(uint32_t port, int16_t x, int16_t y)
{
    m_pointer_x[port] = x;
    m_pointer_y[port] = y;
}
void InputHandler::SetPointerPressed(uint32_t port, int16_t pressed)
{
    m_pointer_pressed[port] = pressed;
}
void InputHandler::SetPointerCount(uint32_t port, int16_t count)
{
    m_pointer_count[port] = count;
}
int16_t InputHandler::GetPointerX(uint32_t port)
{
    return m_pointer_x[port];
}
int16_t InputHandler::GetPointerY(uint32_t port)
{
    return m_pointer_y[port];
}
int16_t InputHandler::GetPointerPressed(uint32_t port)
{
    return m_pointer_pressed[port];
}
int16_t InputHandler::GetPointerCount(uint32_t port)
{
    return m_pointer_count[port];
}

void InputHandler::SetAnalogLeft(uint32_t port, int16_t x, int16_t y)
{
    m_analog_left_x[port] = x;
    m_analog_left_y[port] = y;
}
void InputHandler::SetAnalogRight(uint32_t port, int16_t x, int16_t y)
{
    m_analog_right_x[port] = x;
    m_analog_right_y[port] = y;
}
int16_t InputHandler::GetAnalogLeftX(uint32_t port)
{
    return m_analog_left_x[port];
}
int16_t InputHandler::GetAnalogLeftY(uint32_t port)
{
    return m_analog_left_y[port];
}
int16_t InputHandler::GetAnalogRightX(uint32_t port)
{
    return m_analog_right_x[port];
}
int16_t InputHandler::GetAnalogRightY(uint32_t port)
{
    return m_analog_right_y[port];
}

void InputHandler::CallKeyboardEventCallback(bool down, uint32_t keycode, uint32_t character, uint16_t keyModifiers)
{
    if (m_keyboard_event)
        m_keyboard_event(down, keycode, character, keyModifiers);
}

bool InputHandler::SetInputDescriptors(const retro_input_descriptor* input_descriptors)
{
    m_devices.clear();

    if (!input_descriptors)
    {
        return false;
    }

    for (int i = 0; input_descriptors[i].description; ++i)
    {
        InputHandler::RetroDevice device;
        device.port        = input_descriptors[i].port;
        device.device      = input_descriptors[i].device;
        device.index       = input_descriptors[i].index;
        device.id          = input_descriptors[i].id;
        device.description = input_descriptors[i].description;
        m_devices.emplace_back(std::move(device));
    }

    return true;
}

bool InputHandler::SetControllerInfo(const retro_controller_info* controller_info)
{
    m_controllers.clear();

    if (!controller_info)
        return true;

    for (int i = 0; controller_info[i].types; ++i)
    {
        std::vector<InputHandler::RetroController> controllers;
        for (int j = 0; j < controller_info[i].num_types; ++j)
        {
            // Looks like some cores don't set num_types to the correct value...
            if (!controller_info[i].types[j].desc) 
                continue;
            
            InputHandler::RetroController controller;
            controller.name = controller_info[i].types[j].desc;
            controller.id   = controller_info[i].types[j].id;
            controllers.emplace_back(std::move(controller));
        }
        m_controllers.emplace_back(std::move(controllers));
    }

    return true;
}

bool InputHandler::GetRumbleInterface(retro_rumble_interface* rumble_interface)
{
    if (!rumble_interface)
        return true;

    rumble_interface->set_rumble_state = RumbleInterfaceSetRumbleState;
    return true;
}

bool InputHandler::GetInputDeviceCapabilities(uint32_t* capabilities)
{
    if (!capabilities)
        return true;

    *capabilities = (1 << RETRO_DEVICE_JOYPAD)
                  | (1 << RETRO_DEVICE_MOUSE)
                  | (1 << RETRO_DEVICE_KEYBOARD)
                  | (1 << RETRO_DEVICE_LIGHTGUN)
                  | (1 << RETRO_DEVICE_ANALOG)
                  | (1 << RETRO_DEVICE_POINTER);
    return true;
}

bool InputHandler::GetInputBitmasks(bool* available)
{
    // data should be ignored but just in case a core misuses this environment
    if (available)
        *available = true;
    return true;
}

bool InputHandler::SetKeyboardEventCallback(const retro_keyboard_callback* keyboard_callback)
{
    if (!keyboard_callback)
        return false;

    if (!keyboard_callback->callback)
    {
        LogError("Invalid callback provided.");
        return false;
    }

    m_keyboard_event = keyboard_callback->callback;
    return true;
}

int16_t InputHandler::ProcessJoypadDevice(uint32_t port, uint32_t id)
{
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
        return m_joypad_buttons[port];
    return m_joypad_buttons[port] & (1 << id);
}

int16_t InputHandler::ProcessMouseDevice(uint32_t port, uint32_t id)
{
    switch (id)
    {
    case RETRO_DEVICE_ID_MOUSE_X:
    {
        int16_t dx = m_mouse_x[port];
        m_mouse_x[port] = 0;
        return dx;
    }
    break;
    case RETRO_DEVICE_ID_MOUSE_Y:
    {
        int16_t dy = m_mouse_y[port];
        m_mouse_y[port] = 0;
        return dy;
    }
    break;
    default:
        return m_mouse_buttons[port] & (1 << id);
    }
}

int16_t InputHandler::ProcessKeyboardDevice(uint32_t port, uint32_t id)
{
    return m_keyboard_keys[port] & (1 << id);
}

int16_t InputHandler::ProcessLightgunDevice(uint32_t port, uint32_t id)
{
    switch (id)
    {
        case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X:
            return m_lightgun_x[port];
        case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y:
            return m_lightgun_y[port];
        case RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN:
            return m_lightgun_is_offscreen[port];
        default:
            return m_lightgun_buttons[port] & (1 << id);
    }
}

int16_t InputHandler::ProcessPointerDevice(uint32_t port, uint32_t id)
{
    switch (id)
    {
        case RETRO_DEVICE_ID_POINTER_X:
            return m_pointer_x[port];
        case RETRO_DEVICE_ID_POINTER_Y:
            return m_pointer_y[port];
        case RETRO_DEVICE_ID_POINTER_PRESSED:
            return m_pointer_pressed[port];
        case RETRO_DEVICE_ID_POINTER_COUNT:
            return m_pointer_count[port];
        default:
            return 0;
    }
}

int16_t InputHandler::ProcessAnalogDevice(uint32_t port, uint32_t index, uint32_t id)
{
    switch (index)
    {
        case RETRO_DEVICE_INDEX_ANALOG_LEFT:
            switch (id)
            {
                case RETRO_DEVICE_ID_ANALOG_X:
                    return m_analog_left_x[port];
                case RETRO_DEVICE_ID_ANALOG_Y:
                    return m_analog_left_y[port];
                default:
                    return 0;
            }
        case RETRO_DEVICE_INDEX_ANALOG_RIGHT:
            switch (id)
            {
                case RETRO_DEVICE_ID_ANALOG_X:
                    return m_analog_right_x[port];
                case RETRO_DEVICE_ID_ANALOG_Y:
                    return m_analog_right_y[port];
                default:
                    return 0;
            }
        default:
            return 0;
    }
}
}
