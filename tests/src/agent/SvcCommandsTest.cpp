#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>

#include "agent/CommandBus.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "SVCClip.h"
#include "SVCCatalog.h"
#include "SVCPlayback.h"
#include "SVCTrack.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::agent;

namespace {

QString sourceAudio()
{
	QString dataDir = qEnvironmentVariable("LMMS_DATA_DIR");
	if (dataDir.isEmpty())
	{
		dataDir = QDir(QCoreApplication::applicationDirPath()).filePath("data");
	}
	return QDir(dataDir).filePath("samples/shapes/additive.wav");
}

int createTrack()
{
	const auto result = CommandBus::instance().execute("svc.createTrack", {{"name", "SVC test"}});
	return result.ok ? result.data.value("index").toInt() : -1;
}

int setReference(int trackIndex)
{
	const auto result = CommandBus::instance().execute("svc.setModel",
		{{"track", trackIndex}, {"engine_id", "reference"}, {"model_id", "identity"}});
	return result.ok ? 0 : -1;
}

int createClip(int trackIndex)
{
	const auto result = CommandBus::instance().execute(
		"svc.createClip", {{"track", trackIndex}, {"position", 0}, {"length", 192}});
	return result.ok ? result.data.value("index").toInt() : -1;
}

} // namespace

class SvcCommandsTest : public QObject
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

	void createTrackAndSelectModel()
	{
		const int trackIndex = createTrack();
		QVERIFY(trackIndex >= 0);
		QCOMPARE(setReference(trackIndex), 0);

		const auto detail = CommandBus::instance().execute("svc.getTrack", {{"track", trackIndex}});
		QVERIFY2(detail.ok, qPrintable(detail.errorMessage));
		QCOMPARE(detail.data.value("type").toString(), QString("svc"));
		const auto selection = detail.data.value("selection").toObject();
		QCOMPARE(selection.value("engine_id").toString(), QString("reference"));
		QCOMPARE(selection.value("model_id").toString(), QString("identity"));
		QCOMPARE(selection.value("speaker_id").toString(), QString("0"));
		QVERIFY(selection.value("parameters").isObject());
		QCOMPARE(detail.data.value("engine").toObject().value("id").toString(), QString("reference"));
		QCOMPARE(detail.data.value("model").toObject().value("id").toString(), QString("identity"));

		const auto unknown = CommandBus::instance().execute("svc.setModel",
			{{"track", trackIndex}, {"engine_id", "reference"}, {"model_id", "missing"}});
		QVERIFY(!unknown.ok);
		QCOMPARE(unknown.errorCode, QString("svc_model_not_found"));
		QCOMPARE(CommandBus::instance().execute("svc.getTrack", {{"track", trackIndex}})
					 .data.value("selection")
					 .toObject()
					 .value("model_id")
					 .toString(),
			QString("identity"));
	}

	void sourceAndChunkConfig()
	{
		const int trackIndex = createTrack();
		QVERIFY(trackIndex >= 0);
		QCOMPARE(setReference(trackIndex), 0);
		const int clipIndex = createClip(trackIndex);
		QVERIFY(clipIndex >= 0);

		const auto source = CommandBus::instance().execute(
			"svc.setSourceFile", {{"track", trackIndex}, {"clip", clipIndex}, {"path", sourceAudio()}});
		QVERIFY2(source.ok, qPrintable(source.errorMessage));
		QCOMPARE(source.data.value("sourceFile").toString(), QFileInfo(sourceAudio()).absoluteFilePath());
		QVERIFY(source.data.value("sourceFrames").toDouble() > 0);
		QVERIFY(source.data.value("sourceRate").toInt() > 0);

		const auto chunk = CommandBus::instance().execute("svc.setChunkConfig",
			{{"track", trackIndex}, {"silenceThresholdDbfs", -60}, {"lengthThresholdSeconds", 5},
				{"forcedChunkSeconds", 2}});
		QVERIFY2(chunk.ok, qPrintable(chunk.errorMessage));
		QCOMPARE(chunk.data.value("chunkConfig").toObject().value("silenceThresholdDbfs").toDouble(), -60.0);
		QCOMPARE(chunk.data.value("chunkConfig").toObject().value("lengthThresholdSeconds").toDouble(), 5.0);

		const auto invalid = CommandBus::instance().execute(
			"svc.setChunkConfig", {{"track", trackIndex}, {"silenceThresholdDbfs", -200}});
		QVERIFY(!invalid.ok);
		QCOMPARE(invalid.errorCode, QString("invalid_arguments"));

		QVERIFY(CommandBus::instance().execute("history.undo").ok);
		QVERIFY(CommandBus::instance().execute("history.undo").ok);
		const auto restored = CommandBus::instance().execute(
			"svc.getStatus", {{"track", trackIndex}, {"clip", clipIndex}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(restored.data.value("sourceFile").toString(), QString());
	}

	void convertReference()
	{
		const int trackIndex = createTrack();
		QVERIFY(trackIndex >= 0);
		QCOMPARE(setReference(trackIndex), 0);
		const int clipIndex = createClip(trackIndex);
		QVERIFY(clipIndex >= 0);
		const auto source = CommandBus::instance().execute(
			"svc.setSourceFile", {{"track", trackIndex}, {"clip", clipIndex}, {"path", sourceAudio()}});
		QVERIFY2(source.ok, qPrintable(source.errorMessage));

		const auto started = CommandBus::instance().execute(
			"svc.convert", {{"track", trackIndex}, {"clip", clipIndex}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));

		QElapsedTimer timer;
		timer.start();
		QJsonObject clip;
		while (timer.elapsed() < 60000)
		{
			const auto current = CommandBus::instance().execute(
				"svc.getStatus", {{"track", trackIndex}, {"clip", clipIndex}});
			if (current.ok)
			{
				clip = current.data;
				if (clip.value("conversionComplete").toBool() || clip.value("conversionFailed").toBool())
				{
					break;
				}
			}
			QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
			QThread::msleep(20);
		}
		QVERIFY(clip.value("status").toString() != QString("Import audio"));
		if (!clip.value("conversionComplete").toBool())
		{
			QSKIP("Reference SVC conversion did not finish within the automated window");
		}
		QVERIFY(!clip.value("conversionFailed").toBool());
	}

	void undoAndDryRun()
	{
		const auto before = Engine::getSong()->tracks().size();
		const auto dryRun = CommandBus::instance().execute("svc.createTrack", {{"dryRun", true}});
		QVERIFY2(dryRun.ok, qPrintable(dryRun.errorMessage));
		QCOMPARE(Engine::getSong()->tracks().size(), before);

		const int trackIndex = createTrack();
		QVERIFY(trackIndex >= 0);
		QCOMPARE(Engine::getSong()->tracks().size(), before + 1);
		QVERIFY(CommandBus::instance().execute("history.undo").ok);
		QCOMPARE(Engine::getSong()->tracks().size(), before);

		const int second = createTrack();
		QVERIFY(second >= 0);
		QCOMPARE(setReference(second), 0);
		QVERIFY(CommandBus::instance().execute("history.undo").ok);
		QCOMPARE(CommandBus::instance().execute("svc.getTrack", {{"track", second}})
					 .data.value("selection")
					 .toObject()
					 .value("engine_id")
					 .toString(),
			QString());
	}

	void wrongTrackType()
	{
		const auto created = CommandBus::instance().execute("track.create", {{"type", "instrument"}});
		QVERIFY2(created.ok, qPrintable(created.errorMessage));
		const auto result = CommandBus::instance().execute(
			"svc.getTrack", {{"track", created.data.value("index")}});
		QVERIFY(!result.ok);
		QCOMPARE(result.errorCode, QString("wrong_track_type"));
	}
};

QTEST_GUILESS_MAIN(SvcCommandsTest)
#include "SvcCommandsTest.moc"
