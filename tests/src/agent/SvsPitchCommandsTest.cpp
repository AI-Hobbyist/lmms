#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "agent/CommandBus.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::agent;

class SvsPitchCommandsTest : public QObject
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

	void writesAbsolutePitchCurve()
	{
		const int track = createClipWithNotes();
		QVERIFY(track >= 0);

		const auto result = CommandBus::instance().execute("svs.setPitchCurve",
			{{"track", track}, {"clip", 0}, {"relative", false},
				{"points", QJsonArray{QJsonObject{{"tick", 0}, {"value", 60.0}},
							 QJsonObject{{"tick", 96}, {"value", 62.5}},
							 QJsonObject{{"tick", 288}, {"value", 64.0}}}}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(result.data.value("id").toString(), QStringLiteral("svs.pitch"));
		QCOMPARE(result.data.value("pointCount").toInt(), 3);

		const auto points = curvePoints(track);
		QCOMPARE(points.size(), 3);
		QCOMPARE(points.at(1).toObject().value("value").toDouble(), 62.5);
	}

	void resolvesRelativeOffsetsAgainstNotes()
	{
		const int track = createClipWithNotes();
		QVERIFY(track >= 0);

		const auto result = CommandBus::instance().execute("svs.setPitchCurve",
			{{"track", track}, {"clip", 0},
				{"points", QJsonArray{QJsonObject{{"tick", 0}, {"value", 0.0}},
							 QJsonObject{{"tick", 96}, {"value", 2.0}},
							 QJsonObject{{"tick", 200}, {"value", -1.0}}}}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(result.data.value("relative").toBool(), true);

		const auto points = curvePoints(track);
		QCOMPARE(points.size(), 3);
		QCOMPARE(points.at(0).toObject().value("value").toDouble(), 60.0);
		QCOMPARE(points.at(1).toObject().value("value").toDouble(), 62.0);
		QCOMPARE(points.at(2).toObject().value("value").toDouble(), 63.0);
	}

	void rejectsUnsortedPoints()
	{
		const int track = createClipWithNotes();
		QVERIFY(track >= 0);

		const auto result = CommandBus::instance().execute("svs.setPitchCurve",
			{{"track", track}, {"clip", 0},
				{"points", QJsonArray{QJsonObject{{"tick", 96}, {"value", 1.0}},
							 QJsonObject{{"tick", 0}, {"value", 0.0}}}}});
		QVERIFY(!result.ok);
		QCOMPARE(result.errorCode, QStringLiteral("invalid_arguments"));
	}

	void rejectsPitchOutsideKeyRange()
	{
		const int track = createClipWithNotes();
		QVERIFY(track >= 0);

		const auto result = CommandBus::instance().execute("svs.setPitchCurve",
			{{"track", track}, {"clip", 0},
				{"points", QJsonArray{QJsonObject{{"tick", 0}, {"value", 200.0}}}}});
		QVERIFY(!result.ok);
		QCOMPARE(result.errorCode, QStringLiteral("invalid_arguments"));
	}

	void clearsPitchCurve()
	{
		const int track = createClipWithNotes();
		QVERIFY(track >= 0);
		QVERIFY(CommandBus::instance()
					.execute("svs.setPitchCurve",
						{{"track", track}, {"clip", 0},
							{"points", QJsonArray{QJsonObject{{"tick", 0}, {"value", 61.0}}}}})
					.ok);

		const auto cleared = CommandBus::instance().execute(
			"svs.setPitchCurve", {{"track", track}, {"clip", 0}, {"clear", true}});
		QVERIFY2(cleared.ok, qPrintable(cleared.errorMessage));
		QVERIFY(curvePoints(track).isEmpty());
	}

	void producesSynthesizableCurveShape()
	{
		const int track = createClipWithNotes();
		QVERIFY(track >= 0);

		const auto result = CommandBus::instance().execute("svs.setPitchCurve",
			{{"track", track}, {"clip", 0}, {"interpolation", "linear"},
				{"points", QJsonArray{QJsonObject{{"tick", 0}, {"value", 0.0}},
							 QJsonObject{{"tick", 192}, {"value", 1.0}}}}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));

		const auto curves = CommandBus::instance().execute("svs.getCurves", {{"track", track}, {"clip", 0}});
		QVERIFY2(curves.ok, qPrintable(curves.errorMessage));
		const auto curve = curves.data.value("curves").toObject().value("svs.pitch").toObject();
		QCOMPARE(curve.value("unit").toString(), QStringLiteral("semitone"));
		QCOMPARE(curve.value("scope").toString(), QStringLiteral("clip"));
		QCOMPARE(curve.value("interpolation").toString(), QStringLiteral("linear"));
	}

private:
	int createClipWithNotes()
	{
		const auto track = CommandBus::instance().execute("svs.createTrack", {{"name", "Vocal"}});
		if (!track.ok)
		{
			return -1;
		}
		const int trackIndex = track.data.value("index").toInt();

		const auto clip = CommandBus::instance().execute(
			"svs.createClip", {{"track", trackIndex}, {"position", 0}, {"length", 384}});
		if (!clip.ok)
		{
			return -1;
		}

		const auto notes = CommandBus::instance().execute("svs.addNotes",
			{{"track", trackIndex}, {"clip", 0},
				{"notes", QJsonArray{QJsonObject{{"tick", 0}, {"duration", 192}, {"pitch", 60}},
							 QJsonObject{{"tick", 192}, {"duration", 192}, {"pitch", 64}}}}});
		if (!notes.ok)
		{
			return -1;
		}
		return trackIndex;
	}

	QJsonArray curvePoints(int track)
	{
		const auto curves = CommandBus::instance().execute("svs.getCurves", {{"track", track}, {"clip", 0}});
		if (!curves.ok)
		{
			return {};
		}
		const auto curve = curves.data.value("curves").toObject().value("svs.pitch").toObject();
		return curve.value("points").toArray();
	}
};

QTEST_GUILESS_MAIN(SvsPitchCommandsTest)
#include "SvsPitchCommandsTest.moc"
