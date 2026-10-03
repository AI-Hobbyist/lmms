#ifndef LMMS_AGENT_COMMAND_BUS_H
#define LMMS_AGENT_COMMAND_BUS_H

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <functional>
#include <vector>

#include "lmms_export.h"

namespace lmms::agent
{

enum class Mutability
{
	ReadOnly,
	Mutating,
	Destructive
};

enum class TxScope
{
	None,
	Single,
	Batch
};

struct LMMS_EXPORT CommandResult
{
	bool ok = false;
	QJsonObject data;
	QString errorCode;
	QString errorMessage;
	QString errorHint;

	static CommandResult success( QJsonObject data = {} );
	static CommandResult failure( const QString &code, const QString &message,
		const QString &hint = {} );
	QJsonObject toJson() const;
};

using CommandHandler = std::function<CommandResult( const QJsonObject &arguments )>;

struct LMMS_EXPORT CommandDescriptor
{
	QString name;
	QString summary;
	QJsonObject argsSchema;
	Mutability mutability = Mutability::ReadOnly;
	TxScope scope = TxScope::None;
	CommandHandler handler;
};

class LMMS_EXPORT CommandBus
{
public:
	static CommandBus & instance();

	bool registerCommand( const CommandDescriptor &descriptor );
	CommandResult execute( const QString &name, const QJsonObject &arguments = {} );
	QList<CommandDescriptor> descriptors() const;

	bool beginBatch( const QString &label );
	CommandResult endBatch( bool success );
	bool isBatchActive() const;
	int batchDepth() const;
	CommandResult rollbackBatch();

private:
	struct Batch
	{
		QString label;
	};

	CommandBus();

	QHash<QString, CommandDescriptor> m_commands;
	std::vector<Batch> m_batches;
};

} // namespace lmms::agent

#endif // LMMS_AGENT_COMMAND_BUS_H
