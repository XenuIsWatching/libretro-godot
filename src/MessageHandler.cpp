#include "MessageHandler.hpp"

#include "Debug.hpp"

namespace SK
{
bool MessageHandler::GetMessageInterfaceVersion(uint32_t* version)
{
    if (!version)
        return true;

    *version = SUPPORTED_MESSAGE_INTERFACE_VERSION;
    return true;
}

bool MessageHandler::SetMessage(const retro_message* message)
{
    if (!message)
        return true;

    Log("message: " + std::string(message->msg));
    Log("frames: " + std::to_string(message->frames));

    return true;
}

bool MessageHandler::SetMessageExt(const retro_message_ext* message)
{
    if (!message)
        return true;

    Log("[RETRO_ENVIRONMENT_SET_MESSAGE_EXT] message: " + std::string(message->msg) +
        " duration: " + std::to_string(message->duration) +
        " priority: " + std::to_string(message->priority) +
        " level: " + std::to_string(message->level) +
        " target: " + std::to_string(message->target) +
        " type: " + std::to_string(message->type) +
        " progress: " + std::to_string(message->progress));

    return true;
}
}
