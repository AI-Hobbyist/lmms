#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "agent/CommandBus.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::agent;

class MixerCommandsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }

	void cleanupTestCase() { Engine::destroy(); }

	void init()
	{
		Engine::getSong()->clearProject();
		Engine::mixer()->clear();
		Engine::projectJournal()->clearJournal();
	}

	void addChannelCreatesChannel()
	{
		const int before = static_cast<int>(Engine::mixer()->numChannels());

		const auto result = CommandBus::instance().execute("mixer.addChannel", {{"name", "Reverb Bus"}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));

		QCOMPARE(static_cast<int>(Engine::mixer()->numChannels()), before + 1);
		QCOMPARE(result.data.value("channel").toInt(), before);
		QCOMPARE(result.data.value("numChannels").toInt(), before + 1);
		QCOMPARE(result.data.value("name").toString(), QStringLiteral("Reverb Bus"));
		QCOMPARE(result.data.value("master").toBool(), false);
		QCOMPARE(result.data.value("path").toString(), QStringLiteral("song/channel:%1").arg(before));

		// A new channel is routed to the master channel like the mixer GUI does.
		const auto sends = result.data.value("sends").toArray();
		QCOMPARE(sends.size(), 1);
		QCOMPARE(sends.first().toObject().value("to").toInt(), 0);
	}

	void addChannelDryRunRollsBack()
	{
		const int before = static_cast<int>(Engine::mixer()->numChannels());

		const auto result = CommandBus::instance().execute("mixer.addChannel", {{"dryRun", true}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QVERIFY(result.data.value("dryRun").toBool());

		QCOMPARE(static_cast<int>(Engine::mixer()->numChannels()), before);
	}

	void addChannelIsUndoable()
	{
		const int before = static_cast<int>(Engine::mixer()->numChannels());

		QVERIFY(CommandBus::instance().execute("mixer.addChannel", {}).ok);
		QCOMPARE(static_cast<int>(Engine::mixer()->numChannels()), before + 1);

		const auto undo = CommandBus::instance().execute("history.undo", {});
		QVERIFY2(undo.ok, qPrintable(undo.errorMessage));
		QCOMPARE(static_cast<int>(Engine::mixer()->numChannels()), before);
	}

	void addChannelRejectsInvalidName()
	{
		const auto result = CommandBus::instance().execute("mixer.addChannel", {{"name", 42}});
		QVERIFY(!result.ok);
		QCOMPARE(result.errorCode, QStringLiteral("invalid_arguments"));
	}

	void removeChannelProtectsMaster()
	{
		const auto result = CommandBus::instance().execute("mixer.removeChannel", {{"channel", 0}});
		QVERIFY(!result.ok);
		QCOMPARE(result.errorCode, QStringLiteral("mixer_master_channel_protected"));
		QCOMPARE(static_cast<int>(Engine::mixer()->numChannels()), 1);
	}

	void removeChannelRejectsUnknownChannel()
	{
		const auto result = CommandBus::instance().execute("mixer.removeChannel", {{"channel", 7}});
		QVERIFY(!result.ok);
		QCOMPARE(result.errorCode, QStringLiteral("mixer_channel_not_found"));
	}

	void removeChannelRejectsChannelInUse()
	{
		const auto add = CommandBus::instance().execute("mixer.addChannel", {});
		QVERIFY2(add.ok, qPrintable(add.errorMessage));
		const int channelIndex = add.data.value("channel").toInt();

		QVERIFY(CommandBus::instance().execute("track.create", {{"type", "instrument"}, {"name", "Test"}}).ok);
		const auto routed = CommandBus::instance().execute("track.setMixerChannel", {{"track", 0}, {"channel", channelIndex}});
		QVERIFY2(routed.ok, qPrintable(routed.errorMessage));

		const auto blocked = CommandBus::instance().execute("mixer.removeChannel", {{"channel", channelIndex}});
		QVERIFY(!blocked.ok);
		QCOMPARE(blocked.errorCode, QStringLiteral("mixer_channel_in_use"));
		QVERIFY(!blocked.errorHint.isEmpty());
		QCOMPARE(static_cast<int>(Engine::mixer()->numChannels()), 2);
	}

	void removeChannelWithForceReroutesTracks()
	{
		const auto add = CommandBus::instance().execute("mixer.addChannel", {});
		QVERIFY2(add.ok, qPrintable(add.errorMessage));
		const int channelIndex = add.data.value("channel").toInt();

		QVERIFY(CommandBus::instance().execute("track.create", {{"type", "instrument"}, {"name", "Test"}}).ok);
		QVERIFY(CommandBus::instance().execute("track.setMixerChannel", {{"track", 0}, {"channel", channelIndex}}).ok);

		const auto removed = CommandBus::instance().execute(
			"mixer.removeChannel", {{"channel", channelIndex}, {"force", true}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(removed.data.value("removed").toInt(), channelIndex);
		QCOMPARE(static_cast<int>(Engine::mixer()->numChannels()), 1);

		const auto track = CommandBus::instance().execute("track.get", {{"track", 0}});
		QVERIFY2(track.ok, qPrintable(track.errorMessage));
		QCOMPARE(track.data.value("mixerChannel").toInt(), 0);
	}

	void removeChannelIsUndoable()
	{
		const auto add = CommandBus::instance().execute("mixer.addChannel", {});
		QVERIFY2(add.ok, qPrintable(add.errorMessage));
		const int channelIndex = add.data.value("channel").toInt();
		QCOMPARE(static_cast<int>(Engine::mixer()->numChannels()), 2);

		QVERIFY(CommandBus::instance().execute("mixer.removeChannel", {{"channel", channelIndex}}).ok);
		QCOMPARE(static_cast<int>(Engine::mixer()->numChannels()), 1);

		const auto undo = CommandBus::instance().execute("history.undo", {});
		QVERIFY2(undo.ok, qPrintable(undo.errorMessage));
		QCOMPARE(static_cast<int>(Engine::mixer()->numChannels()), 2);
	}

	void moveChannelSwapsWithNeighbour()
	{
		QVERIFY(CommandBus::instance().execute("mixer.addChannel", {{"name", "First"}}).ok);
		QVERIFY(CommandBus::instance().execute("mixer.addChannel", {{"name", "Second"}}).ok);

		const auto moved = CommandBus::instance().execute("mixer.moveChannel", {{"channel", 2}, {"direction", "left"}});
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QCOMPARE(moved.data.value("channel").toInt(), 1);
		QCOMPARE(moved.data.value("name").toString(), QStringLiteral("Second"));
		QCOMPARE(Engine::mixer()->mixerChannel(1)->m_name, QStringLiteral("Second"));
		QCOMPARE(Engine::mixer()->mixerChannel(2)->m_name, QStringLiteral("First"));

		const auto back = CommandBus::instance().execute("mixer.moveChannel", {{"channel", 1}, {"direction", "right"}});
		QVERIFY2(back.ok, qPrintable(back.errorMessage));
		QCOMPARE(back.data.value("channel").toInt(), 2);
		QCOMPARE(Engine::mixer()->mixerChannel(1)->m_name, QStringLiteral("First"));
	}

	void moveChannelRejectsEdges()
	{
		const auto master = CommandBus::instance().execute("mixer.moveChannel", {{"channel", 0}, {"direction", "right"}});
		QVERIFY(!master.ok);
		QCOMPARE(master.errorCode, QStringLiteral("mixer_channel_at_edge"));

		QVERIFY(CommandBus::instance().execute("mixer.addChannel", {}).ok);
		const auto first = CommandBus::instance().execute("mixer.moveChannel", {{"channel", 1}, {"direction", "left"}});
		QVERIFY(!first.ok);
		QCOMPARE(first.errorCode, QStringLiteral("mixer_channel_at_edge"));
	}

	void moveChannelRejectsInvalidDirection()
	{
		const auto result = CommandBus::instance().execute("mixer.moveChannel", {{"channel", 1}, {"direction", "up"}});
		QVERIFY(!result.ok);
		QCOMPARE(result.errorCode, QStringLiteral("invalid_arguments"));
	}

	void setMixerChannelRoutesSvsTrack()
	{
		const auto add = CommandBus::instance().execute("mixer.addChannel", {{"name", "Vocal FX"}});
		QVERIFY2(add.ok, qPrintable(add.errorMessage));
		const int channelIndex = add.data.value("channel").toInt();

		const auto track = CommandBus::instance().execute("svs.createTrack", {{"name", "Vocal"}});
		QVERIFY2(track.ok, qPrintable(track.errorMessage));
		const int trackIndex = track.data.value("index").toInt();

		const auto routed = CommandBus::instance().execute(
			"track.setMixerChannel", {{"track", trackIndex}, {"channel", channelIndex}});
		QVERIFY2(routed.ok, qPrintable(routed.errorMessage));
		QCOMPARE(routed.data.value("mixerChannel").toInt(), channelIndex);
		QCOMPARE(routed.data.value("type").toString(), QStringLiteral("svs"));

		const auto detail = CommandBus::instance().execute("svs.getTrack", {{"track", trackIndex}});
		QVERIFY2(detail.ok, qPrintable(detail.errorMessage));
		QCOMPARE(detail.data.value("mixerChannel").toInt(), channelIndex);

		const auto undo = CommandBus::instance().execute("history.undo", {});
		QVERIFY2(undo.ok, qPrintable(undo.errorMessage));
		const auto restored = CommandBus::instance().execute("track.get", {{"track", trackIndex}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(restored.data.value("mixerChannel").toInt(), 0);
	}

	void setMixerChannelRoutesSvcTrack()
	{
		const auto add = CommandBus::instance().execute("mixer.addChannel", {});
		QVERIFY2(add.ok, qPrintable(add.errorMessage));
		const int channelIndex = add.data.value("channel").toInt();

		const auto track = CommandBus::instance().execute("svc.createTrack", {{"name", "Conversion"}});
		QVERIFY2(track.ok, qPrintable(track.errorMessage));
		const int trackIndex = track.data.value("index").toInt();

		const auto routed = CommandBus::instance().execute(
			"track.setMixerChannel", {{"track", trackIndex}, {"channel", channelIndex}});
		QVERIFY2(routed.ok, qPrintable(routed.errorMessage));
		QCOMPARE(routed.data.value("mixerChannel").toInt(), channelIndex);
		QCOMPARE(routed.data.value("type").toString(), QStringLiteral("svc"));
	}

	void setMixerChannelRejectsUnroutableTrack()
	{
		QVERIFY(CommandBus::instance().execute("track.create", {{"type", "automation"}, {"name", "Auto"}}).ok);

		const auto routed = CommandBus::instance().execute("track.setMixerChannel", {{"track", 0}, {"channel", 0}});
		QVERIFY(!routed.ok);
		QCOMPARE(routed.errorCode, QStringLiteral("wrong_track_type"));
	}

	void setMixerChannelRejectsUnknownChannel()
	{
		QVERIFY(CommandBus::instance().execute("track.create", {{"type", "instrument"}}).ok);

		const auto routed = CommandBus::instance().execute("track.setMixerChannel", {{"track", 0}, {"channel", 4}});
		QVERIFY(!routed.ok);
		QCOMPARE(routed.errorCode, QStringLiteral("invalid_arguments"));
	}

	void setMixerChannelAcceptsTrackName()
	{
		QVERIFY(CommandBus::instance().execute("mixer.addChannel", {}).ok);
		QVERIFY(CommandBus::instance().execute("track.create", {{"type", "instrument"}, {"name", "Keys"}}).ok);

		const auto routed = CommandBus::instance().execute("track.setMixerChannel", {{"track", "Keys"}, {"channel", 1}});
		QVERIFY2(routed.ok, qPrintable(routed.errorMessage));
		QCOMPARE(routed.data.value("mixerChannel").toInt(), 1);
	}

	void reportsUnknownTrackNameClearly()
	{
		const auto routed = CommandBus::instance().execute("track.setMixerChannel", {{"track", "0"}, {"channel", 0}});
		QVERIFY(!routed.ok);
		QCOMPARE(routed.errorCode, QStringLiteral("track_not_found"));
		QCOMPARE(routed.errorMessage, QStringLiteral("Track \"0\" was not found by name."));
		QVERIFY(!routed.errorHint.isEmpty());
	}
};

QTEST_GUILESS_MAIN(MixerCommandsTest)
#include "MixerCommandsTest.moc"
