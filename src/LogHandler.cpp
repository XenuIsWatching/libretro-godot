#include "LogHandler.hpp"

#include <godot_cpp/core/print_string.hpp>

#include <cstdarg>
#include <cstdio>

#include "Wrapper.hpp"

using namespace godot;

namespace SK
{
bool LogHandler::GetLogInterface(retro_log_callback* callback)
{
    if (!callback)
        return true;

    callback->log = LogInterfaceLog;
    return true;
}

void LogHandler::LogInterfaceLog(retro_log_level level, const char* fmt, ...)
{
    auto* w = Wrapper::GetCurrentThreadWrapper();
    if (!w)
        return;
    if (level < w->m_log_handler->m_log_level)
        return;

    va_list args;
    va_start(args, fmt);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    switch (level)
    {
    case RETRO_LOG_DEBUG:
        print_line_rich("[color=white][Core][Debug]:[/color] " + String(buffer));
        break;
    case RETRO_LOG_INFO:
        print_line_rich("[color=white][Core][Info]:[/color] " + String(buffer));
        break;
    case RETRO_LOG_WARN:
        print_line_rich("[color=orange][Core][Warning]:[/color] " + String(buffer));
        break;
    case RETRO_LOG_ERROR:
        print_line_rich("[color=red][Core][Error]:[/color] " + String(buffer));
        break;
    default:
        break;
    }

    va_end(args);
}
}
