#include "agent/CommandBus.h"
#include "agent/ExportCommands.h"

#include <QCoreApplication>
#include <QDomElement>
#include <QJsonArray>
#include <QMap>
#include <QThread>

#include <algorithm>
#include <exception>

#include "CoreCommands.h"
#include "CommandSchema.h"
#include "AudioEngine.h"
#include "DataFile.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"

namespace lmms::agent
{

namespace
{

bool onMainThread()
{
	const auto *application = QCoreApplication::instance();
	return application != nullptr && application->thread() == QThread::currentThread();
}

using ProjectFields = QMap<QString, QString>;

void collectFields( const QDomElement &element, const QString &path, ProjectFields &fields )
{
	fields.insert( path, element.tagName() );
	const auto attributes = element.attributes();
	for( int index = 0; index < attributes.size(); ++index )
	{
		const auto attribute = attributes.item( index ).toAttr();
		fields.insert( path + "/@" + attribute.name(), attribute.value() );
	}
	QMap<QString, int> counts;
	for( auto child = element.firstChild(); !child.isNull(); child = child.nextSibling() )
	{
		if( child.isElement() )
		{
			const auto childElement = child.toElement();
			const auto tag = childElement.tagName();
			const int index = counts[tag]++;
			collectFields( childElement, path + "/" + tag + QString( "[%1]" ).arg( index ), fields );
		}
		else if( child.isText() || child.isCDATASection() )
		{
			fields[path + "/text()"] += child.nodeValue();
		}
	}
}

ProjectFields projectFields()
{
	ProjectFields fields;
	if( auto *song = Engine::getSong() )
	{
		DataFile state( DataFile::Type::SongProject );
		auto *journal = Engine::projectJournal();
		const bool journalling = journal->isJournalling();
		journal->setJournalling( false );
		song->saveProjectState( state );
		journal->setJournalling( journalling );
		collectFields( state.head(), "/head", fields );
		collectFields( state.content(), "/project", fields );
	}
	return fields;
}

QJsonObject projectDiff( const QString &command, const ProjectFields &before, const ProjectFields &after )
{
	QJsonArray changes;
	for( auto it = before.begin(); it != before.end(); ++it )
	{
		if( !after.contains( it.key() ) )
		{
			changes.append( QJsonObject{ { "op", "remove" }, { "path", it.key() }, { "before", it.value() } } );
		}
		else if( after.value( it.key() ) != it.value() )
		{
			changes.append( QJsonObject{ { "op", "replace" }, { "path", it.key() },
				{ "before", it.value() }, { "after", after.value( it.key() ) } } );
		}
	}
	for( auto it = after.begin(); it != after.end(); ++it )
	{
		if( !before.contains( it.key() ) )
		{
			changes.append( QJsonObject{ { "op", "add" }, { "path", it.key() }, { "after", it.value() } } );
		}
	}
	return { { "command", command }, { "changes", changes } };
}

QJsonObject historySchema()
{
	return {
		{ "type", "object" },
		{ "properties", QJsonObject{
			{ "dryRun", QJsonObject{
				{ "type", "boolean" },
				{ "description", "Preview the operation without changing project history." }
			} }
		} }
	};
}

} // namespace




CommandResult CommandResult::success( QJsonObject data )
{
	CommandResult result;
	result.ok = true;
	result.data = data;
	return result;
}




CommandResult CommandResult::failure( const QString &code, const QString &message,
	const QString &hint )
{
	CommandResult result;
	result.errorCode = code;
	result.errorMessage = message;
	result.errorHint = hint;
	return result;
}




QJsonObject CommandResult::toJson() const
{
	QJsonObject result{
		{ "ok", ok },
		{ "data", data }
	};

	if( !ok )
	{
		QJsonObject error{
			{ "code", errorCode },
			{ "message", errorMessage }
		};
		if( !errorHint.isEmpty() )
		{
			error.insert( "hint", errorHint );
		}
		result.insert( "error", error );
	}

	return result;
}




CommandBus & CommandBus::instance()
{
	static CommandBus commandBus;
	return commandBus;
}




CommandBus::CommandBus()
{
	CommandDescriptor undoCommand;
	undoCommand.name = "history.undo";
	undoCommand.summary = "Undo the most recent project change.";
	undoCommand.argsSchema = historySchema();
	undoCommand.mutability = Mutability::Mutating;
	undoCommand.handler = []( const QJsonObject &arguments )
	{
		auto *journal = Engine::projectJournal();
		if( journal == nullptr )
		{
			return CommandResult::failure( "engine_unavailable", "The project journal is unavailable." );
		}
		if( journal->hasActiveTransaction() )
		{
			return CommandResult::failure( "transaction_active", "Cannot undo during an active transaction." );
		}
		if( arguments.value( "dryRun" ).toBool() )
		{
			return CommandResult::success( QJsonObject{ { "wouldUndo", journal->canUndo() } } );
		}
		if( !journal->canUndo() )
		{
			return CommandResult::failure( "nothing_to_undo", "There is no project change to undo." );
		}

		journal->undo();
		return CommandResult::success( QJsonObject{
			{ "canUndo", journal->canUndo() },
			{ "canRedo", journal->canRedo() }
		} );
	};
	registerCommand( undoCommand );

	CommandDescriptor redoCommand;
	redoCommand.name = "history.redo";
	redoCommand.summary = "Redo the most recently undone project change.";
	redoCommand.argsSchema = historySchema();
	redoCommand.mutability = Mutability::Mutating;
	redoCommand.handler = []( const QJsonObject &arguments )
	{
		auto *journal = Engine::projectJournal();
		if( journal == nullptr )
		{
			return CommandResult::failure( "engine_unavailable", "The project journal is unavailable." );
		}
		if( journal->hasActiveTransaction() )
		{
			return CommandResult::failure( "transaction_active", "Cannot redo during an active transaction." );
		}
		if( arguments.value( "dryRun" ).toBool() )
		{
			return CommandResult::success( QJsonObject{ { "wouldRedo", journal->canRedo() } } );
		}
		if( !journal->canRedo() )
		{
			return CommandResult::failure( "nothing_to_redo", "There is no project change to redo." );
		}

		journal->redo();
		return CommandResult::success( QJsonObject{
			{ "canUndo", journal->canUndo() },
			{ "canRedo", journal->canRedo() }
		} );
	};
	registerCommand( redoCommand );

	CommandDescriptor statusCommand;
	statusCommand.name = "history.status";
	statusCommand.summary = "Return undo, redo and active batch depths.";
	statusCommand.argsSchema = historySchema();
	statusCommand.handler = [this]( const QJsonObject & )
	{
		auto *journal = Engine::projectJournal();
		if( journal == nullptr )
		{
			return CommandResult::failure( "engine_unavailable", "The project journal is unavailable." );
		}
		return CommandResult::success( QJsonObject{
			{ "canUndo", journal->canUndo() }, { "canRedo", journal->canRedo() },
			{ "undoDepth", journal->undoDepth() }, { "redoDepth", journal->redoDepth() },
			{ "batchDepth", batchDepth() }
		} );
	};
	registerCommand( statusCommand );

	CommandDescriptor rollbackCommand;
	rollbackCommand.name = "history.rollbackBatch";
	rollbackCommand.summary = "Roll back the entire active batch, including nested batches.";
	rollbackCommand.argsSchema = historySchema();
	rollbackCommand.mutability = Mutability::Mutating;
	rollbackCommand.handler = [this]( const QJsonObject &arguments )
	{
		if( arguments.value( "dryRun" ).toBool() )
		{
			return CommandResult::success( QJsonObject{ { "wouldRollback", isBatchActive() }, { "batchDepth", batchDepth() } } );
		}
		return rollbackBatch();
	};
	registerCommand( rollbackCommand );

	registerCoreCommands( *this );
	registerExportCommands( *this );
}




bool CommandBus::registerCommand( const CommandDescriptor &descriptor )
{
	if( !onMainThread() || descriptor.name.isEmpty() || descriptor.summary.isEmpty() ||
		!descriptor.handler || m_commands.contains( descriptor.name ) )
	{
		return false;
	}

	auto normalized = descriptor;
	normalized.argsSchema = describeArguments( normalized.argsSchema, normalized.mutability != Mutability::ReadOnly );
	m_commands.insert( normalized.name, normalized );
	return true;
}




CommandResult CommandBus::execute( const QString &name, const QJsonObject &arguments )
{
	if( !onMainThread() )
	{
		return CommandResult::failure( "wrong_thread", "Commands must execute on the main thread." );
	}

	const auto command = m_commands.constFind( name );
	if( command == m_commands.cend() )
	{
		if( isBatchActive() ) { rollbackBatch(); }
		return CommandResult::failure( "unknown_command", "The requested command is not registered.",
			"Use agent.listCommands when it becomes available." );
	}

	const auto validationError = validateArguments( command->argsSchema, arguments );
	if( !validationError.isEmpty() )
	{
		if( isBatchActive() ) { rollbackBatch(); }
		return CommandResult::failure( "invalid_arguments", validationError );
	}
	// Copy before calling a handler: registration during a handler must not invalidate the QHash iterator.
	const auto descriptor = command.value();
	const bool rendering = hasActiveAudioExport() || (Engine::getSong() && Engine::getSong()->isExporting());
	if( rendering && descriptor.mutability != Mutability::ReadOnly && name != "export.cancel" )
	{
		return CommandResult::failure( "export_busy", "Finish or cancel the active audio export before editing the project." );
	}
	// Keep preview models inaccessible to rendering, and read models between render periods.
	const auto previewGuard = Engine::audioEngine() &&
		(arguments.value( "dryRun" ).toBool() || (rendering && descriptor.mutability == Mutability::ReadOnly && name != "export.status"))
		? Engine::audioEngine()->requestChangesGuard() : AudioEngine::RequestChangesGuard{};
	const bool dryRun = arguments.value( "dryRun" ).toBool();
	const bool transactional = descriptor.mutability != Mutability::ReadOnly && descriptor.scope != TxScope::None;
	const bool ownsBatch = transactional && ( dryRun || !isBatchActive() );
	if( ownsBatch && !beginBatch( descriptor.name ) )
	{
		return CommandResult::failure( "transaction_unavailable", "Could not create a project transaction." );
	}

	CommandResult result;
	const auto before = dryRun ? projectFields() : ProjectFields{};
	try
	{
		result = descriptor.handler( arguments );
		if( result.ok && dryRun )
		{
			result.data.insert( "dryRun", true );
			result.data.insert( "diff", projectDiff( name, before, projectFields() ) );
		}
	}
	catch( const std::exception &exception )
	{
		result = CommandResult::failure( "command_exception", QString::fromUtf8( exception.what() ) );
	}
	catch( ... )
	{
		result = CommandResult::failure( "command_exception", "The command threw an unknown exception." );
	}

	if( !result.ok )
	{
		if( isBatchActive() )
		{
			const auto rollback = rollbackBatch();
			if( !rollback.ok ) { return rollback; }
		}
		return result;
	}

	if( ownsBatch )
	{
		const auto transactionResult = endBatch( !dryRun );
		if( !transactionResult.ok )
		{
			return transactionResult;
		}
	}

	return result;
}




QList<CommandDescriptor> CommandBus::descriptors() const
{
	auto result = m_commands.values();
	std::sort( result.begin(), result.end(), []( const CommandDescriptor &left, const CommandDescriptor &right )
	{
		return left.name < right.name;
	} );
	return result;
}




bool CommandBus::beginBatch( const QString &label )
{
	if( !onMainThread() || Engine::projectJournal() == nullptr || Engine::getSong() == nullptr )
	{
		return false;
	}
	if( hasActiveAudioExport() || Engine::getSong()->isExporting() ) { return false; }
	if( !Engine::projectJournal()->beginTransaction( Engine::getSong() ) )
	{
		return false;
	}

	m_batches.push_back( Batch{ label } );
	return true;
}




CommandResult CommandBus::endBatch( bool success )
{
	if( !onMainThread() )
	{
		return CommandResult::failure( "wrong_thread", "Batches must execute on the main thread." );
	}
	if( !isBatchActive() || Engine::projectJournal() == nullptr )
	{
		return CommandResult::failure( "no_active_transaction", "There is no active project transaction." );
	}

	const QString label = m_batches.back().label;
	const bool completed = success ? Engine::projectJournal()->commitTransaction()
		: Engine::projectJournal()->rollbackTransaction();
	m_batches.pop_back();
	if( !completed )
	{
		return CommandResult::failure( "transaction_failed", "Could not finalize the project transaction." );
	}

	return CommandResult::success( QJsonObject{ { "label", label }, { "batchDepth", batchDepth() } } );
}




bool CommandBus::isBatchActive() const
{
	return !m_batches.empty();
}

int CommandBus::batchDepth() const
{
	return static_cast<int>( m_batches.size() );
}

CommandResult CommandBus::rollbackBatch()
{
	if( !onMainThread() )
	{
		return CommandResult::failure( "wrong_thread", "Batches must execute on the main thread." );
	}
	if( !isBatchActive() )
	{
		return CommandResult::failure( "no_active_transaction", "There is no active project transaction." );
	}
	CommandResult result;
	while( isBatchActive() )
	{
		result = endBatch( false );
		if( !result.ok ) { return result; }
	}
	return result;
}

} // namespace lmms::agent
