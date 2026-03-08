#include "Debug.hpp"

#include <godot_cpp/core/print_string.hpp>

using namespace godot;

namespace SK
{
void Debug::Log_(const std::string& message, const char* caller)
{
    print_line_rich(String("[color=cyan][") + caller + "][/color] " + String(message.c_str()));
}

void Debug::LogOK_(const std::string& message, const char* caller)
{
    print_line_rich(String("[color=green][") + caller + "][/color] " + String(message.c_str()));
}

void Debug::LogWarning_(const std::string& message, const char* caller)
{
    print_line_rich(String("[color=orange][") + caller + "][/color] " + String(message.c_str()));
}

void Debug::LogError_(const std::string& message, const char* caller)
{
    print_line_rich(String("[color=red][") + caller + "][/color] " + String(message.c_str()));
}
}
