#pragma once

#include <string>

namespace SK
{
#define Log(msg) Debug::Log_(msg, __FUNCTION__)
#define LogOK(msg) Debug::LogOK_(msg, __FUNCTION__)
#define LogWarning(msg) Debug::LogWarning_(msg, __FUNCTION__)
#define LogError(msg) Debug::LogError_(msg, __FUNCTION__)

class Debug
{
public:
    static void Log_(const std::string& message, const char* caller);
    static void LogOK_(const std::string& message, const char* caller);
    static void LogWarning_(const std::string& message, const char* caller);
    static void LogError_(const std::string& message, const char* caller);
};
}
