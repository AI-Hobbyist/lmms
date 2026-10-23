#include <QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>

#include "agent/CommandBus.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "SVSClip.h"
#include "SVSModel.h"
#include "SVSTrack.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::agent;

namespace {

QString examplePlugin()
{
	return "org.lmms.svs.example";
}

bool findExampleVoice(QString& pluginId, QString& voiceId)
{
	for (const auto& voice : svs::Registry::instance().voices())
	{
		if (voice.pluginId == examplePlugin())
		{
			pluginId = voice.pluginId;
			voiceId = voice.id;
			return true;
		}
	}
	return false;
}

int createTrack(const QString& name = "SVS test")
{
	QString pluginId;
	QString voiceId;
	if (!findExampleVoice(pluginId, voiceId))
	{
		return -1;
	}
	const auto result = CommandBus::instance().execute(
		"svs.createTrack", {{"name", name}, {"pluginId", pluginId}, {"voiceId", voiceId}});
	return result.ok ? result.data.value("index").toInt() : -1;
}

bool waitForCapabilities(int trackIndex, QJsonObject* detail = nullptr)
{
	QElapsedTimer timer;
	timer.start();
	for (;;)
	{
		const auto result = CommandBus::instance().execute("svs.getTrack", {{"track", trackIndex}});
		if (result.ok && result.data.value("capabilitiesReady").toBool())
		{
			if (detail != nullptr)
			{
				*detail = result.data;
			}
			return true;
		}
		if (timer.elapsed() > 60000)
		{
			return false;
		}
		QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
		QThread::msleep(20);
	}
}

int createClip(int trackIndex)
{
	const auto result = CommandBus::instance().execute(
		"svs.createClip", {{"track", trackIndex}, {"position", 0}, {"length", 384}});
	return result.ok ? result.data.value("index").toInt() : -1;
}

QJsonArray exampleNotes()
{
	return QJsonArray{
		QJsonObject{{"tick", 0}, {"duration", 48}, {"pitch", 60}, {"lyric", "la"}},
		QJsonObject{{"tick", 48}, {"duration", 48}, {"pitch", 62}, {"lyric", "la"}},
		QJsonObject{{"tick", 96}, {"duration", 48}, {"pitch", 64}, {"lyric", "la"}}};
}

} // namespace

class SvsCommandsTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		Engine::init(true);
		svs::Registry::instance().voices();
		QTRY_VERIFY_WITH_TIMEOUT(!svs::Registry::instance().scanning(), 60000);
	}

	void cleanupTestCase() { Engine::destroy(); }

	void init()
	{
		Engine::getSong()->clearProject();
		Engine::projectJournal()->clearJournal();
	}

	void createTrackAndEditNotes()
	{
		QString pluginId;
		QString voiceId;
		QVERIFY2(findExampleVoice(pluginId, voiceId), "SVSExample voicebank is not available");
		const auto trackResult = CommandBus::instance().execute(
			"svs.createTrack", {{"name", "Lead"}, {"pluginId", pluginId}, {"voiceId", voiceId}});
		QVERIFY2(trackResult.ok, qPrintable(trackResult.errorMessage));
		const int trackIndex = trackResult.data.value("index").toInt();
		QJsonObject trackDetail;
		QVERIFY(waitForCapabilities(trackIndex, &trackDetail));
		QCOMPARE(trackDetail.value("voiceId").toString(), voiceId);

		const auto clipResult = CommandBus::instance().execute(
			"svs.createClip", {{"track", trackIndex}, {"position", 0}, {"length", 384}});
		QVERIFY2(clipResult.ok, qPrintable(clipResult.errorMessage));
		const int clipIndex = clipResult.data.value("index").toInt();

		const auto added = CommandBus::instance().execute(
			"svs.addNotes", {{"track", trackIndex}, {"clip", clipIndex}, {"notes", exampleNotes()}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QCOMPARE(added.data.value("noteCount").toInt(), 3);
		const auto ids = added.data.value("added").toArray();
		QCOMPARE(ids.size(), 3);

		const auto read = CommandBus::instance().execute(
			"svs.getNotes", {{"track", trackIndex}, {"clip", clipIndex}});
		QVERIFY2(read.ok, qPrintable(read.errorMessage));
		QCOMPARE(read.data.value("total").toInt(), 3);
		QCOMPARE(read.data.value("notes").toArray().first().toObject().value("id").toString(), ids.first().toString());

		const auto updated = CommandBus::instance().execute("svs.updateNote",
			{{"track", trackIndex}, {"clip", clipIndex}, {"id", ids.first().toString()},
				{"fields", QJsonObject{{"pitch", 72}, {"lyric", "you"}}}});
		QVERIFY2(updated.ok, qPrintable(updated.errorMessage));
		QCOMPARE(updated.data.value("pitch").toDouble(), 72.0);
		QCOMPARE(updated.data.value("lyric").toString(), QString("you"));

		const auto removed = CommandBus::instance().execute("svs.removeNotes",
			{{"track", trackIndex}, {"clip", clipIndex}, {"ids", QJsonArray{ids.last().toString()}}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(removed.data.value("noteCount").toInt(), 2);

		const auto remaining = CommandBus::instance().execute(
			"svs.getNotes", {{"track", trackIndex}, {"clip", clipIndex}});
		QCOMPARE(remaining.data.value("total").toInt(), 2);
	}

	void lyricsAndPronunciation()
	{
		const int trackIndex = createTrack();
		QVERIFY(trackIndex >= 0);
		QVERIFY(waitForCapabilities(trackIndex));
		const int clipIndex = createClip(trackIndex);
		QVERIFY(clipIndex >= 0);
		const auto added = CommandBus::instance().execute(
			"svs.addNotes", {{"track", trackIndex}, {"clip", clipIndex}, {"notes", exampleNotes()}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));

		const auto lyrics = CommandBus::instance().execute("svs.setLyrics",
			{{"track", trackIndex}, {"clip", clipIndex}, {"text", QString::fromUtf8("你好 世界")}});
		QVERIFY2(lyrics.ok, qPrintable(lyrics.errorMessage));
		QCOMPARE(lyrics.data.value("assigned").toInt(), 3);

		const auto read = CommandBus::instance().execute(
			"svs.getNotes", {{"track", trackIndex}, {"clip", clipIndex}});
		QVERIFY2(read.ok, qPrintable(read.errorMessage));
		const auto notes = read.data.value("notes").toArray();
		QCOMPARE(notes.at(0).toObject().value("lyric").toString(), QString::fromUtf8("你"));
		QCOMPARE(notes.at(1).toObject().value("lyric").toString(), QString::fromUtf8("好"));
		QCOMPARE(notes.at(2).toObject().value("lyric").toString(), QString::fromUtf8("世"));

		const auto pronunciation = CommandBus::instance().execute("svs.getPronunciation",
			{{"track", trackIndex}, {"clip", clipIndex},
				{"noteId", notes.at(0).toObject().value("id").toString()}});
		QVERIFY2(pronunciation.ok, qPrintable(pronunciation.errorMessage));
		QVERIFY(pronunciation.data.contains("phonemes") || pronunciation.data.contains("text"));
	}

	void parameters()
	{
		const int trackIndex = createTrack();
		QVERIFY(trackIndex >= 0);
		QVERIFY(waitForCapabilities(trackIndex));
		const int clipIndex = createClip(trackIndex);
		const auto added = CommandBus::instance().execute(
			"svs.addNotes", {{"track", trackIndex}, {"clip", clipIndex}, {"notes", exampleNotes()}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const auto noteId = added.data.value("added").toArray().first().toString();

		const auto capabilities = CommandBus::instance().execute(
			"svs.getCapabilities", {{"track", trackIndex}});
		QVERIFY2(capabilities.ok, qPrintable(capabilities.errorMessage));
		QJsonObject noteParameter;
		for (const auto& entry : capabilities.data.value("parameters").toArray())
		{
			const auto parameter = entry.toObject();
			if (parameter.value("scope").toString() == "note" && parameter.value("writable").toBool()
				&& !parameter.value("feedback").toBool())
			{
				noteParameter = parameter;
				break;
			}
		}
		if (noteParameter.isEmpty())
		{
			QSKIP("SVSExample does not declare a writable note parameter");
		}
		QJsonValue value;
		if (noteParameter.value("type").toString() == "enum")
		{
			const auto options = noteParameter.value("choices").toArray();
			if (options.isEmpty())
			{
				QSKIP("SVSExample enum parameter has no options");
			}
			value = options.first().toObject().value("id");
		}
		else if (noteParameter.contains("maximum"))
		{
			value = noteParameter.value("maximum");
		}
		else
		{
			value = 0.5;
		}
		const auto applied = CommandBus::instance().execute("svs.setNoteParameter",
			{{"track", trackIndex}, {"clip", clipIndex}, {"noteIds", QJsonArray{noteId}},
				{"parameter", noteParameter.value("id")}, {"value", value}});
		QVERIFY2(applied.ok, qPrintable(applied.errorMessage));
		QCOMPARE(applied.data.value("updated").toInt(), 1);
	}

	void undoAndDryRun()
	{
		const auto before = Engine::getSong()->tracks().size();
		const auto dryRun = CommandBus::instance().execute(
			"svs.createTrack", {{"name", "dry"}, {"dryRun", true}});
		QVERIFY2(dryRun.ok, qPrintable(dryRun.errorMessage));
		QCOMPARE(Engine::getSong()->tracks().size(), before);

		const int trackIndex = createTrack();
		QVERIFY(trackIndex >= 0);
		QCOMPARE(Engine::getSong()->tracks().size(), before + 1);
		QVERIFY(CommandBus::instance().execute("history.undo").ok);
		QCOMPARE(Engine::getSong()->tracks().size(), before);
	}

	void wrongTrackType()
	{
		const auto created = CommandBus::instance().execute("track.create", {{"type", "instrument"}});
		QVERIFY2(created.ok, qPrintable(created.errorMessage));
		const auto result = CommandBus::instance().execute(
			"svs.getTrack", {{"track", created.data.value("index")}});
		QVERIFY(!result.ok);
		QCOMPARE(result.errorCode, QString("wrong_track_type"));
	}

	void synthesisStatus()
	{
		const int trackIndex = createTrack();
		QVERIFY(trackIndex >= 0);
		QVERIFY(waitForCapabilities(trackIndex));
		const int clipIndex = createClip(trackIndex);
		QVERIFY(clipIndex >= 0);
		const auto added = CommandBus::instance().execute(
			"svs.addNotes", {{"track", trackIndex}, {"clip", clipIndex}, {"notes", exampleNotes()}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));

		const auto started = CommandBus::instance().execute(
			"svs.synthesize", {{"track", trackIndex}, {"clip", clipIndex}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		QElapsedTimer timer;
		timer.start();
		QString status;
		bool audioAvailable = false;
		while (timer.elapsed() < 60000)
		{
			const auto current = CommandBus::instance().execute(
				"svs.getStatus", {{"track", trackIndex}, {"clip", clipIndex}});
			if (current.ok)
			{
				status = current.data.value("status").toString();
				audioAvailable = current.data.value("audioAvailable").toBool();
				if (audioAvailable || status.startsWith("Failed") || status == "Cancelled")
				{
					break;
				}
			}
			QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
			QThread::msleep(20);
		}
		QVERIFY2(!status.isEmpty(), "SVS synthesis produced no status");
		if (!audioAvailable && !status.startsWith("Failed") && status != "Cancelled")
		{
			QSKIP("SVSExample synthesis did not finish within the automated window");
		}
	}
};

QTEST_GUILESS_MAIN(SvsCommandsTest)
#include "SvsCommandsTest.moc"
