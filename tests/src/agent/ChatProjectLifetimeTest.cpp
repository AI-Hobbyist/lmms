#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWidget>
#include <QtTest>
#include <memory>

#include "AudioDummy.h"
#include "ConfigManager.h"
#include "DataFile.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "InstrumentTrackView.h"
#include "MainWindow.h"
#include "MidiDummy.h"
#include "PatternEditor.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "Track.h"
#include "agent/CommandBus.h"

class ChatProjectLifetimeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
		QVERIFY(m_profile.isValid());
		auto* config = lmms::ConfigManager::inst();
		config->loadConfigFile(m_profile.filePath("config.xml"));
		config->setWorkingDir(m_profile.path() + '/');
		config->setValue("app", "configured", "1");
		config->setValue("audioengine", "audiodev", lmms::AudioDummy::name());
		config->setValue("audioengine", "mididev", lmms::MidiDummy::name());
		m_gui = std::make_unique<lmms::gui::GuiApplication>();
		auto* window = m_gui->mainWindow();
		window->resize(1200, 800);
		window->show();
		QVERIFY(QTest::qWaitForWindowExposed(window));
		lmms::agent::CommandDescriptor probe;
		probe.name = "test.incrementalFailure";
		probe.summary = "Exercise failures after a real nested edit.";
		probe.mutability = lmms::agent::Mutability::Mutating;
		probe.scope = lmms::agent::TxScope::Batch;
		probe.incremental = true;
		probe.argsSchema = {{"type", "object"},
							{"properties", QJsonObject{{"mode", QJsonObject{{"type", "string"}}}}},
							{"required", QJsonArray{"mode"}}};
		probe.handler = [](const QJsonObject& arguments) {
			auto result
				= lmms::agent::CommandBus::instance().execute("track.setName", {{"track", 0}, {"value", "Partial"}});
			if (!result.ok)
			{
				return result;
			}
			const auto mode = arguments.value("mode").toString();
			if (mode == "exception")
			{
				throw std::runtime_error("Failure after a real edit");
			}
			if (mode == "reset")
			{
				lmms::Engine::getSong()->clearProject();
			}
			else if (mode == "reset-new")
			{
				lmms::Engine::getSong()->createNewProject();
			}
			else if (mode == "reset-template")
			{
				lmms::Engine::getSong()->createNewProjectFromTemplate("not-read.mpt");
			}
			else if (mode == "reset-load")
			{
				lmms::Engine::getSong()->loadProject("not-read.mmp");
			}
			else if (mode == "reset-restore")
			{
				lmms::DataFile state(lmms::DataFile::Type::SongProject);
				lmms::Engine::getSong()->restoreProjectState(state);
			}
			return lmms::agent::CommandResult::failure("test_failure", "Failure after a real edit");
		};
		QVERIFY(lmms::agent::CommandBus::instance().registerCommand(probe));
	}

	void cleanupTestCase()
	{
		delete static_cast<QWidget*>(m_gui->mainWindow());
		m_gui.reset();
	}

	void init()
	{
		lmms::Engine::getSong()->clearProject();
		lmms::Engine::projectJournal()->clearJournal();
	}

	void resetNotifiesBeforeDeletion()
	{
		auto* song = lmms::Engine::getSong();
		auto& bus = lmms::agent::CommandBus::instance();
		QVERIFY(bus.execute("track.create", {{"type", "sample"}, {"name", "Original"}}).ok);
		QPointer<lmms::Track> original(song->tracks().front());
		bool guarded = false;
		bool originalStillAlive = false;
		QString queryError;
		const auto connection = connect(song, &lmms::Song::projectAboutToReset, this, [&] {
			guarded = song->isLoadingProject();
			originalStillAlive = !original.isNull();
			queryError = bus.execute("query.songSummary").errorCode;
		});
		QSignalSpy reset(song, &lmms::Song::projectAboutToReset);
		song->clearProject();
		disconnect(connection);
		QCOMPARE(reset.count(), 1);
		QVERIFY(guarded);
		QVERIFY(originalStillAlive);
		QCOMPARE(queryError, QStringLiteral("project_busy"));
		QVERIFY(original.isNull());
		QVERIFY(!song->isLoadingProject());
	}

	void patternTracksSurviveAgentTransactions_data()
	{
		QTest::addColumn<QString>("operation");
		for (const auto* operation : {"success", "dryRun", "undo", "redo", "failed-batch"})
		{
			QTest::newRow(operation) << QString::fromLatin1(operation);
		}
	}

	void exportsRegisteredCommandInventory()
	{
		const auto path = qEnvironmentVariable("LMMS_COMMAND_INVENTORY");
		if (path.isEmpty())
		{
			return;
		}
		QJsonArray commands;
		for (const auto& descriptor : lmms::agent::CommandBus::instance().descriptors())
		{
			commands.append(QJsonObject{{"name", descriptor.name},
										{"summary", descriptor.summary},
										{"mutability", static_cast<int>(descriptor.mutability)},
										{"scope", static_cast<int>(descriptor.scope)},
										{"preflight", bool(descriptor.preflight)},
										{"schema", descriptor.argsSchema}});
		}
		QFile output(path);
		QVERIFY(output.open(QIODevice::WriteOnly));
		QVERIFY(output.write(QJsonDocument(commands).toJson()) > 0);
	}

	void agentOperationsNeverResetProject_data()
	{
		QTest::addColumn<QString>("operation");
		for (const auto* operation : {"dryRun", "failed-batch", "script-failure", "composition-preview", "undo",
									  "clear", "load", "script-clear", "script-load"})
		{
			QTest::newRow(operation) << QString::fromLatin1(operation);
		}
	}

	void agentOperationsNeverResetProject()
	{
		QFETCH(QString, operation);
		auto* song = lmms::Engine::getSong();
		auto& bus = lmms::agent::CommandBus::instance();
		QPointer<lmms::Track> original(lmms::Track::create(lmms::Track::Type::Sample, song));
		original->setName("Original");
		lmms::Engine::projectJournal()->clearJournal();
		QSignalSpy reset(song, &lmms::Song::projectAboutToReset);
		lmms::agent::CommandResult result;
		const QJsonObject edit{{"track", 0}, {"value", "Edited"}};
		if (operation == "dryRun")
		{
			auto arguments = edit;
			arguments.insert("dryRun", true);
			result = bus.executeForAgent("track.setName", arguments);
			QVERIFY(result.ok);
		}
		else if (operation == "failed-batch")
		{
			const QJsonArray steps{QJsonObject{{"cmd", "track.setName"}, {"args", edit}},
								   QJsonObject{{"cmd", "missing.command"}}};
			result = bus.executeForAgent("agent.runScript", {{"script", QJsonObject{{"steps", steps}}}});
			QVERIFY(!result.ok);
		}
		else if (operation == "script-failure")
		{
			const QJsonArray steps{QJsonObject{{"cmd", "track.setName"}, {"args", edit}},
								   QJsonObject{{"assert", QJsonObject{{"expr", false}}}}};
			result = bus.executeForAgent("agent.runScript", {{"script", QJsonObject{{"steps", steps}}}});
			QVERIFY(!result.ok);
		}
		else if (operation == "composition-preview")
		{
			result = bus.executeForAgent("compose.drumPattern", {{"bars", 1}, {"dryRun", true}});
			QVERIFY(result.ok);
		}
		else if (operation == "undo")
		{
			QVERIFY(bus.executeForAgent("track.setName", edit).ok);
			result = bus.executeForAgent("history.undo");
			QVERIFY(result.ok);
		}
		else if (operation == "clear" || operation == "script-clear")
		{
			if (operation == "script-clear")
			{
				const QJsonArray steps{QJsonObject{{"cmd", "song.clearProject"}}};
				result = bus.executeForAgent("agent.runScript", {{"script", QJsonObject{{"steps", steps}}}});
			}
			else
			{
				result = bus.executeForAgent("song.clearProject");
			}
		}
		else
		{
			lmms::DataFile snapshot(lmms::DataFile::Type::SongProject);
			song->saveProjectState(snapshot);
			const auto path = m_profile.filePath("agent-reset-contract.mmp");
			QVERIFY(snapshot.writeFile(path, false));
			if (operation == "script-load")
			{
				const QJsonArray steps{QJsonObject{{"cmd", "song.load"}, {"args", QJsonObject{{"path", path}}}}};
				result = bus.executeForAgent("agent.runScript", {{"script", QJsonObject{{"steps", steps}}}});
			}
			else
			{
				result = bus.executeForAgent("song.load", {{"path", path}});
			}
		}
		QCOMPARE(reset.count(), 0);
		QVERIFY(!original.isNull());
		QCOMPARE(song->tracks().front(), original.data());
		QCOMPARE(original->name(), QStringLiteral("Original"));
		QCOMPARE(bus.batchDepth(), 0);
		if (operation == "clear" || operation == "load" || operation == "script-clear" || operation == "script-load")
		{
			QVERIFY(!result.ok);
			QCOMPARE(result.errorCode, QStringLiteral("agent_project_reset_forbidden"));
		}
	}

	void incrementalFailuresPreserveObjects_data()
	{
		QTest::addColumn<QString>("mode");
		for (const auto* mode :
			 {"return", "exception", "reset", "reset-new", "reset-template", "reset-load", "reset-restore"})
		{
			QTest::newRow(mode) << QString::fromLatin1(mode);
		}
	}

	void incrementalFailuresPreserveObjects()
	{
		QFETCH(QString, mode);
		auto* song = lmms::Engine::getSong();
		QPointer<lmms::Track> track(lmms::Track::create(lmms::Track::Type::Sample, song));
		track->setName("Original");
		auto* journal = lmms::Engine::projectJournal();
		journal->clearJournal();
		song->setModified(false);
		QSignalSpy reset(song, &lmms::Song::projectAboutToReset);
		const auto result
			= lmms::agent::CommandBus::instance().executeForAgent("test.incrementalFailure", {{"mode", mode}});
		QVERIFY(!result.ok);
		QCOMPARE(result.errorCode,
				 mode.startsWith("reset")  ? QStringLiteral("agent_project_reset_forbidden")
					 : mode == "exception" ? QStringLiteral("command_exception")
										   : QStringLiteral("test_failure"));
		QCOMPARE(reset.count(), 0);
		QVERIFY(track);
		QCOMPARE(track->name(), QStringLiteral("Original"));
		QVERIFY(!song->isModified());
		QCOMPARE(journal->transactionDepth(), 0);
		QCOMPARE(journal->undoDepth(), 0);
	}

	void incrementalPreviewPreservesRedoAndParentChanges()
	{
		auto* song = lmms::Engine::getSong();
		QPointer<lmms::Track> track(lmms::Track::create(lmms::Track::Type::Sample, song));
		track->setName("Original");
		auto& bus = lmms::agent::CommandBus::instance();
		auto* journal = lmms::Engine::projectJournal();
		journal->clearJournal();
		QVERIFY(bus.executeForAgent("track.setName", {{"track", 0}, {"value", "Committed"}}).ok);
		QVERIFY(bus.executeForAgent("history.undo").ok);
		QCOMPARE(journal->redoDepth(), 1);
		song->setModified(false);
		QSignalSpy reset(song, &lmms::Song::projectAboutToReset);
		QVERIFY(bus.executeForAgent("track.setName", {{"track", 0}, {"value", "Preview"}, {"dryRun", true}}).ok);
		QVERIFY(track);
		QCOMPARE(track->name(), QStringLiteral("Original"));
		QCOMPARE(journal->redoDepth(), 1);
		QVERIFY(!song->isModified());
		QVERIFY(bus.executeForAgent("history.redo").ok);
		QCOMPARE(track->name(), QStringLiteral("Committed"));
		QVERIFY(bus.executeForAgent("history.undo").ok);
		QCOMPARE(track->name(), QStringLiteral("Original"));
		QCOMPARE(reset.count(), 0);

		// A local dry-run savepoint must not discard an already applied parent edit.
		QVERIFY(journal->beginIncrementalTransaction(song));
		QVERIFY(
			journal->addIncrementalChange({[track] { return !track.isNull(); }, [track] { track->setName("Original"); },
										   [track] { track->setName("Parent"); }}));
		track->setName("Parent");
		QVERIFY(bus.executeForAgent("track.setName", {{"track", 0}, {"value", "Preview"}, {"dryRun", true}}).ok);
		QCOMPARE(track->name(), QStringLiteral("Parent"));
		QCOMPARE(journal->transactionDepth(), 1);
		QVERIFY(journal->rollbackTransaction());
		QCOMPARE(track->name(), QStringLiteral("Original"));
	}

	void agentRejectsLegacyHistoryWithoutPoppingIt()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		auto* journal = lmms::Engine::projectJournal();
		QVERIFY(bus.execute("song.setTempo", {{"bpm", 133}}).ok);
		const auto undoDepth = journal->undoDepth();
		QSignalSpy reset(lmms::Engine::getSong(), &lmms::Song::projectAboutToReset);
		const auto result = bus.executeForAgent("history.undo");
		QVERIFY(!result.ok);
		QCOMPARE(result.errorCode, QStringLiteral("agent_history_not_incremental"));
		QCOMPARE(journal->undoDepth(), undoDepth);
		QCOMPARE(reset.count(), 0);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(reset.count(), 1);
	}

	void patternViewDeletionKeepsRegistryEmpty()
	{
		auto* song = lmms::Engine::getSong();
		QPointer<lmms::Track> pattern(lmms::Track::create(lmms::Track::Type::Pattern, song));
		QVERIFY(patternRegistryMatchesSong());
		song->clearProject();
		QVERIFY(pattern.isNull());
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QVERIFY2(patternRegistryMatchesSong(), "Deleting the Pattern view must not reinsert a deleted track.");
	}

	void queuedTrackCreationSkipsDeletedModel()
	{
		QPointer<lmms::Track> track(lmms::Track::create(lmms::Track::Type::Instrument, lmms::Engine::patternStore()));
		delete track.data();
		QVERIFY(track.isNull());
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
		QVERIFY(m_gui->patternEditor()->m_editor->findChildren<lmms::gui::InstrumentTrackView*>().isEmpty());
		QVERIFY(patternRegistryMatchesSong());
	}

	void patternTracksSurviveAgentTransactions()
	{
		QFETCH(QString, operation);
		auto* song = lmms::Engine::getSong();
		auto& bus = lmms::agent::CommandBus::instance();
		lmms::Track::create(lmms::Track::Type::Pattern, song);
		lmms::Track::create(lmms::Track::Type::Pattern, song);
		lmms::Track::create(lmms::Track::Type::Instrument, lmms::Engine::patternStore());
		const auto originalName = song->tracks().front()->name();
		QVERIFY(patternRegistryMatchesSong());
		QTemporaryDir directory;
		lmms::DataFile snapshot(lmms::DataFile::Type::SongProject);
		song->saveProjectState(snapshot);
		const auto templatePath = directory.filePath("pattern-template.mpt");
		QVERIFY(snapshot.writeFile(templatePath, false));
		const auto defaultDirectory = lmms::ConfigManager::inst()->userTemplateDir();
		QVERIFY(QDir::cleanPath(defaultDirectory).startsWith(QDir::cleanPath(m_profile.path()), Qt::CaseInsensitive));
		QVERIFY(QDir().mkpath(defaultDirectory));
		QVERIFY(snapshot.writeFile(QDir(defaultDirectory).filePath("default.mpt"), false));
		if (operation == "failed-batch")
		{
			QVERIFY(bus.beginBatch("Pattern failure reproduction"));
		}
		QVERIFY(
			bus.execute("track.setName", {{"track", 0}, {"value", "Edited pattern"}, {"dryRun", operation == "dryRun"}})
				.ok);
		if (operation == "undo" || operation == "redo")
		{
			QVERIFY(bus.execute("history.undo").ok);
			QVERIFY(patternRegistryMatchesSong());
		}
		if (operation == "redo")
		{
			QVERIFY(bus.execute("history.redo").ok);
		}
		if (operation == "failed-batch")
		{
			QVERIFY(!bus.execute("missing.command").ok);
		}
		QVERIFY2(patternRegistryMatchesSong(), qPrintable("Pattern registry diverged after " + operation));
		const bool keepsEdit = operation == "success" || operation == "redo";
		QCOMPARE(song->tracks().front()->name(), keepsEdit ? QStringLiteral("Edited pattern") : originalName);
		song->createNewProjectFromTemplate(templatePath);
		QVERIFY(patternRegistryMatchesSong());
		QCOMPARE(song->countTracks(lmms::Track::Type::Pattern), 2);
		song->createNewProject();
		QVERIFY(patternRegistryMatchesSong());
		QCOMPARE(song->countTracks(lmms::Track::Type::Pattern), 2);
		QCOMPARE(song->tracks().front()->name(), originalName);
	}

	void loadTemplateAndRestoreNotify()
	{
		auto* song = lmms::Engine::getSong();
		lmms::DataFile snapshot(lmms::DataFile::Type::SongProject);
		song->saveProjectState(snapshot);
		QTemporaryDir directory;
		const auto path = directory.filePath("empty.mmp");
		QVERIFY(snapshot.writeFile(path, false));
		QSignalSpy reset(song, &lmms::Song::projectAboutToReset);
		QSignalSpy loaded(song, &lmms::Song::projectLoaded);
		song->restoreProjectState(snapshot);
		QCOMPARE(reset.count(), 1);
		song->loadProject(path);
		QCOMPARE(reset.count(), 2);
		QCOMPARE(loaded.count(), 1);
		song->createNewProjectFromTemplate(path);
		QCOMPARE(reset.count(), 3);
		QCOMPARE(loaded.count(), 2);
		QVERIFY(song->projectFileName().isEmpty());
	}

	void editKeepsObjectsButUndoRedoNotify()
	{
		auto* song = lmms::Engine::getSong();
		auto& bus = lmms::agent::CommandBus::instance();
		QSignalSpy reset(song, &lmms::Song::projectAboutToReset);
		QVERIFY(bus.execute("track.create", {{"type", "sample"}, {"name", "Original"}}).ok);
		QPointer<lmms::Track> original(song->tracks().front());
		QVERIFY(bus.execute("track.setName", {{"track", 0}, {"value", "Renamed"}}).ok);
		QCOMPARE(reset.count(), 0);
		QVERIFY(!original.isNull());
		QCOMPARE(song->tracks().front(), original.data());
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(reset.count(), 1);
		QVERIFY(original.isNull());
		QVERIFY(bus.execute("history.redo").ok);
		QCOMPARE(reset.count(), 2);
		QCOMPARE(song->tracks().front()->name(), QStringLiteral("Renamed"));
	}

private:
	bool patternRegistryMatchesSong() const
	{
		auto* song = lmms::Engine::getSong();
		QSet<lmms::Track*> patterns;
		for (auto* track : song->tracks())
		{
			if (track->type() == lmms::Track::Type::Pattern)
			{
				patterns.insert(track);
			}
		}
		const int count = patterns.size();
		for (int index = 0; index < count; ++index)
		{
			if (!patterns.remove(lmms::PatternTrack::findPatternTrack(index)))
			{
				return false;
			}
		}
		return patterns.isEmpty() && lmms::PatternTrack::findPatternTrack(count) == nullptr;
	}

	QTemporaryDir m_profile;
	std::unique_ptr<lmms::gui::GuiApplication> m_gui;
};

QTEST_MAIN(ChatProjectLifetimeTest)
#include "ChatProjectLifetimeTest.moc"
