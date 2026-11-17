#ifndef LMMS_AGENT_SVS_COMMANDS_H
#define LMMS_AGENT_SVS_COMMANDS_H

#include "CoreCommands.h"

namespace lmms::agent {

class CommandBus;

void registerSvsCommands(CommandBus& commandBus);
bool includeSvsNoteTargets(CommandTargetIdentities& targets, const QJsonObject& arguments, QString& error);

} // namespace lmms::agent

#endif // LMMS_AGENT_SVS_COMMANDS_H
