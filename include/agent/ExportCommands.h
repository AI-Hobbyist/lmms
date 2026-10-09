#ifndef LMMS_AGENT_EXPORT_COMMANDS_H
#define LMMS_AGENT_EXPORT_COMMANDS_H

#include "lmms_export.h"

namespace lmms::agent {
class CommandBus;

void registerExportCommands(CommandBus& bus);
LMMS_EXPORT bool hasActiveAudioExport();
LMMS_EXPORT void shutdownAudioExports();
}

#endif
