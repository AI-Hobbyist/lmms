#include <QDir>
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
