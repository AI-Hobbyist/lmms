#ifndef LMMS_AGENT_SVC_COMMANDS_H
#define LMMS_AGENT_SVC_COMMANDS_H

namespace lmms::agent {

class CommandBus;

void registerSvcCommands(CommandBus& commandBus);

} // namespace lmms::agent

#endif // LMMS_AGENT_SVC_COMMANDS_H
