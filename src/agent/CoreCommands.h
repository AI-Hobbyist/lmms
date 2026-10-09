#ifndef LMMS_AGENT_CORE_COMMANDS_H
#define LMMS_AGENT_CORE_COMMANDS_H

namespace lmms::agent {

class CommandBus;

void registerCoreCommands(CommandBus& commandBus);

} // namespace lmms::agent

#endif // LMMS_AGENT_CORE_COMMANDS_H