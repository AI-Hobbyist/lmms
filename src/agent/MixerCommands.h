#ifndef LMMS_AGENT_MIXER_COMMANDS_H
#define LMMS_AGENT_MIXER_COMMANDS_H

namespace lmms::agent {

class CommandBus;

void registerMixerCommands(CommandBus& commandBus);

} // namespace lmms::agent

#endif // LMMS_AGENT_MIXER_COMMANDS_H
