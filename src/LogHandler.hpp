#pragma once

#include <libretro.h>

namespace SK
{
class LogHandler
{
public:
    bool GetLogInterface(retro_log_callback* callback);
    static void LogInterfaceLog(retro_log_level level, const char* fmt, ...);

private:
    retro_log_level m_log_level = RETRO_LOG_WARN;
};
}
