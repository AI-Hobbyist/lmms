#ifndef LMMS_AGENT_DISCOVERY_COMMANDS_H
#define LMMS_AGENT_DISCOVERY_COMMANDS_H

namespace lmms::agent {

class CommandBus;

void registerDiscoveryCommands(CommandBus& commandBus);

} // namespace lmms::agent

#endif // LMMS_AGENT_DISCOVERY_COMMANDS_H
