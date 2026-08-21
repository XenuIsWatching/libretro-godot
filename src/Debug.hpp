#pragma once

#include <string>

namespace Xenu
{
#define Log(msg) Debug::Log_(msg, __FUNCTION__)
#define LogOK(msg) Debug::LogOK_(msg, __FUNCTION__)
#define LogWarning(msg) Debug::LogWarning_(msg, __FUNCTION__)
#define LogError(msg) Debug::LogError_(msg, __FUNCTION__)

/// LogError, but only the first time this line is reached.
///
/// For faults that live on a HOT path. A message written every frame, or every
/// input poll, is not diagnosis: it is a string build and a log write inside
/// emulation, and enough of them are audible as crackling in the sound. It
/// still says the thing once, which is all the reader needed.
#define LogErrorOnce(msg)                                                          do                                                                             {                                                                                  static bool s_said_it = false;                                                 if (!s_said_it)                                                                {                                                                                  s_said_it = true;                                                              Debug::LogError_(msg, __FUNCTION__);                                       }                                                                          } while (false)

class Debug
{
public:
    static void Log_(const std::string& message, const char* caller);
    static void LogOK_(const std::string& message, const char* caller);
    static void LogWarning_(const std::string& message, const char* caller);
    static void LogError_(const std::string& message, const char* caller);
};
}
