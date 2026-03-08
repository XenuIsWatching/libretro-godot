#pragma once

#include <libretro.h>

namespace SK
{
class MessageHandler
{
public:
    bool GetMessageInterfaceVersion(uint32_t* version);
    bool SetMessage(const retro_message* message);
    bool SetMessageExt(const retro_message_ext* message);

private:
    static const uint32_t SUPPORTED_MESSAGE_INTERFACE_VERSION = 1;
};
}
