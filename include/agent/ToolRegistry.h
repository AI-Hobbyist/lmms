#ifndef LMMS_AGENT_TOOL_REGISTRY_H
#define LMMS_AGENT_TOOL_REGISTRY_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "lmms_export.h"

namespace lmms::agent {
class CommandBus;

class LMMS_EXPORT ToolRegistry
{
public:
	static QJsonArray tools();
	static QJsonObject commandHelp(const QString& name);
	static QString commandManual();
};

void registerMetadataCommands(CommandBus& bus);
}

#endif
