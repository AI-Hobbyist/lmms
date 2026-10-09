#include <QtTest>

#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "agent/CommandBus.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "PatternStore.h"
#include "PluginFactory.h"
#include "InstrumentTrack.h"
#include "Instrument.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"

namespace {

constexpr int DrumTempo = 128;
constexpr int KickKey = 36;

QJsonArray fourOnTheFloorNotes()
{
	QJsonArray notes;
	const int ticksPerBar = lmms::TimePos::ticksPerBar();
	const int beatLength = ticksPerBar / 4;
	const int noteLength = ticksPerBar / 8;

	for (int bar = 0; bar < 8; ++bar)
	{
		for (int beat = 0; beat < 4; ++beat)
		{
			notes.append(QJsonObject{{"position", bar * ticksPerBar + beat * beatLength}, {"length", noteLength},
				{"key", KickKey}, {"volume", 100}, {"panning", 0}});
		}
	}

	return notes;
}

} // namespace

class CoreCommandsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { lmms::Engine::init(true); }

	void cleanupTestCase() { lmms::Engine::destroy(); }

	void init()
	{
		lmms::Engine::getSong()->clearProject();
		lmms::Engine::projectJournal()->clearJournal();
	}

	void cleanup()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		if (bus.isBatchActive())
		{
			QVERIFY(bus.rollbackBatch().ok);
		}
		lmms::Engine::getSong()->stop();
	}

	void registersThePlannedA1ThroughA3Commands()
	{
		QStringList names;
		for (const auto& descriptor : lmms::agent::CommandBus::instance().descriptors())
		{
			names.append(descriptor.name);
		}
		QFile plan(QFINDTESTDATA("../../../AGENT_SUPPORT_PLAN.md"));
		QVERIFY(plan.open(QIODevice::ReadOnly));
		int checked = 0;
		for (auto line : QString::fromUtf8(plan.readAll()).split('\n'))
		{
			line = line.trimmed();
			if (!line.endsWith("| A1 |") && !line.endsWith("| A2 |") && !line.endsWith("| A3 |"))
			{
				continue;
			}
			const auto column = line.split('|')[1].remove('`').trimmed();
			const auto prefix = column.section('.', 0, 0);
			for (auto name : column.split('/'))
			{
				name = name.trimmed();
				if (!name.contains('.'))
				{
					name = prefix + '.' + name;
				}
				QVERIFY2(names.contains(name), qPrintable(name));
				++checked;
			}
		}
		QVERIFY2(checked >= 130, "The A1/A2/A3 appendix must be included in the command audit.");
	}

	void writesInstrumentParametersInNativeUnitsAndPatternContainer()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		QVERIFY(bus.execute("track.create", {{"parent", "pattern"}}).ok);
		QVERIFY(bus.execute("instrument.setVolume", {{"parent", "pattern"}, {"track", 0}, {"value", 123}}).ok);
		QVERIFY(
			bus.execute(
				   "instrument.setParameters", {{"parent", "pattern"}, {"track", 0}, {"pitchRange", 2}, {"pitch", 150}})
				.ok);
		QCOMPARE(bus.execute("model.getValue", {{"path", "pattern/track:0/instrument/pitch"}})
					 .data.value("value")
					 .toDouble(),
			150.0);
		QVERIFY(bus.execute("instrument.setPitch", {{"parent", "pattern"}, {"track", 0}, {"value", -200}}).ok);
		QVERIFY(!bus.execute("instrument.setPitch", {{"parent", "pattern"}, {"track", 0}, {"value", 201}}).ok);
		QCOMPARE(bus.execute("model.getValue", {{"path", "pattern/track:0/instrument/pitch"}})
					 .data.value("value")
					 .toDouble(),
			-200.0);
		QVERIFY(bus.execute("instrument.setPitchRange", {{"parent", "pattern"}, {"track", 0}, {"value", 1}}).ok);
		QCOMPARE(bus.execute("model.getValue", {{"path", "pattern/track:0/instrument/pitch"}})
					 .data.value("value")
					 .toDouble(),
			-100.0);
		QVERIFY(
			!bus.execute("instrument.setParameters",
					{{"track", "pattern/track:0"}, {"pitchRange", 2}, {"pitch", 250}, {"volume", 111}})
				.ok);
		QCOMPARE(bus.execute("model.getValue", {{"path", "pattern/track:0/instrument/volume"}})
					 .data.value("value")
					 .toDouble(),
			123.0);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(bus.execute("model.getValue", {{"path", "pattern/track:0/instrument/pitch"}})
					 .data.value("value")
					 .toDouble(),
			-200.0);
	}

	void createsEightBarDrumPatternAndQueriesIt()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		auto* journal = lmms::Engine::projectJournal();
		const int ticksPerBar = lmms::TimePos::ticksPerBar();

		QVERIFY(song->tracks().empty());
		QVERIFY(!journal->canUndo());
		QVERIFY(bus.beginBatch("eight-bar drum pattern"));

		QVERIFY(bus.execute("song.setTempo", QJsonObject{{"bpm", DrumTempo}}).ok);

		const auto createdTrack = bus.execute("track.create", QJsonObject{{"type", "instrument"}, {"name", "Drums"}});
		QVERIFY(createdTrack.ok);
		QCOMPARE(createdTrack.data.value("path").toString(), QString("song/track:0"));
		QCOMPARE(createdTrack.data.value("name").toString(), QString("Drums"));

		const auto parameters = bus.execute("instrument.setParameters",
			QJsonObject{{"track", 0}, {"volume", 90.0}, {"panning", 0.0}, {"baseNote", KickKey}});
		QVERIFY(parameters.ok);
		QCOMPARE(parameters.data.value("parameters").toObject().value("baseNote").toInt(), KickKey);

		const auto createdClip = bus.execute("clip.create",
			QJsonObject{
				{"track", 0}, {"type", "midi"}, {"start", 0}, {"length", 8 * ticksPerBar}, {"name", "Kick Pattern"}});
		QVERIFY(createdClip.ok);
		QCOMPARE(createdClip.data.value("path").toString(), QString("song/track:0/clip:0"));
		QCOMPARE(createdClip.data.value("length").toInt(), 8 * ticksPerBar);

		const auto addedNotes
			= bus.execute("midi.addNotes", QJsonObject{{"track", 0}, {"clip", 0}, {"notes", fourOnTheFloorNotes()}});
		QVERIFY(addedNotes.ok);
		QCOMPARE(addedNotes.data.value("added").toInt(), 32);
		QCOMPARE(addedNotes.data.value("noteCount").toInt(), 32);
		QVERIFY(bus.endBatch(true).ok);
		QVERIFY(journal->canUndo());

		const auto summary = bus.execute("query.songSummary");
		QVERIFY(summary.ok);
		QCOMPARE(summary.data.value("tempo").toInt(), DrumTempo);
		QCOMPARE(summary.data.value("trackCount").toInt(), 1);
		QCOMPARE(summary.data.value("lengthBars").toInt(), 8);

		const auto track = bus.execute("query.trackDetail", QJsonObject{{"track", 0}});
		QVERIFY(track.ok);
		QCOMPARE(track.data.value("path").toString(), QString("song/track:0"));
		QCOMPARE(track.data.value("name").toString(), QString("Drums"));
		QCOMPARE(track.data.value("clipCount").toInt(), 1);
		QCOMPARE(track.data.value("instrumentParameters").toObject().value("volume").toDouble(), 90.0);

		const auto clip = bus.execute("query.clipDetail", QJsonObject{{"track", 0}, {"clip", 0}});
		QVERIFY(clip.ok);
		QCOMPARE(clip.data.value("path").toString(), QString("song/track:0/clip:0"));
		QCOMPARE(clip.data.value("length").toInt(), 8 * ticksPerBar);
		QCOMPARE(clip.data.value("noteCount").toInt(), 32);

		const auto notes = bus.execute("query.notes", QJsonObject{{"track", 0}, {"clip", 0}});
		QVERIFY(notes.ok);
		const auto noteList = notes.data.value("notes").toArray();
		QCOMPARE(noteList.size(), 32);
		QCOMPARE(noteList.first().toObject().value("position").toInt(), 0);
		QCOMPARE(noteList.last().toObject().value("position").toInt(), 7 * ticksPerBar + 3 * ticksPerBar / 4);
		QCOMPARE(noteList.last().toObject().value("key").toInt(), KickKey);

		const int clipsBeforeFailedQuery = static_cast<int>(song->tracks().at(0)->getClips().size());
		const auto missingClip = bus.execute("query.clipDetail", QJsonObject{{"track", 0}, {"clip", 1}});
		QVERIFY(!missingClip.ok);
		QCOMPARE(missingClip.errorCode, QString("clip_not_found"));
		QCOMPARE(static_cast<int>(song->tracks().at(0)->getClips().size()), clipsBeforeFailedQuery);

		const auto invalidNote = bus.execute("midi.addNotes",
			QJsonObject{{"track", 0}, {"clip", 0},
				{"notes", QJsonArray{QJsonObject{{"position", -1}, {"length", ticksPerBar / 8}, {"key", KickKey}}}}});
		QVERIFY(!invalidNote.ok);
		QCOMPARE(invalidNote.errorCode, QString("invalid_arguments"));
		QCOMPARE(
			bus.execute("query.notes", QJsonObject{{"track", 0}, {"clip", 0}}).data.value("notes").toArray().size(),
			32);

		const auto seek = bus.execute("transport.seek", QJsonObject{{"ticks", ticksPerBar}});
		QVERIFY(seek.ok);
		QCOMPARE(seek.data.value("mode").toString(), QString("song"));
		QCOMPARE(seek.data.value("ticks").toInt(), ticksPerBar);
		QVERIFY(journal->canUndo());

		QVERIFY(bus.execute("history.undo").ok);
		const auto afterUndo = bus.execute("query.songSummary");
		QVERIFY(afterUndo.ok);
		QCOMPARE(afterUndo.data.value("trackCount").toInt(), 0);

		QVERIFY(bus.execute("history.redo").ok);
		const auto afterRedo = bus.execute("query.songSummary");
		QVERIFY(afterRedo.ok);
		QCOMPARE(afterRedo.data.value("tempo").toInt(), DrumTempo);
		QCOMPARE(afterRedo.data.value("trackCount").toInt(), 1);
		QCOMPARE(
			bus.execute("query.notes", QJsonObject{{"track", 0}, {"clip", 0}}).data.value("notes").toArray().size(),
			32);
	}

	void editsTrackAndClipPropertiesWithOneUndoStep()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		QVERIFY(bus.beginBatch("Track and clip editing"));
		QVERIFY(bus.execute("track.create", {{"name", "Original"}}).ok);
		QVERIFY(bus.execute("track.create", {{"name", "Sample"}, {"type", "sample"}, {"index", 0}}).ok);
		QVERIFY(bus.execute("track.clone", {{"track", "Original"}, {"name", "Copy"}}).ok);
		QVERIFY(bus.execute("track.move", {{"track", "Copy"}, {"newIndex", 0}}).ok);
		QVERIFY(bus.execute("track.setHeight", {{"track", "song/track:0"}, {"value", 64}}).ok);
		QVERIFY(bus.execute("track.setColor", {{"track", "Copy"}, {"value", "#123456"}}).ok);
		QVERIFY(bus.execute("track.setMixerChannel", {{"track", 0}, {"channel", 0}}).ok);
		QVERIFY(bus.execute("clip.create", {{"track", "Copy"}, {"length", 192}}).ok);
		QVERIFY(bus.execute("clip.setAutoResize", {{"track", 0}, {"clip", 0}, {"value", false}}).ok);
		QVERIFY(bus.execute("clip.setStartTimeOffset", {{"track", 0}, {"clip", 0}, {"value", -12}}).ok);
		QVERIFY(bus.execute("clip.setColor", {{"track", 0}, {"clip", 0}, {"value", "#abcdef"}}).ok);
		QVERIFY(bus.execute("clip.duplicate", {{"track", 0}, {"clip", 0}}).ok);
		QVERIFY(bus.execute("clip.move", {{"track", 0}, {"clip", 1}, {"bar", 4}}).ok);
		QVERIFY(bus.execute("track.setName", {{"track", 0}, {"value", "Edited"}}).ok);
		const auto beforePreview = bus.execute("track.list").data.value("tracks").toArray();
		QVERIFY(bus.execute("track.clone", {{"track", 0}, {"dryRun", true}}).ok);
		QCOMPARE(bus.execute("track.list").data.value("tracks").toArray(), beforePreview);
		QCOMPARE(bus.batchDepth(), 1);
		QVERIFY(bus.endBatch(true).ok);
		QCOMPARE(lmms::Engine::projectJournal()->undoDepth(), 1);
		const auto detail = bus.execute("query.trackDetail", {{"track", "Edited"}});
		QVERIFY(detail.ok);
		QCOMPARE(detail.data.value("height").toInt(), 64);
		QCOMPARE(detail.data.value("color").toString(), QString("#123456"));
		QVERIFY(detail.data.value("effects").isArray());
		const auto clips = detail.data.value("clips").toArray();
		QCOMPARE(clips.size(), 2);
		QCOMPARE(clips[1].toObject().value("start").toInt(), 4 * lmms::TimePos::ticksPerBar());
		QCOMPARE(clips[1].toObject().value("startTimeOffset").toInt(), -12);
		QCOMPARE(clips[1].toObject().value("color").toString(), QString("#abcdef"));
		QVERIFY(!clips[1].toObject().value("autoResize").toBool());
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(lmms::Engine::getSong()->tracks().size(), std::size_t{0});
		QVERIFY(bus.execute("history.redo").ok);
		QCOMPARE(bus.execute("track.list").data.value("tracks").toArray(), beforePreview);
	}

	void editsNotesUsingReturnedIndicesAndFilteredPages()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		QVERIFY(bus.execute("track.create").ok);
		QVERIFY(bus.execute("clip.create", {{"track", 0}}).ok);
		const auto added = bus.execute("midi.addNotes",
			{{"track", 0}, {"clip", 0},
				{"notes",
					QJsonArray{QJsonObject{{"position", 25}, {"length", 12}, {"key", 60}},
						QJsonObject{{"position", 1}, {"length", 12}, {"key", 62}},
						QJsonObject{{"position", 50}, {"length", 12}, {"key", 60}}}}});
		QVERIFY(added.ok);
		QCOMPARE(added.data.value("indices").toArray(), (QJsonArray{1, 0, 2}));
		const auto updated = bus.execute("midi.updateNote",
			{{"track", 0}, {"clip", 0}, {"note", 1}, {"fields", QJsonObject{{"position", 70}, {"volume", 80}}}});
		QVERIFY(updated.ok);
		QCOMPARE(updated.data.value("index").toInt(), 2);
		const auto filtered = bus.execute("query.notes",
			{{"track", 0}, {"clip", 0}, {"keys", QJsonArray{60}}, {"range", QJsonObject{{"start", 0}, {"end", 100}}},
				{"page", 1}, {"pageSize", 1}});
		QVERIFY(filtered.ok);
		QCOMPARE(filtered.data.value("total").toInt(), 2);
		QCOMPARE(filtered.data.value("notes").toArray()[0].toObject().value("position").toInt(), 70);
		const auto original = bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}}).data;
		const auto preview
			= bus.execute("midi.transpose", {{"track", 0}, {"clip", 0}, {"semitones", 12}, {"dryRun", true}});
		QVERIFY(preview.ok);
		QVERIFY(!preview.data.value("diff").toObject().value("changes").toArray().isEmpty());
		QCOMPARE(bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}}).data, original);
		QVERIFY(!bus.execute("midi.transpose", {{"track", 0}, {"clip", 0}, {"semitones", 100000}}).ok);
		QCOMPARE(bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}}).data, original);
		QVERIFY(bus.execute("midi.quantize", {{"track", 0}, {"clip", 0}, {"grid", 16}, {"strength", 1.0}}).ok);
		QVERIFY(
			bus.execute("midi.transpose", {{"track", 0}, {"clip", 0}, {"keys", QJsonArray{60}}, {"semitones", 2}}).ok);
		const auto removed = bus.execute(
			"midi.removeNotes", {{"track", 0}, {"clip", 0}, {"range", QJsonObject{{"start", 48}, {"end", 72}}}});
		QVERIFY(removed.ok);
		QCOMPARE(removed.data.value("removed").toInt(), 1);
		QCOMPARE(removed.data.value("noteCount").toInt(), 2);
		QVERIFY(
			!bus.execute(
					"midi.updateNote", {{"track", 0}, {"clip", 0}, {"note", 0}, {"fields", QJsonObject{{"typo", 1}}}})
				.ok);
		QVERIFY(!bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}, {"pageSize", 0}}).ok);
		QVERIFY(bus.execute("midi.removeNotes", {{"track", 0}, {"clip", 0}}).ok);
		QCOMPARE(bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}}).data.value("total").toInt(), 0);
	}

	void loadsNativeInstrumentPlugins()
	{
		const auto pluginDirectory = qEnvironmentVariable("LMMS_AGENT_PLUGIN_TEST_PATH");
		if (pluginDirectory.isEmpty())
		{
			QSKIP("Set LMMS_AGENT_PLUGIN_TEST_PATH to run native DLL integration.");
		}
		QDir::setSearchPaths("plugins", {pluginDirectory});
		lmms::PluginFactory::instance()->discoverPlugins();
		auto& bus = lmms::agent::CommandBus::instance();
		QVERIFY(bus.execute("track.create", {{"type", "instrument"}}).ok);
		const auto loaded = bus.execute("instrument.load", {{"track", 0}, {"plugin", "tripleoscillator"}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		auto* track = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks()[0]);
		QCOMPARE(QString(track->instrument()->descriptor()->name), QString("tripleoscillator"));
		QVERIFY(
			!bus.execute("instrument.load",
					{{"track", 0}, {"plugin", "tripleoscillator"},
						{"subKey", QJsonObject{{"attributes", QJsonObject{{"id", "invalid"}}}}}})
				.ok);
		track = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks()[0]);
		QCOMPARE(QString(track->instrument()->descriptor()->name), QString("tripleoscillator"));
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		QFile wav(temporary.filePath("sample.wav"));
		QVERIFY(wav.open(QIODevice::WriteOnly));
		const auto data = QByteArray::fromHex(
			"524946462800000057415645666d7420100000000100010044ac00008858010002001000646174610400000000000000");
		QCOMPARE(wav.write(data), qint64(data.size()));
		wav.close();
		QVERIFY(
			bus.execute("instrument.load",
				   {{"track", 0}, {"plugin", "audiofileprocessor"}, {"path", wav.fileName()}, {"dryRun", true}})
				.ok);
		track = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks()[0]);
		QCOMPARE(QString(track->instrument()->descriptor()->name), QString("tripleoscillator"));
		const auto sampled = bus.execute(
			"instrument.load", {{"track", 0}, {"plugin", "audiofileprocessor"}, {"path", wav.fileName()}});
		QVERIFY2(sampled.ok, qPrintable(sampled.errorMessage));
		track = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks()[0]);
		QDomDocument document;
		auto root = document.createElement("state");
		document.appendChild(root);
		const auto state = track->instrument()->saveState(document, root);
		QCOMPARE(QFileInfo(state.attribute("src")).fileName(), QString("sample.wav"));
		QVERIFY(bus.execute("history.undo").ok);
		track = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks()[0]);
		QCOMPARE(QString(track->instrument()->descriptor()->name), QString("tripleoscillator"));
		QVERIFY(bus.execute("history.redo").ok);
		track = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks()[0]);
		QCOMPARE(QString(track->instrument()->descriptor()->name), QString("audiofileprocessor"));
		const auto setParameter = bus.execute("instrument.setParam", {{"track", 0}, {"name", "amp"}, {"value", 125}});
		QVERIFY2(setParameter.ok, qPrintable(setParameter.errorMessage));
		const auto amplitude = [&bus]() {
			const auto queried = bus.execute("instrument.getParams", {{"track", 0}});
			for (const auto& parameter : queried.data.value("parameters").toArray())
			{
				const auto object = parameter.toObject();
				if (object.value("name").toString() == "amp")
				{
					return object.value("value").toDouble();
				}
			}
			return -1.0;
		};
		QCOMPARE(amplitude(), 125.0);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(amplitude(), 100.0);
		QVERIFY(bus.execute("history.redo").ok);
		QCOMPARE(amplitude(), 125.0);
	}

	void selectsPlaybackModesAndPreviewsAutomation()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		QVERIFY(bus.execute("track.create", {{"type", "instrument"}}).ok);
		QVERIFY(bus.execute("clip.create", {{"track", 0}, {"position", 0}, {"length", 192}}).ok);
		QVERIFY(bus.execute("track.create", {{"type", "automation"}}).ok);
		QVERIFY(bus.execute("clip.create", {{"track", 1}, {"position", 384}, {"length", 192}}).ok);
		QVERIFY(bus.execute("automation.addTarget", {{"track", 1}, {"clip", 0}, {"target", "song/masterVolume"}}).ok);
		QVERIFY(bus.execute("automation.putValue", {{"track", 1}, {"clip", 0}, {"pos", 0}, {"value", 75}}).ok);
		const auto depth = bus.execute("history.status").data.value("undoDepth");
		QVERIFY(bus.execute("song.setPlayMode", {{"mode", "MidiClip"}, {"track", 0}, {"clip", 0}}).ok);
		QCOMPARE(song->playMode(), lmms::Song::PlayMode::MidiClip);
		QVERIFY(bus.execute("transport.play", {{"dryRun", true}}).ok);
		QVERIFY(!song->isPlaying());
		QVERIFY(bus.execute("transport.play", {{"fromBar", 1}}).ok);
		QVERIFY(song->isPlaying());
		QVERIFY(bus.execute("transport.stop", {{"dryRun", true}}).ok);
		QVERIFY(song->isPlaying());
		QVERIFY(bus.execute("transport.stop").ok);
		QVERIFY(
			bus.execute("transport.play", {{"mode", "AutomationClip"}, {"track", 1}, {"clip", 0}, {"ticks", 0}}).ok);
		QCOMPARE(song->playMode(), lmms::Song::PlayMode::AutomationClip);
		song->processNextBuffer();
		QCOMPARE(song->masterVolume(), 75);
		QVERIFY(bus.execute("transport.playSong").ok);
		QCOMPARE(song->playMode(), lmms::Song::PlayMode::Song);
		QVERIFY(bus.execute("transport.stop").ok);
		QVERIFY(bus.execute("song.setPlayMode", {{"mode", "MidiClip"}, {"track", 0}, {"clip", 0}}).ok);
		QVERIFY(bus.execute("clip.remove", {{"track", 0}, {"clip", 0}}).ok);
		QVERIFY(song->previewClip() == nullptr);
		QCOMPARE(song->playMode(), lmms::Song::PlayMode::None);
		QVERIFY(!bus.execute("transport.play", {{"mode", "AutomationClip"}, {"track", 0}, {"clip", 0}}).ok);
		QCOMPARE(bus.execute("history.status").data.value("undoDepth").toInt(), depth.toInt() + 1);
	}

	void addressesPatternTracksAndRestoresTheirContainer()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		QVERIFY(bus.beginBatch("Pattern tracks"));
		QVERIFY(bus.execute("track.create", {{"parent", "pattern"}, {"name", "First"}}).ok);
		QVERIFY(bus.execute("track.create", {{"parent", "pattern"}, {"name", "Second"}, {"index", 0}}).ok);
		const auto list = bus.execute("track.list", {{"parent", "pattern"}});
		QVERIFY(list.ok);
		const auto tracks = list.data.value("tracks").toArray();
		QCOMPARE(tracks.size(), 2);
		QCOMPARE(tracks[0].toObject().value("path").toString(), QString("pattern/track:0"));
		QCOMPARE(
			bus.execute("track.get", {{"track", "pattern/track:0"}}).data.value("name").toString(), QString("Second"));
		QCOMPARE(bus.execute("track.get", {{"track", "First"}, {"parent", "pattern"}}).data.value("index").toInt(), 1);
		QVERIFY(bus.endBatch(true).ok);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(lmms::Engine::patternStore()->tracks().size(), std::size_t{0});
		QVERIFY(bus.execute("history.redo").ok);
		QCOMPARE(bus.execute("track.list", {{"parent", "pattern"}}).data.value("tracks").toArray(), tracks);
		QVERIFY(bus.execute("transport.play", {{"mode", "Pattern"}, {"pattern", 0}}).ok);
		QCOMPARE(lmms::Engine::getSong()->playMode(), lmms::Song::PlayMode::Pattern);
		QVERIFY(bus.execute("transport.stop").ok);
		QVERIFY(!bus.execute("track.get", {{"track", "pattern/track:0"}, {"parent", "song"}}).ok);
	}
};

QTEST_GUILESS_MAIN(CoreCommandsTest)
#include "CoreCommandsTest.moc"
