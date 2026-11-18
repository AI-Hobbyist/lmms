#ifndef LMMS_AGENT_CORE_COMMANDS_H
#define LMMS_AGENT_CORE_COMMANDS_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPointer>
#include <QString>

namespace lmms {
class Clip;
class Track;
} // namespace lmms

namespace lmms::agent {

class CommandBus;

void registerCoreCommands(CommandBus& commandBus);

// Inside an Agent transaction, record a track or clip that was just created so
// rollback and history detach it (keeping the same object). No-op otherwise.
void recordAgentCreatedTrack(Track* track);
void recordAgentCreatedClip(Clip* clip);

struct CommandTargetIdentity
{
	QPointer<QObject> object;
	QString name;
	QList<quintptr> indexedObjects;
	QJsonArray indexedValues;
	bool operator==(const CommandTargetIdentity&) const = default;
};

// Local target snapshots, never sent to the model or used to execute commands.
using CommandTargetIdentities = QMap<QString, CommandTargetIdentity>;
CommandTargetIdentities commandTargetIdentities();
bool includeIndexedNoteTargets(CommandTargetIdentities& targets, const QString& command, const QJsonObject& arguments,
							   QString& error);

} // namespace lmms::agent

#endif // LMMS_AGENT_CORE_COMMANDS_H
