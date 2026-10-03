#include <QtTest>

#include <QJsonArray>
#include <memory>
#include <thread>

#include "agent/CommandBus.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "Track.h"

namespace
{

constexpr int InitialTempo = 120;

QJsonObject tempoSchema()
{
	return {
		{ "type", "object" },
		{ "properties", QJsonObject{
			{ "bpm", QJsonObject{ { "type", "integer" } } }
		} },
		{ "required", QJsonArray{ "bpm" } }
	};
}

lmms::agent::CommandDescriptor makeTempoCommand( const QString &name, bool fail )
{
	using namespace lmms::agent;

	CommandDescriptor descriptor;
	descriptor.name = name;
	descriptor.summary = "Set the project tempo for CommandBus tests.";
	descriptor.argsSchema = tempoSchema();
	descriptor.mutability = Mutability::Mutating;
	descriptor.scope = TxScope::Single;
	descriptor.handler = [fail]( const QJsonObject &arguments )
	{
		lmms::Engine::getSong()->tempoModel().setValue( arguments.value( "bpm" ).toInt() );
		if( fail )
		{
			return CommandResult::failure( "test_failure", "The test command failed after changing the tempo." );
		}
		return CommandResult::success( QJsonObject{
			{ "tempo", lmms::Engine::getSong()->tempoModel().value() }
		} );
	};
	return descriptor;
}

} // namespace

class CommandBusTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		lmms::Engine::init( true );

		auto &bus = lmms::agent::CommandBus::instance();
		QVERIFY( bus.registerCommand( makeTempoCommand( "test.setTempo", false ) ) );
		QVERIFY( bus.registerCommand( makeTempoCommand( "test.setTempoThenFail", true ) ) );
	}

	void cleanupTestCase()
	{
		lmms::Engine::destroy();
	}

	void init()
	{
		auto *journal = lmms::Engine::projectJournal();
		journal->clearJournal();
		const bool journalling = journal->isJournalling();
		journal->setJournalling( false );
		lmms::Engine::getSong()->tempoModel().setValue( InitialTempo );
		journal->setJournalling( journalling );
		journal->clearJournal();
	}

	void registersDescriptorsAndReportsUnknownCommands()
	{
		const auto descriptors = lmms::agent::CommandBus::instance().descriptors();
		bool hasUndo = false;
		bool hasRedo = false;
		bool hasTempo = false;
		for( const auto &descriptor : descriptors )
		{
			hasUndo = hasUndo || descriptor.name == "history.undo";
			hasRedo = hasRedo || descriptor.name == "history.redo";
			hasTempo = hasTempo || descriptor.name == "test.setTempo";
		}

		QVERIFY( hasUndo );
		QVERIFY( hasRedo );
		QVERIFY( hasTempo );
		const auto unknown = lmms::agent::CommandBus::instance().execute( "test.missing" );
		QVERIFY( !unknown.ok );
		QCOMPARE( unknown.errorCode, QString( "unknown_command" ) );
	}

	void commitsSingleCommandAndSupportsHistory()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		auto *journal = lmms::Engine::projectJournal();

		const auto result = bus.execute( "test.setTempo", QJsonObject{ { "bpm", 132 } } );
		QVERIFY( result.ok );
		QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), 132.0f );
		QVERIFY( journal->canUndo() );

		QVERIFY( bus.execute( "history.undo" ).ok );
		QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), static_cast<float>( InitialTempo ) );
		QVERIFY( bus.execute( "history.redo" ).ok );
		QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), 132.0f );
	}

	void rollsBackAFailedCommand()
	{
		auto *journal = lmms::Engine::projectJournal();
		QSignalSpy projectLoadedSpy( lmms::Engine::getSong(), &lmms::Song::projectLoaded );
		QVERIFY( projectLoadedSpy.isValid() );
		const auto result = lmms::agent::CommandBus::instance().execute(
			"test.setTempoThenFail", QJsonObject{ { "bpm", 140 } } );

		QVERIFY( !result.ok );
		QCOMPARE( result.errorCode, QString( "test_failure" ) );
		QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), static_cast<float>( InitialTempo ) );
		QVERIFY( !journal->canUndo() );
		QVERIFY( !journal->canRedo() );
		QCOMPARE( projectLoadedSpy.count(), 0 );
	}

	void dryRunRestoresStateAndPreservesRedoHistory()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		auto *journal = lmms::Engine::projectJournal();

		QVERIFY( bus.execute( "test.setTempo", QJsonObject{ { "bpm", 132 } } ).ok );
		QVERIFY( bus.execute( "history.undo" ).ok );
		QVERIFY( journal->canRedo() );

		const auto result = bus.execute( "test.setTempo", QJsonObject{
			{ "bpm", 144 },
			{ "dryRun", true }
		} );
		QVERIFY( result.ok );
		QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), static_cast<float>( InitialTempo ) );
		QVERIFY( !journal->canUndo() );
		QVERIFY( journal->canRedo() );

		QVERIFY( bus.execute( "history.redo" ).ok );
		QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), 132.0f );
	}

	void batchesChangesIntoOneUndoStep()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		auto *journal = lmms::Engine::projectJournal();

		QVERIFY( bus.beginBatch( "two tempo changes" ) );
		QVERIFY( bus.execute( "test.setTempo", QJsonObject{ { "bpm", 126 } } ).ok );
		QVERIFY( bus.execute( "test.setTempo", QJsonObject{ { "bpm", 132 } } ).ok );
		QVERIFY( bus.endBatch( true ).ok );
		QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), 132.0f );
		QVERIFY( journal->canUndo() );

		QVERIFY( bus.execute( "history.undo" ).ok );
		QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), static_cast<float>( InitialTempo ) );
	}

	void validatesArgumentsBeforeChangingTheProject()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		for( const auto &arguments : {
			QJsonObject{ { "bpm", 140 }, { "dryRun", "true" } },
			QJsonObject{ { "bpm", "140" } },
			QJsonObject{ { "bpm", 140 }, { "misspelledArgument", true } },
			QJsonObject{ { "bpm", 140.5 } }, QJsonObject{} } )
		{
			const auto result = bus.execute( "test.setTempo", arguments );
			QVERIFY( !result.ok );
			QCOMPARE( result.errorCode, QString( "invalid_arguments" ) );
			QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), static_cast<float>( InitialTempo ) );
			QCOMPARE( lmms::Engine::projectJournal()->undoDepth(), 0 );
		}
	}

	void descriptorsDocumentRequiredArgumentsAndPreviews()
	{
		for( const auto &descriptor : lmms::agent::CommandBus::instance().descriptors() )
		{
			QVERIFY( !descriptor.summary.isEmpty() );
			QCOMPARE( descriptor.argsSchema.value( "type" ).toString(), QString( "object" ) );
			const auto properties = descriptor.argsSchema.value( "properties" ).toObject();
			for( const auto &required : descriptor.argsSchema.value( "required" ).toArray() )
			{
				QVERIFY( properties.contains( required.toString() ) );
				QVERIFY( !properties.value( required.toString() ).toObject().value( "description" ).toString().isEmpty() );
			}
			if( descriptor.mutability != lmms::agent::Mutability::ReadOnly )
			{
				QCOMPARE( properties.value( "dryRun" ).toObject().value( "type" ).toString(), QString( "boolean" ) );
			}
		}
	}

	void previewsInsideABatchWithoutLosingPreviousChanges()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		QVERIFY( bus.beginBatch( "outer" ) );
		QVERIFY( bus.execute( "test.setTempo", QJsonObject{ { "bpm", 130 } } ).ok );
		const auto preview = bus.execute( "test.setTempo", QJsonObject{ { "bpm", 150 }, { "dryRun", true } } );
		QVERIFY( preview.ok );
		QVERIFY( preview.data.value( "dryRun" ).toBool() );
		const auto changes = preview.data.value( "diff" ).toObject().value( "changes" ).toArray();
		QVERIFY( !changes.isEmpty() );
		bool hasTempoChange = false;
		for( const auto &change : changes )
		{
			const auto entry = change.toObject();
			hasTempoChange = hasTempoChange || ( entry.value( "before" ).toString() == "130" &&
				entry.value( "after" ).toString() == "150" && entry.value( "op" ).toString() == "replace" );
		}
		QVERIFY( hasTempoChange );
		QCOMPARE( bus.batchDepth(), 1 );
		QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), 130.0f );
		QCOMPARE( lmms::Engine::projectJournal()->undoDepth(), 0 );
		QVERIFY( bus.endBatch( true ).ok );
		QCOMPARE( lmms::Engine::projectJournal()->undoDepth(), 1 );
		QVERIFY( bus.execute( "history.undo" ).ok );
		QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), static_cast<float>( InitialTempo ) );
	}

	void commitsNestedBatchesAsOneUndoStep()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		QVERIFY( bus.beginBatch( "outer" ) );
		QVERIFY( bus.execute( "test.setTempo", QJsonObject{ { "bpm", 130 } } ).ok );
		QVERIFY( bus.beginBatch( "inner" ) );
		QVERIFY( bus.execute( "test.setTempo", QJsonObject{ { "bpm", 150 } } ).ok );
		QVERIFY( bus.endBatch( true ).ok );
		QCOMPARE( lmms::Engine::projectJournal()->undoDepth(), 0 );
		QVERIFY( bus.endBatch( true ).ok );
		QCOMPARE( lmms::Engine::projectJournal()->undoDepth(), 1 );
		QVERIFY( bus.execute( "history.undo" ).ok );
		QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), static_cast<float>( InitialTempo ) );
		QVERIFY( bus.execute( "history.redo" ).ok );
		QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), 150.0f );
	}

	void failedNestedCommandRollsBackTheWholeBatch()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		QVERIFY( bus.beginBatch( "outer" ) );
		QVERIFY( bus.execute( "test.setTempo", QJsonObject{ { "bpm", 130 } } ).ok );
		QVERIFY( bus.beginBatch( "inner" ) );
		QVERIFY( !bus.execute( "test.setTempoThenFail", QJsonObject{ { "bpm", 150 } } ).ok );
		QCOMPARE( bus.batchDepth(), 0 );
		QCOMPARE( lmms::Engine::projectJournal()->transactionDepth(), 0 );
		QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), static_cast<float>( InitialTempo ) );
		QCOMPARE( lmms::Engine::projectJournal()->undoDepth(), 0 );
	}

	void validationFailureRollsBackAnActiveBatch()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		QVERIFY( bus.beginBatch( "invalid arguments" ) );
		QVERIFY( bus.execute( "test.setTempo", QJsonObject{ { "bpm", 130 } } ).ok );
		QVERIFY( !bus.execute( "test.setTempo", QJsonObject{ { "bpm", "invalid" } } ).ok );
		QVERIFY( !bus.isBatchActive() );
		QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), static_cast<float>( InitialTempo ) );
	}

	void reportsHistoryAndExplicitlyRollsBackBatches()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		QVERIFY( bus.beginBatch( "rollback" ) );
		QVERIFY( bus.execute( "test.setTempo", QJsonObject{ { "bpm", 130 } } ).ok );
		QCOMPARE( bus.execute( "history.status" ).data.value( "batchDepth" ).toInt(), 1 );
		QVERIFY( bus.execute( "history.rollbackBatch", QJsonObject{ { "dryRun", true } } ).ok );
		QVERIFY( bus.isBatchActive() );
		QVERIFY( bus.execute( "history.rollbackBatch" ).ok );
		const auto status = bus.execute( "history.status" );
		QVERIFY( status.ok );
		QCOMPARE( status.data.value( "batchDepth" ).toInt(), 0 );
		QCOMPARE( status.data.value( "undoDepth" ).toInt(), 0 );
		QCOMPARE( lmms::Engine::getSong()->tempoModel().value(), static_cast<float>( InitialTempo ) );
	}

	void rejectsWorkerThreadTransactionAccess()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		QVERIFY( bus.beginBatch( "main thread batch" ) );
		bool began = true;
		lmms::agent::CommandResult ended;
		lmms::agent::CommandResult executed;
		std::thread worker( [&]
		{
			began = bus.beginBatch( "worker" );
			ended = bus.endBatch( false );
			executed = bus.execute( "test.setTempo", QJsonObject{ { "bpm", 140 } } );
		} );
		worker.join();
		QVERIFY( !began );
		QCOMPARE( ended.errorCode, QString( "wrong_thread" ) );
		QCOMPARE( executed.errorCode, QString( "wrong_thread" ) );
		QCOMPARE( bus.batchDepth(), 1 );
		QVERIFY( bus.endBatch( false ).ok );
	}

	void previewsPreserveTransportAndModifiedState()
	{
		// Freeze normal audio progression while comparing exact transport positions.
		const auto guard = lmms::Engine::audioEngine()->requestChangesGuard();
		auto &bus = lmms::agent::CommandBus::instance();
		auto *song = lmms::Engine::getSong();
		song->playSong();
		song->setPlayPos( 384, lmms::Song::PlayMode::Song );
		song->getTimeline( lmms::Song::PlayMode::Song ).setFrameOffset( 0.5f );
		song->setPlayPos( 192, lmms::Song::PlayMode::Pattern );
		const auto wasModified = song->isModified();
		const auto seconds = song->getTimeline( lmms::Song::PlayMode::Song ).getElapsedSeconds();
		QVERIFY( bus.execute( "test.setTempo", QJsonObject{ { "bpm", 150 }, { "dryRun", true } } ).ok );
		QVERIFY( song->isPlaying() );
		QCOMPARE( song->playMode(), lmms::Song::PlayMode::Song );
		QCOMPARE( song->getPlayPos( lmms::Song::PlayMode::Song ).getTicks(), 384 );
		QCOMPARE( song->getPlayPos( lmms::Song::PlayMode::Pattern ).getTicks(), 192 );
		QCOMPARE( song->getTimeline( lmms::Song::PlayMode::Song ).frameOffset(), 0.5f );
		QCOMPARE( song->getTimeline( lmms::Song::PlayMode::Song ).getElapsedSeconds(), seconds );
		QCOMPARE( song->isModified(), wasModified );
		song->stop();
	}

	void validatesNestedArrayItemsWithoutCallingTheHandler()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		auto called = std::make_shared<bool>( false );
		lmms::agent::CommandDescriptor descriptor;
		descriptor.name = "test.nestedSchema";
		descriptor.summary = "Validate nested note arrays.";
		descriptor.argsSchema = QJsonObject{
			{ "type", "object" },
			{ "properties", QJsonObject{ { "notes", QJsonObject{
				{ "type", "array" }, { "items", QJsonObject{
					{ "type", "object" }, { "required", QJsonArray{ "key" } },
					{ "properties", QJsonObject{ { "key", QJsonObject{
						{ "type", "integer" }, { "minimum", 0 }, { "maximum", 127 }
					} } } }
				} }
			} } } }, { "required", QJsonArray{ "notes" } }
		};
		descriptor.handler = [called]( const QJsonObject & )
		{
			*called = true;
			return lmms::agent::CommandResult::success();
		};
		QVERIFY( bus.registerCommand( descriptor ) );
		QVERIFY( !bus.execute( descriptor.name, QJsonObject{ { "notes", QJsonArray{ QJsonObject{ { "key", 128 } } } } } ).ok );
		QVERIFY( !*called );
		QVERIFY( bus.execute( descriptor.name, QJsonObject{ { "notes", QJsonArray{ QJsonObject{ { "key", 60 } } } } } ).ok );
		QVERIFY( *called );
	}

	void previewPreservesObjectIdsAndNativeRedoHistory()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		auto *song = lmms::Engine::getSong();
		auto *journal = lmms::Engine::projectJournal();
		auto *track = lmms::Track::create( lmms::Track::Type::Instrument, song );
		track->setName( "Before" );
		journal->clearJournal();
		const auto originalId = track->id();
		track->addJournalCheckPoint();
		track->setName( "After" );
		QVERIFY( bus.execute( "history.undo" ).ok );
		QVERIFY( journal->canRedo() );
		QVERIFY( bus.execute( "test.setTempo", QJsonObject{ { "bpm", 150 }, { "dryRun", true } } ).ok );
		QCOMPARE( song->tracks().size(), std::size_t{ 1 } );
		QCOMPARE( song->tracks().front()->id(), originalId );
		QCOMPARE( song->tracks().front()->name(), QString( "Before" ) );
		QVERIFY( journal->canRedo() );
		const auto redo = bus.execute( "history.redo" );
		QVERIFY2( redo.ok, qPrintable( redo.errorCode + ": " + redo.errorMessage ) );
		QCOMPARE( song->tracks().front()->name(), QString( "After" ) );
		song->clearProject();
	}
};

QTEST_GUILESS_MAIN( CommandBusTest )
#include "CommandBusTest.moc"
