#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWidget>
#include <QtTest>
#include <algorithm>
#include <memory>

#include "AudioDummy.h"
#include "ConfigManager.h"
#include "DataFile.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrack.h"
#include "Clip.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MidiDummy.h"
#include "MidiClip.h"
#include "PatternEditor.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "SongEditor.h"
#include "TrackView.h"
#include "ClipView.h"
#include "SVSClip.h"
#include "Keymap.h"
#include "Scale.h"
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

	void incrementalReplayRejectsReentrantCommands()
	{
		auto* song = lmms::Engine::getSong();
		auto* track = lmms::Track::create(lmms::Track::Type::Sample, song);
		track->setName("Original");
		auto& bus = lmms::agent::CommandBus::instance();
		QVERIFY(bus.executeForAgent("track.setName", {{"track", 0}, {"value", "Edited"}}).ok);
		int attempts = 0;
		const auto connection = connect(track, &lmms::Track::nameChanged, this, [&] {
			if (lmms::Engine::projectJournal()->isRestoring())
			{
				++attempts;
				QCOMPARE(bus.executeForAgent("song.getInfo").errorCode, QStringLiteral("project_busy"));
				QCOMPARE(bus.execute("missing.command").errorCode, QStringLiteral("project_busy"));
			}
		});
		QVERIFY(bus.executeForAgent("history.undo").ok);
		disconnect(connection);
		QCOMPARE(attempts, 1);
		QCOMPARE(track->name(), QStringLiteral("Original"));
		QCOMPARE(bus.batchDepth(), 0);
	}

	void incrementalPropertiesRestoreRelatedState_data()
	{
		QTest::addColumn<QString>("command");
		QTest::addColumn<QJsonObject>("arguments");
		QTest::addColumn<bool>("preview");
		const auto add = [](const char* name, QJsonObject arguments) {
			for (const bool preview : {false, true})
			{
				QTest::newRow((QString::fromLatin1(name) + (preview ? ":preview" : ":failure")).toUtf8().constData())
					<< QString::fromLatin1(name) << arguments << preview;
			}
		};
		add("song.setTempo", {{"bpm", 145}});
		add("song.setMasterVolume", {{"value", 70}});
		add("song.setMasterPitch", {{"semitones", 3}});
		add("track.setName", {{"track", 0}, {"value", "Changed"}});
		add("track.setMute", {{"track", 0}, {"value", true}});
		add("track.setMuted", {{"track", 0}, {"value", true}});
		add("track.setSolo", {{"track", 0}, {"value", true}});
		add("track.setHeight", {{"track", 0}, {"value", 90}});
		add("track.setColor", {{"track", 0}, {"value", "#123456"}});
		add("track.setMixerChannel", {{"track", 0}, {"channel", 1}});
		add("model.setValue", {{"path", "song/track:0/instrument/mixerChannel"}, {"value", 1}});
		add("instrument.setParameters", {{"track", 0}, {"volume", 50}, {"panning", -20}, {"pitchRange", 1}});
		add("instrument.setVolume", {{"track", 0}, {"value", 50}});
		add("instrument.setPanning", {{"track", 0}, {"value", -20}});
		add("instrument.setPitch", {{"track", 0}, {"value", 50}});
		add("instrument.setPitchRange", {{"track", 0}, {"value", 1}});
		add("instrument.setBaseNote", {{"track", 0}, {"value", 55}});
		add("clip.setPosition", {{"track", 0}, {"clip", 0}, {"position", 384}});
		add("clip.move", {{"track", 0}, {"clip", 0}, {"position", 384}});
		add("clip.setLength", {{"track", 0}, {"clip", 0}, {"length", 576}});
		add("clip.resize", {{"track", 0}, {"clip", 0}, {"length", 576}});
		add("clip.setMute", {{"track", 0}, {"clip", 0}, {"value", true}});
		add("clip.setColor", {{"track", 0}, {"clip", 0}, {"value", "#123456"}});
		add("clip.setAutoResize", {{"track", 0}, {"clip", 0}, {"value", false}});
		add("clip.setStartTimeOffset", {{"track", 0}, {"clip", 0}, {"value", 24}});
	}

	void incrementalPropertiesRestoreRelatedState()
	{
		QFETCH(QString, command);
		QFETCH(QJsonObject, arguments);
		QFETCH(bool, preview);
		auto* song = lmms::Engine::getSong();
		auto& bus = lmms::agent::CommandBus::instance();
		QVERIFY(bus.execute("track.create", {{"type", "instrument"}, {"name", "Original"}}).ok);
		QVERIFY(bus.execute("track.create", {{"type", "sample"}, {"name", "Unrelated"}}).ok);
		QVERIFY(bus.execute("clip.create", {{"track", 0}, {"position", 0}, {"length", 192}}).ok);
		QVERIFY(bus.execute("mixer.addChannel", {{"name", "Route"}}).ok);
		QPointer<lmms::Track> original(song->tracks()[0]);
		QPointer<lmms::Track> unrelated(song->tracks()[1]);
		QPointer<lmms::Clip> clip(original->getClips()[0]);
		clip->setAutoResize(true);
		auto* instrument = qobject_cast<lmms::InstrumentTrack*>(original.data());
		instrument->mixerChannelModel()->setRange(0, 128, 1);
		instrument->pitchRangeModel()->setValue(12);
		instrument->pitchModel()->setValue(900);
		original->setMutedBeforeSolo(true);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
		QTest::qWait(10);
		lmms::Engine::projectJournal()->clearJournal();
		song->setModified(false);
		lmms::DataFile before(lmms::DataFile::Type::SongProject);
		song->saveProjectState(before);
		// Fixture serialization itself sets the modified flag; establish the
		// transaction's initial dirty/history state after preparing that baseline.
		lmms::Engine::projectJournal()->clearJournal();
		song->setModified(false);
		QVERIFY(!song->isModified());
		QSignalSpy reset(song, &lmms::Song::projectAboutToReset);
		if (preview)
		{
			arguments.insert("dryRun", true);
			const auto result = bus.executeForAgent(command, arguments);
			QVERIFY2(result.ok, qPrintable(result.errorCode + ": " + result.errorMessage));
		}
		else
		{
			const QJsonArray steps{QJsonObject{{"cmd", command}, {"args", arguments}},
								   QJsonObject{{"assert", QJsonObject{{"expr", false}}}}};
			const auto result = bus.executeForAgent("agent.runScript", {{"script", QJsonObject{{"steps", steps}}}});
			QVERIFY(!result.ok);
			QCOMPARE(result.errorCode, QStringLiteral("script_assertion_failed"));
		}
		QCOMPARE(reset.count(), 0);
		QVERIFY(original && unrelated && clip);
		QCOMPARE(song->tracks()[0], original.data());
		QCOMPARE(song->tracks()[1], unrelated.data());
		QCOMPARE(original->getClips()[0], clip.data());
		QCOMPARE(instrument->pitchRangeModel()->value(), 12);
		QCOMPARE(instrument->pitchModel()->value(), 900.0f);
		QCOMPARE(instrument->mixerChannelModel()->maxValue(), 128);
		QVERIFY(!original->isMuted());
		QVERIFY(!unrelated->isMuted());
		QVERIFY(!original->isSolo());
		QVERIFY(original->isMutedBeforeSolo());
		QVERIFY(!song->isModified());
		QCOMPARE(lmms::Engine::projectJournal()->undoDepth(), 0);
		QCOMPARE(bus.batchDepth(), 0);
		lmms::DataFile after(lmms::DataFile::Type::SongProject);
		song->saveProjectState(after);
		QCOMPARE(after.content().ownerDocument().toString(), before.content().ownerDocument().toString());
	}

	void incrementalNoteValuesKeepIdentity_data()
	{
		QTest::addColumn<QString>("command");
		QTest::addColumn<QJsonObject>("arguments");
		QTest::addColumn<QString>("mode");
		const auto add = [](const char* command, QJsonObject arguments) {
			arguments.insert("track", 0);
			arguments.insert("clip", 0);
			for (const auto* mode : {"failure", "preview", "history"})
			{
				QTest::newRow(qPrintable(QString("%1-%2").arg(command, mode)))
					<< QString(command) << arguments << QString(mode);
			}
		};
		add("midi.updateNote", {{"note", 0},
			{"fields", QJsonObject{{"position", 250}, {"length", 200}, {"key", 75}, {"volume", 65}, {"panning", -20}}}});
		add("midi.transpose", {{"semitones", 7}});
		add("midi.quantize", {{"grid", 16}});
		add("midi.setSteps", {{"steps", 48}});
		add("midi.setClipType", {{"type", "beat"}});
		add("midi.addNotes", {{"notes", QJsonArray{QJsonObject{{"position", 300}, {"length", 48}, {"key", 70}}}}});
		add("midi.removeNotes", {});
		add("midi.clearNotes", {});
		add("midi.humanize", {{"timing", 6}, {"velocity", 8}, {"detune", 0.5}, {"seed", 17}});
		// The fixture sets slot 1 to C# major, so key 60 (C) snaps and 64 (E) moves too.
		add("scale.snapNotes", {{"index", 1}});
		add("edit.humanize", {{"timing", 6}, {"velocity", 8}, {"seed", 17}});
		add("edit.transpose", {{"semitones", 5}});
	}

	void incrementalNoteMembershipKeepsLaterHistory()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		auto* journal = lmms::Engine::projectJournal();
		QVERIFY(bus.execute("track.create", {{"type", "instrument"}}).ok);
		QVERIFY(bus.execute("clip.create", {{"track", 0}, {"position", 0}, {"length", 192}}).ok);
		auto* clip = qobject_cast<lmms::MidiClip*>(song->tracks()[0]->getClips()[0]);
		QVERIFY(clip);
		journal->clearJournal();
		const QJsonObject address{{"track", 0}, {"clip", 0}};
		auto added = address;
		added.insert("notes", QJsonArray{QJsonObject{{"position", 10}, {"length", 48}, {"key", 60}}});
		QVERIFY(bus.executeForAgent("midi.addNotes", added).ok);
		QCOMPARE(clip->notes().size(), std::size_t(1));
		auto* note = clip->notes()[0];
		auto edited = address;
		edited.insert("note", 0);
		edited.insert("fields", QJsonObject{{"key", 67}});
		QVERIFY(bus.executeForAgent("midi.updateNote", edited).ok);
		QCOMPARE(journal->undoDepth(), 2);

		QVERIFY(bus.executeForAgent("history.undo").ok);
		QCOMPARE(note->key(), 60);
		QVERIFY(bus.executeForAgent("history.undo").ok);
		QVERIFY(clip->notes().empty());
		QVERIFY(bus.executeForAgent("history.redo").ok);
		QCOMPARE(clip->notes().size(), std::size_t(1));
		QCOMPARE(clip->notes()[0], note);
		const auto redone = bus.executeForAgent("history.redo");
		QVERIFY2(redone.ok, qPrintable(redone.errorCode + ": " + redone.errorMessage));
		QCOMPARE(note->key(), 67);

		// Removal in a failed script must re-attach the same object.
		auto removed = address;
		const QJsonArray steps{QJsonObject{{"cmd", "midi.clearNotes"}, {"args", removed}},
			QJsonObject{{"assert", QJsonObject{{"expr", false}}}}};
		const auto failed = bus.executeForAgent("agent.runScript", {{"script", QJsonObject{{"steps", steps}}}});
		QCOMPARE(failed.errorCode, QStringLiteral("script_assertion_failed"));
		QCOMPARE(clip->notes().size(), std::size_t(1));
		QCOMPARE(clip->notes()[0], note);
		QCOMPARE(journal->undoDepth(), 2);
	}

	void incrementalNoteValuesKeepIdentity()
	{
		QFETCH(QString, command);
		QFETCH(QJsonObject, arguments);
		QFETCH(QString, mode);
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		auto* journal = lmms::Engine::projectJournal();
		QVERIFY(bus.execute("track.create", {{"type", "instrument"}}).ok);
		QVERIFY(bus.execute("track.create", {{"type", "sample"}, {"name", "Unrelated"}}).ok);
		QVERIFY(bus.execute("clip.create", {{"track", 0}, {"position", 0}, {"length", 192}}).ok);
		auto* clip = qobject_cast<lmms::MidiClip*>(song->tracks()[0]->getClips()[0]);
		QVERIFY(clip);
		QVERIFY(bus.execute("scale.set", {{"index", 1}, {"root", 1}, {"type", "major"}}).ok);
		auto* first = clip->addNote(lmms::Note(lmms::TimePos(48), lmms::TimePos(5), 60), false);
		auto* second = clip->addNote(lmms::Note(lmms::TimePos(24), lmms::TimePos(100), 64), false);
		clip->setAutoResize(true);
		clip->setStartTimeOffset(lmms::TimePos(7));
		QPointer<lmms::Track> unrelated(song->tracks()[1]);
		QPointer<lmms::MidiClip> original(clip);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
		lmms::DataFile before(lmms::DataFile::Type::SongProject);
		song->saveProjectState(before);
		journal->clearJournal();
		song->setModified(false);
		QSignalSpy reset(song, &lmms::Song::projectAboutToReset);
		if (mode == "failure")
		{
			const QJsonArray steps{QJsonObject{{"cmd", command}, {"args", arguments}},
				QJsonObject{{"assert", QJsonObject{{"expr", false}}}}};
			const auto result = bus.executeForAgent("agent.runScript", {{"script", QJsonObject{{"steps", steps}}}});
			QCOMPARE(result.errorCode, QStringLiteral("script_assertion_failed"));
		}
		else
		{
			arguments.insert("dryRun", mode == "preview");
			const auto result = bus.executeForAgent(command, arguments);
			QVERIFY2(result.ok, qPrintable(result.errorCode + ": " + result.errorMessage));
			if (mode == "history")
			{
				QCOMPARE(journal->undoDepth(), 1);
				const auto localState = [clip] {
					QDomDocument document;
					auto root = document.createElement("midiclip");
					document.appendChild(root);
					clip->saveSettings(document, root);
					return document.toString();
				};
				const auto edited = localState();
				QVERIFY(bus.executeForAgent("history.undo").ok);
				QVERIFY(bus.executeForAgent("history.redo").ok);
				QCOMPARE(localState(), edited);
				QVERIFY(bus.executeForAgent("history.undo").ok);
			}
		}
		QCOMPARE(reset.count(), 0);
		QVERIFY(original && unrelated);
		QCOMPARE(song->tracks()[1], unrelated.data());
		QCOMPARE(clip->notes().size(), std::size_t(2));
		QCOMPARE(clip->notes()[0], first);
		QCOMPARE(clip->notes()[1], second);
		QCOMPARE(first->pos().getTicks(), 5);
		QCOMPARE(first->key(), 60);
		QCOMPARE(second->pos().getTicks(), 100);
		QCOMPARE(second->key(), 64);
		QCOMPARE(clip->startTimeOffset().getTicks(), 7);
		QVERIFY(!song->isModified());
		QCOMPARE(journal->undoDepth(), 0);
		QCOMPARE(bus.batchDepth(), 0);
		lmms::DataFile after(lmms::DataFile::Type::SongProject);
		song->saveProjectState(after);
		QCOMPARE(after.content().ownerDocument().toString(), before.content().ownerDocument().toString());
	}

	void incrementalLocalStateRestores_data()
	{
		QTest::addColumn<QString>("command");
		QTest::addColumn<QJsonObject>("arguments");
		QTest::addColumn<QString>("mode");
		const auto add = [](const char* command, const QJsonObject& arguments) {
			for (const auto* mode : {"failure", "preview", "history"})
			{
				QTest::newRow(qPrintable(QString("%1-%2").arg(command, mode)))
					<< QString(command) << arguments << QString(mode);
			}
		};
		add("mixer.setName", {{"channel", 1}, {"name", "Changed"}});
		add("mixer.setColor", {{"channel", 1}, {"value", "#123456"}});
		add("mixer.setVolume", {{"channel", 1}, {"value", 0.5}});
		add("mixer.setMute", {{"channel", 1}, {"value", true}});
		// The fixture already solos channel 2; clearing it runs the restore half
		// of the solo transition and rewrites every channel's mute.
		add("mixer.setSolo", {{"channel", 2}, {"value", false}});
		add("mixer.setSendAmount", {{"from", 1}, {"to", 2}, {"amount", 0.25}});
		add("instrument.setArpeggio", {{"track", 0}, {"enabled", true}});
		add("instrument.setNoteStacking", {{"track", 0}, {"enabled", true}});
		add("instrument.setMidiIn", {{"track", 0}, {"channel", 3}, {"enabled", true}});
		add("instrument.setMidiOut", {{"track", 0}, {"channel", 4}, {"enabled", true}});
		add("sample.setReversed", {{"track", 1}, {"clip", 0}, {"value", true}});
		add("sample.setOffset", {{"track", 1}, {"clip", 0}, {"value", 24}});
		add("automation.setProgression", {{"track", 2}, {"clip", 0}, {"type", "linear"}});
		add("automation.setTension", {{"track", 2}, {"clip", 0}, {"value", 0.25}});
		add("pattern.rename", {{"pattern", 0}, {"name", "Changed"}});
		add("pattern.setLength", {{"pattern", 0}, {"bars", 3}});
		add("controller.setParam", {{"controller", 0}, {"name", "speed"}, {"value", 0.5}});
		add("song.setTimeSignature", {{"numerator", 3}, {"denominator", 4}});
		add("transport.setLoopRange", {{"startBar", 1}, {"endBar", 3}});
		add("scale.set", {{"index", 0}, {"root", 2}, {"type", "minor"}});
		add("automation.putValue", {{"track", 2}, {"clip", 0}, {"pos", 96}, {"value", 100}});
		add("automation.putValues", {{"track", 2}, {"clip", 0},
			{"nodes", QJsonArray{QJsonObject{{"pos", 48}, {"inValue", 80}}, QJsonObject{{"pos", 150}, {"inValue", 120}}}}});
		add("automation.removeNode", {{"track", 2}, {"clip", 0}, {"pos", 0}});
		add("automation.removeNodes", {{"track", 2}, {"clip", 0}, {"start", 0}, {"end", 200}});
		add("automation.addClip", {{"track", 2}, {"position", 384}, {"length", 192}});
		add("automation.createTrack", {{"name", "More"}});
		add("mixer.addSend", {{"from", 2}, {"to", 0}, {"amount", 0.75}});
		add("mixer.removeSend", {{"from", 1}, {"to", 2}});
		add("pattern.placeInSong", {{"pattern", 0}, {"position", 576}});
		add("pattern.removeFromSong", {{"track", 3}, {"clip", 0}});
		add("sample.setFile", {{"track", 1}, {"clip", 0},
			{"path", QFINDTESTDATA("../../../data/samples/drums/kick01.ogg")}});
	}

	void incrementalLocalStateRestores()
	{
		QFETCH(QString, command);
		QFETCH(QJsonObject, arguments);
		QFETCH(QString, mode);

		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		auto* journal = lmms::Engine::projectJournal();
		auto* mixer = lmms::Engine::mixer();
		QVERIFY(bus.execute("track.create", {{"type", "instrument"}, {"name", "Lead"}}).ok);
		QVERIFY(bus.execute("track.create", {{"type", "sample"}, {"name", "Audio"}}).ok);
		QVERIFY(bus.execute("track.create", {{"type", "automation"}, {"name", "Curve"}}).ok);
		QVERIFY(bus.execute("clip.create", {{"track", 0}, {"position", 0}, {"length", 192}}).ok);
		QVERIFY(bus.execute("clip.create", {{"track", 1}, {"position", 0}, {"length", 192}}).ok);
		QVERIFY(bus.execute("automation.addClip", {{"track", 2}, {"position", 0}, {"length", 192}}).ok);
		const auto target = bus.execute("automation.addTarget",
			{{"track", 2}, {"clip", 0}, {"target", "song/masterVolume"},
				{"nodes", QJsonArray{QJsonObject{{"pos", 0}, {"inValue", 50}}, QJsonObject{{"pos", 96}, {"inValue", 150}}}}});
		QVERIFY2(target.ok, qPrintable(target.errorCode + ": " + target.errorMessage));
		QVERIFY(bus.execute("pattern.create", {{"name", "Beat"}}).ok);
		QVERIFY(bus.execute("pattern.placeInSong", {{"pattern", 0}, {"position", 192}}).ok);
		QVERIFY(bus.execute("controller.add", {{"type", "lfo"}}).ok);
		QVERIFY(bus.execute("mixer.addChannel", {{"name", "First"}}).ok);
		QVERIFY(bus.execute("mixer.addChannel", {{"name", "Second"}}).ok);
		QVERIFY(bus.execute("mixer.addSend", {{"from", 1}, {"to", 2}, {"amount", 0.5}}).ok);
		// A pre-existing solo makes the forward transition rewrite every
		// channel's saved mute flag, which the rollback must restore exactly.
		mixer->mixerChannel(2)->m_muteModel.setValue(true);
		// Set the model like the mixer view does; its connection runs the solo
		// transition once.
		mixer->mixerChannel(2)->m_soloModel.setValue(true);
		QVERIFY(mixer->mixerChannel(2)->m_soloModel.value());
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
		QTest::qWait(10);

		QList<QPointer<lmms::Track>> tracks;
		for (auto* track : song->tracks())
		{
			tracks.append(track);
		}
		QPointer<lmms::Clip> sampleClip(song->tracks()[1]->getClips()[0]);
		QPointer<lmms::Clip> automationClip(song->tracks()[2]->getClips()[0]);
		QPointer<lmms::Controller> controller(song->controllers()[0]);
		QList<lmms::MixerChannel*> channels;
		for (int index = 0; index < static_cast<int>(mixer->numChannels()); ++index)
		{
			channels.append(mixer->mixerChannel(index));
		}
		QList<bool> muteBeforeSolo;
		for (auto* channel : channels)
		{
			muteBeforeSolo.append(channel->m_muteBeforeSolo);
		}

		const auto localState = [] {
			return localProjectState();
		};
		lmms::DataFile before(lmms::DataFile::Type::SongProject);
		song->saveProjectState(before);
		const auto localBefore = localState();
		journal->clearJournal();
		song->setModified(false);
		QSignalSpy reset(song, &lmms::Song::projectAboutToReset);
		if (mode == "failure")
		{
			const QJsonArray steps{QJsonObject{{"cmd", command}, {"args", arguments}},
				QJsonObject{{"assert", QJsonObject{{"expr", false}}}}};
			const auto result = bus.executeForAgent("agent.runScript", {{"script", QJsonObject{{"steps", steps}}}});
			QVERIFY2(result.errorCode == QStringLiteral("script_assertion_failed"),
				qPrintable(result.errorCode + ": " + result.errorMessage));
		}
		else
		{
			arguments.insert("dryRun", mode == "preview");
			const auto result = bus.executeForAgent(command, arguments);
			QVERIFY2(result.ok, qPrintable(result.errorCode + ": " + result.errorMessage));
			if (mode == "history")
			{
				QCOMPARE(journal->undoDepth(), 1);
				const auto edited = localState();
				QVERIFY(edited != localBefore);
				QVERIFY(bus.executeForAgent("history.undo").ok);
				QVERIFY2(localState() == localBefore, qPrintable(firstDifference(localState(), localBefore)));
				QVERIFY(bus.executeForAgent("history.redo").ok);
				QVERIFY2(localState() == edited, qPrintable(firstDifference(localState(), edited)));
				QVERIFY(bus.executeForAgent("history.undo").ok);
			}
		}
		QCOMPARE(reset.count(), 0);
		QCOMPARE(song->tracks().size(), static_cast<std::size_t>(tracks.size()));
		for (int index = 0; index < tracks.size(); ++index)
		{
			QVERIFY(tracks[index]);
			QCOMPARE(song->tracks()[index], tracks[index].data());
		}
		QVERIFY(sampleClip && automationClip && controller);
		QCOMPARE(song->tracks()[1]->getClips()[0], sampleClip.data());
		QCOMPARE(song->tracks()[2]->getClips()[0], automationClip.data());
		QCOMPARE(song->controllers()[0], controller.data());
		QCOMPARE(static_cast<int>(mixer->numChannels()), channels.size());
		for (int index = 0; index < channels.size(); ++index)
		{
			QCOMPARE(mixer->mixerChannel(index), channels[index]);
			QCOMPARE(channels[index]->m_muteBeforeSolo, muteBeforeSolo[index]);
		}
		QVERIFY2(localState() == localBefore, qPrintable(firstDifference(localState(), localBefore)));
		QVERIFY(!song->isModified());
		QCOMPARE(journal->undoDepth(), 0);
		QCOMPARE(bus.batchDepth(), 0);
		lmms::DataFile after(lmms::DataFile::Type::SongProject);
		song->saveProjectState(after);
		QCOMPARE(after.content().ownerDocument().toString(), before.content().ownerDocument().toString());

		// The restored private solo bookkeeping must still drive the next
		// forward transition: clearing the earlier solo restores saved mutes.
		QVERIFY(bus.execute("mixer.setSolo", {{"channel", 2}, {"value", false}}).ok);
		QVERIFY(!channels[1]->m_muteModel.value());
		QVERIFY(channels[2]->m_muteModel.value());
	}

	void incrementalTrackMembership_data()
	{
		QTest::addColumn<QString>("command");
		QTest::addColumn<QJsonObject>("arguments");
		QTest::addColumn<QString>("mode");
		const auto add = [](const char* command, const QJsonObject& arguments) {
			for (const auto* mode : {"failure", "preview", "history"})
			{
				QTest::newRow(qPrintable(QString("%1-%2").arg(command, mode)))
					<< QString(command) << arguments << QString(mode);
			}
		};
		add("track.create", {{"type", "instrument"}, {"name", "Added"}, {"index", 1}});
		add("track.create-automation", {{"type", "automation"}, {"name", "Curve2"}});
		add("track.remove", {{"track", 1}});
		add("track.delete", {{"track", 0}});
		add("track.clone", {{"track", 0}, {"name", "Copy"}});
		add("track.move", {{"track", 0}, {"newIndex", 2}});
		add("clip.create", {{"track", 0}, {"position", 384}, {"length", 192}, {"name", "Added"}});
		add("clip.create-sample", {{"track", 1}, {"position", 96}, {"length", 192}});
		add("clip.create-automation", {{"track", 2}, {"position", 0}, {"length", 192}});
		add("clip.remove", {{"track", 0}, {"clip", 0}});
		add("clip.delete", {{"track", 0}, {"clip", 0}});
		add("clip.duplicate", {{"track", 0}, {"clip", 0}, {"position", 384}});
	}

	void incrementalTrackMembership()
	{
		QFETCH(QString, command);
		QFETCH(QJsonObject, arguments);
		QFETCH(QString, mode);
		// Row labels may carry a variant suffix such as "clip.create-sample".
		command = command.section('-', 0, 0);
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		auto* journal = lmms::Engine::projectJournal();
		QVERIFY(bus.execute("track.create", {{"type", "instrument"}, {"name", "Lead"}}).ok);
		QVERIFY(bus.execute("track.create", {{"type", "sample"}, {"name", "Audio"}}).ok);
		QVERIFY(bus.execute("track.create", {{"type", "automation"}, {"name", "Curve"}}).ok);
		QVERIFY(bus.execute("clip.create", {{"track", 0}, {"position", 0}, {"length", 192}}).ok);
		auto* midiClip = qobject_cast<lmms::MidiClip*>(song->tracks()[0]->getClips()[0]);
		QVERIFY(midiClip);
		auto* note = midiClip->addNote(lmms::Note(lmms::TimePos(48), lmms::TimePos(5), 60), false);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
		QTest::qWait(10);
		QList<QPointer<lmms::Track>> tracks;
		for (auto* track : song->tracks())
		{
			tracks.append(track);
		}
		QPointer<lmms::Clip> clip(midiClip);
		const auto localBefore = localProjectState();
		lmms::DataFile before(lmms::DataFile::Type::SongProject);
		song->saveProjectState(before);
		journal->clearJournal();
		song->setModified(false);
		QSignalSpy reset(song, &lmms::Song::projectAboutToReset);
		if (mode == "failure")
		{
			const QJsonArray steps{QJsonObject{{"cmd", command}, {"args", arguments}},
				QJsonObject{{"assert", QJsonObject{{"expr", false}}}}};
			const auto result = bus.executeForAgent("agent.runScript", {{"script", QJsonObject{{"steps", steps}}}});
			QVERIFY2(result.errorCode == QStringLiteral("script_assertion_failed"),
				qPrintable(result.errorCode + ": " + result.errorMessage));
		}
		else
		{
			auto forward = arguments;
			forward.insert("dryRun", mode == "preview");
			const auto result = bus.executeForAgent(command, forward);
			QVERIFY2(result.ok, qPrintable(result.errorCode + ": " + result.errorMessage));
			if (mode == "history")
			{
				QCOMPARE(journal->undoDepth(), 1);
				const auto edited = localProjectState();
				QVERIFY(edited != localBefore);
				QVERIFY(bus.executeForAgent("history.undo").ok);
				QVERIFY2(localProjectState() == localBefore,
					qPrintable(firstDifference(localProjectState(), localBefore)));
				QVERIFY(bus.executeForAgent("history.redo").ok);
				QVERIFY2(localProjectState() == edited, qPrintable(firstDifference(localProjectState(), edited)));
				QVERIFY(bus.executeForAgent("history.undo").ok);
			}
		}
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QCOMPARE(reset.count(), 0);
		QCOMPARE(song->tracks().size(), static_cast<std::size_t>(tracks.size()));
		for (int index = 0; index < tracks.size(); ++index)
		{
			QVERIFY(tracks[index]);
			QCOMPARE(song->tracks()[index], tracks[index].data());
		}
		QVERIFY(clip);
		QCOMPARE(song->tracks()[0]->getClips()[0], clip.data());
		QCOMPARE(midiClip->notes()[0], note);
		const auto& views = lmms::gui::getGUI()->songEditor()->m_editor->trackViews();
		QCOMPARE(views.size(), tracks.size());
		for (int index = 0; index < tracks.size(); ++index)
		{
			QCOMPARE(views[index]->getTrack(), tracks[index].data());
			int openClipViews = 0;
			for (const auto* clipView : views[index]->findChildren<lmms::gui::ClipView*>())
			{
				openClipViews += clipView->isHidden() ? 0 : 1;
			}
			QCOMPARE(openClipViews, static_cast<int>(tracks[index]->getClips().size()));
		}
		QVERIFY(!song->isModified());
		QCOMPARE(journal->undoDepth(), 0);
		QCOMPARE(bus.batchDepth(), 0);
		QVERIFY2(localProjectState() == localBefore, qPrintable(firstDifference(localProjectState(), localBefore)));
		lmms::DataFile after(lmms::DataFile::Type::SongProject);
		song->saveProjectState(after);
		QCOMPARE(after.content().ownerDocument().toString(), before.content().ownerDocument().toString());
	}

	void incrementalSvsClipState_data()
	{
		QTest::addColumn<QString>("command");
		QTest::addColumn<QJsonObject>("arguments");
		QTest::addColumn<QString>("mode");
		const auto add = [](const char* command, QJsonObject arguments) {
			// Creation commands take only what they create into.
			if (QString(command) != "svs.createTrack")
			{
				arguments.insert("track", 0);
			}
			if (!QString(command).startsWith("svs.create"))
			{
				arguments.insert("clip", 0);
			}
			for (const auto* mode : {"failure", "preview", "history"})
			{
				QTest::newRow(qPrintable(QString("%1-%2").arg(command, mode)))
					<< QString(command) << arguments << QString(mode);
			}
		};
		add("svs.addNotes", {{"notes", QJsonArray{QJsonObject{
			{"id", "c"}, {"tick", 384}, {"duration", 48}, {"pitch", 67}, {"lyric", "lo"}}}}});
		add("svs.updateNote", {{"id", "a"}, {"fields", QJsonObject{{"pitch", 62}}}});
		add("svs.removeNotes", {{"ids", QJsonArray{"a"}}});
		add("svs.createClip", {{"position", 384}, {"length", 192}});
		add("svs.createTrack", {{"name", "Second voice"}});
	}

	void incrementalSvsClipState()
	{
		QFETCH(QString, command);
		QFETCH(QJsonObject, arguments);
		QFETCH(QString, mode);
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		auto* journal = lmms::Engine::projectJournal();
		QVERIFY(bus.execute("svs.createTrack", {{"name", "Voice"}}).ok);
		QVERIFY(bus.execute("svs.createClip", {{"track", 0}, {"position", 0}, {"length", 768}}).ok);
		const QJsonArray notes{
			QJsonObject{{"id", "a"}, {"tick", 0}, {"duration", 48}, {"pitch", 60}, {"lyric", "la"}},
			QJsonObject{{"id", "b"}, {"tick", 192}, {"duration", 48}, {"pitch", 64}, {"lyric", "la"}}};
		QVERIFY(bus.execute("svs.addNotes", {{"track", 0}, {"clip", 0}, {"notes", notes}}).ok);
		QPointer<lmms::Clip> clip(song->tracks()[0]->getClips()[0]);
		auto* svsClip = qobject_cast<lmms::SVSClip*>(clip.data());
		QVERIFY(svsClip);
		const auto notesBefore = svsClip->notes();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
		const auto localBefore = localProjectState();
		journal->clearJournal();
		song->setModified(false);
		QSignalSpy reset(song, &lmms::Song::projectAboutToReset);
		if (mode == "failure")
		{
			const QJsonArray steps{QJsonObject{{"cmd", command}, {"args", arguments}},
				QJsonObject{{"assert", QJsonObject{{"expr", false}}}}};
			const auto result = bus.executeForAgent("agent.runScript", {{"script", QJsonObject{{"steps", steps}}}});
			QVERIFY2(result.errorCode == QStringLiteral("script_assertion_failed"),
				qPrintable(result.errorCode + ": " + result.errorMessage));
		}
		else
		{
			arguments.insert("dryRun", mode == "preview");
			const auto result = bus.executeForAgent(command, arguments);
			QVERIFY2(result.ok, qPrintable(result.errorCode + ": " + result.errorMessage));
			if (mode == "history")
			{
				QCOMPARE(journal->undoDepth(), 1);
				const auto edited = localProjectState();
				QVERIFY(edited != localBefore);
				QVERIFY(bus.executeForAgent("history.undo").ok);
				QVERIFY2(localProjectState() == localBefore,
					qPrintable(firstDifference(localProjectState(), localBefore)));
				QVERIFY(bus.executeForAgent("history.redo").ok);
				QVERIFY2(localProjectState() == edited, qPrintable(firstDifference(localProjectState(), edited)));
				QVERIFY(bus.executeForAgent("history.undo").ok);
			}
		}
		QCOMPARE(reset.count(), 0);
		QVERIFY(clip);
		QCOMPARE(song->tracks()[0]->getClips()[0], clip.data());
		QCOMPARE(svsClip->notes(), notesBefore);
		QVERIFY(!song->isModified());
		QCOMPARE(journal->undoDepth(), 0);
		QCOMPARE(bus.batchDepth(), 0);
		QVERIFY2(localProjectState() == localBefore, qPrintable(firstDifference(localProjectState(), localBefore)));
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
	// Song-level state without GUI editor serializers. Serializing still marks
	// the song modified and adds checkpoints, so the snapshot suspends
	// journalling and restores the dirty flag it observed.
	static QString localProjectState()
	{
		auto* song = lmms::Engine::getSong();
		auto* journal = lmms::Engine::projectJournal();
		const bool wasJournalling = journal->isJournalling();
		const bool wasModified = song->isModified();
		journal->setJournalling(false);
		const auto restoreFlags = qScopeGuard([&] {
			journal->setJournalling(wasJournalling);
			song->setModified(wasModified);
		});
		lmms::DataFile file(lmms::DataFile::Type::SongProject);
		song->tempoModel().saveSettings(file, file.head(), "bpm");
		song->getTimeSigModel().saveSettings(file, file.head(), "timesig");
		song->saveState(file, file.content());
		lmms::Engine::mixer()->saveState(file, file.content());
		song->getTimeline(lmms::Song::PlayMode::Song).saveState(file, file.content());
		for (auto* item : song->controllers())
		{
			item->saveState(file, file.content());
		}
		for (unsigned int index = 0; index < lmms::MaxScaleCount; ++index)
		{
			// Serialization does not modify the definitions; the API is non-const.
			const_cast<lmms::Scale*>(song->getScale(index).get())->saveState(file, file.content());
			const_cast<lmms::Keymap*>(song->getKeymap(index).get())->saveState(file, file.content());
		}
		return file.toString();
	}

	static QString firstDifference(const QString& actual, const QString& expected)
	{
		const auto actualLines = actual.split('\n');
		const auto expectedLines = expected.split('\n');
		const auto count = std::max(actualLines.size(), expectedLines.size());
		for (qsizetype line = 0; line < count; ++line)
		{
			const auto left = line < actualLines.size() ? actualLines[line] : QString();
			const auto right = line < expectedLines.size() ? expectedLines[line] : QString();
			if (left != right)
			{
				return QStringLiteral("line %1\n actual:   %2\n expected: %3").arg(line + 1).arg(left, right);
			}
		}
		return QStringLiteral("no differing line");
	}

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
