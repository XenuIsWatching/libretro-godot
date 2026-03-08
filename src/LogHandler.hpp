#pragma once

#include <libretro.h>

namespace SK
{
class LogHandler
{
public:
    bool GetLogInterface(retro_log_callback* callback);

private:
    retro_log_level m_log_level = RETRO_LOG_WARN;

    static void LogInterfaceLog(retro_log_level level, const char* fmt, ...);
};
}
