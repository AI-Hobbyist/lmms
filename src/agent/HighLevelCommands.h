#ifndef LMMS_AGENT_HIGH_LEVEL_COMMANDS_H
#define LMMS_AGENT_HIGH_LEVEL_COMMANDS_H
namespace lmms::agent {
class CommandBus;
void registerHighLevelCommands(CommandBus& bus);
}
#endif
