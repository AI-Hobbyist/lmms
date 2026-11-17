#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWidget>
#include <QtTest>

#include "DataFile.h"
#include "Engine.h"
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
		lmms::Engine::init(true);
		m_window.setWindowTitle(QStringLiteral("LMMS project lifetime validation"));
		m_window.resize(480, 160);
		m_window.show();
		QVERIFY(QTest::qWaitForWindowExposed(&m_window));
	}

	void cleanupTestCase()
	{
		m_window.close();
		lmms::Engine::destroy();
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
	QWidget m_window;
};

QTEST_MAIN(ChatProjectLifetimeTest)
#include "ChatProjectLifetimeTest.moc"
