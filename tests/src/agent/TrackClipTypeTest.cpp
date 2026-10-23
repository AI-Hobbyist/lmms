#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "agent/CommandBus.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::agent;

class TrackClipTypeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }

	void cleanupTestCase() { Engine::destroy(); }

	void init()
	{
		Engine::getSong()->clearProject();
		Engine::projectJournal()->clearJournal();
	}

	void svsTrackAndClipReportTheirType()
	{
		const auto track = CommandBus::instance().execute("svs.createTrack", {{"name", "Vocal"}});
		QVERIFY2(track.ok, qPrintable(track.errorMessage));
		const int trackIndex = track.data.value("index").toInt();

		const auto trackDetail = CommandBus::instance().execute("track.get", {{"track", trackIndex}});
		QVERIFY2(trackDetail.ok, qPrintable(trackDetail.errorMessage));
		QCOMPARE(trackDetail.data.value("type").toString(), QStringLiteral("svs"));

		const auto clip = CommandBus::instance().execute(
			"svs.createClip", {{"track", trackIndex}, {"position", 0}, {"length", 192}});
		QVERIFY2(clip.ok, qPrintable(clip.errorMessage));
		const int clipIndex = clip.data.value("index").toInt();

		const auto clipDetail = CommandBus::instance().execute(
			"clip.get", {{"track", trackIndex}, {"clip", clipIndex}});
		QVERIFY2(clipDetail.ok, qPrintable(clipDetail.errorMessage));
		QCOMPARE(clipDetail.data.value("type").toString(), QStringLiteral("svs"));
	}

	void svcTrackAndClipReportTheirType()
	{
		const auto track = CommandBus::instance().execute("svc.createTrack", {{"name", "Conversion"}});
		QVERIFY2(track.ok, qPrintable(track.errorMessage));
		const int trackIndex = track.data.value("index").toInt();

		const auto trackDetail = CommandBus::instance().execute("track.get", {{"track", trackIndex}});
		QVERIFY2(trackDetail.ok, qPrintable(trackDetail.errorMessage));
		QCOMPARE(trackDetail.data.value("type").toString(), QStringLiteral("svc"));

		const auto clip = CommandBus::instance().execute(
			"svc.createClip", {{"track", trackIndex}, {"position", 0}, {"length", 192}});
		QVERIFY2(clip.ok, qPrintable(clip.errorMessage));
		const int clipIndex = clip.data.value("index").toInt();

		const auto clipDetail = CommandBus::instance().execute(
			"clip.get", {{"track", trackIndex}, {"clip", clipIndex}});
		QVERIFY2(clipDetail.ok, qPrintable(clipDetail.errorMessage));
		QCOMPARE(clipDetail.data.value("type").toString(), QStringLiteral("svc"));
	}

	void trackListUsesTheSameNames()
	{
		QVERIFY(CommandBus::instance().execute("svs.createTrack", {{"name", "Vocal"}}).ok);
		QVERIFY(CommandBus::instance().execute("svc.createTrack", {{"name", "Conversion"}}).ok);

		const auto list = CommandBus::instance().execute("track.list", {});
		QVERIFY2(list.ok, qPrintable(list.errorMessage));

		QStringList types;
		for (const auto& value : list.data.value("tracks").toArray())
		{
			types.append(value.toObject().value("type").toString());
		}
		QVERIFY(types.contains(QStringLiteral("svs")));
		QVERIFY(types.contains(QStringLiteral("svc")));
		QVERIFY(!types.contains(QStringLiteral("unknown")));
	}
};

QTEST_GUILESS_MAIN(TrackClipTypeTest)
#include "TrackClipTypeTest.moc"
