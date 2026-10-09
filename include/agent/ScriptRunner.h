#ifndef LMMS_AGENT_SCRIPT_RUNNER_H
#define LMMS_AGENT_SCRIPT_RUNNER_H

#include "agent/CommandBus.h"
#include <QStringList>

namespace lmms::agent {
class LMMS_EXPORT ScriptRunner
{
public:
	static CommandResult run(
		const QJsonObject& script, const QJsonObject& variables = {}, int seed = 0, bool dryRun = false);
	static QStringList builtInScripts();
	static QJsonObject loadBuiltIn(QString name);
};
void registerScriptCommands(CommandBus& bus);
}
#endif
