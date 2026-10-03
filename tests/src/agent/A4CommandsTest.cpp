#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QDataStream>
#include <QFile>
#include <cmath>
#include "agent/CommandBus.h"
#include "agent/ScriptRunner.h"
#include "agent/ToolRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "ProjectJournal.h"

using namespace lmms;
using namespace lmms::agent;
namespace
{
QJsonObject step(const QString& command, const QJsonObject& arguments = {})
{
	return {{"cmd", command}, {"args", arguments}};
}
CommandResult run(const QJsonArray& steps, bool dryRun = false, int seed = 0)
{
	return ScriptRunner::run({{"steps", steps}}, {}, seed, dryRun);
}
}
class A4CommandsTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }
	void init()
	{
		Engine::getSong()->clearProject();
		Engine::getSong()->setTempo(120);
		Engine::projectJournal()->clearJournal();
	}
	void registry()
	{
		const auto tools = ToolRegistry::tools();
		QSet<QString> names;
		for (const auto& entry : tools)
		{
			const auto tool = entry.toObject();
			const auto name = tool.value("name").toString();
			QVERIFY(!names.contains(name));
			names.insert(name);
			QVERIFY(!name.startsWith("agent.") || name == "agent.runScript");
			QCOMPARE(tool.value("inputSchema").toObject().value("type").toString(), QString("object"));
			QVERIFY(!tool.value("description").toString().isEmpty());
		}
		QVERIFY(names.contains("agent.runScript"));
		QVERIFY(names.contains("midi.addNotes"));
		QVERIFY(!ToolRegistry::commandManual().isEmpty());
		QVERIFY(CommandBus::instance().execute("agent.getContext").ok);
		QVERIFY(CommandBus::instance().execute("agent.commandHelp", {{"name", "midi.addNotes"}}).ok);
	}
	void atomicUndoAndPreview()
	{
		const QJsonArray steps{step("song.setTempo", {{"bpm", 140}}), step("track.create", {{"type", "Instrument"}})};
		auto result = run(steps, true);
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(Engine::getSong()->getTempo(), 120);
		QVERIFY(Engine::getSong()->tracks().empty());
		QVERIFY(!Engine::projectJournal()->canUndo());
		QVERIFY(!result.data.value("diff").toObject().isEmpty());
		result = run(steps);
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		for (const auto& change : result.data.value("diff").toObject().value("changes").toArray())
		{
			const auto field = change.toObject();
			if (field.value("path").toString().endsWith("/@mutedBeforeSolo"))
			{
				QCOMPARE(field.value("after").toString(), QString("0"));
			}
		}
		QCOMPARE(Engine::getSong()->getTempo(), 140);
		QCOMPARE(Engine::getSong()->tracks().size(), std::size_t(1));
		QVERIFY(CommandBus::instance().execute("history.undo").ok);
		QCOMPARE(Engine::getSong()->getTempo(), 120);
		QVERIFY(Engine::getSong()->tracks().empty());
		QVERIFY(!Engine::projectJournal()->canUndo());
		QVERIFY(CommandBus::instance().execute("history.redo").ok);
		QCOMPARE(Engine::getSong()->tracks().size(), std::size_t(1));
	}
	void failureRollsBack()
	{
		for (const auto& failure : QJsonArray{step("does.notExist"),
			QJsonObject{{"assert", QJsonObject{{"expr", false}, {"msg", "stop"}}}},
			step("song.setTempo", {{"bpm", QJsonObject{{"expr", "1 / 0"}}}}),
			step("config.set", {{"group", "app"}, {"key", "foo"}, {"value", "bar"}})})
		{
			const auto result = run({step("track.create", {{"type", "Instrument"}}), failure});
			QVERIFY(!result.ok);
			QVERIFY(Engine::getSong()->tracks().empty());
			QVERIFY(!Engine::projectJournal()->canUndo());
			QVERIFY(!CommandBus::instance().isBatchActive());
			QVERIFY(result.data.value("failedStep").toInt() > 0);
		}
	}
	void expressionsAndControlFlow()
	{
		const auto expression = [](const QString& text) { return QJsonObject{{"expr", text}}; };
		const QJsonArray steps{
			QJsonObject{{"let", "sum"}, {"value", 0}},
			QJsonObject{{"loop", QJsonObject{{"var", "n"}, {"from", 0}, {"to", 4}}},
				{"steps", QJsonArray{QJsonObject{{"let", "sum"}, {"value", expression("$sum + $n")}}}}},
			QJsonObject{{"foreach", QJsonObject{{"var", "n"}, {"in", QJsonArray{2, 3}}}},
				{"steps", QJsonArray{QJsonObject{{"let", "sum"}, {"value", expression("$sum + $n")}}}}},
			QJsonObject{{"if", QJsonObject{{"expr", "$sum == 11"}}},
				{"steps", QJsonArray{step("song.setTempo", {{"bpm", 131}})}},
				{"else", QJsonArray{step("song.setTempo", {{"bpm", 99}})}}},
			QJsonObject{{"assert", QJsonObject{{"expr", "true || $missing"}}}},
			QJsonObject{{"let", "array"}, {"value", QJsonArray{1, 2, 3}}},
			QJsonObject{{"let", "picked"}, {"value", expression("$array[1 + 1]")}},
			QJsonObject{{"random", QJsonObject{{"var", "random"}, {"a", 0}, {"b", 1}}}},
			QJsonObject{{"let", "shuffled"}, {"value", expression("shuffle([1,2,3,4])")}}
		};
		const auto result = run(steps, false, 42);
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(Engine::getSong()->getTempo(), 131);
		const auto variables = result.data.value("vars").toObject();
		QCOMPARE(variables.value("picked").toInt(), 3);
		QCOMPARE(variables.value("sum").toInt(), 11);
		const auto repeated = run(steps, true, 42);
		QVERIFY2(repeated.ok, qPrintable(repeated.errorMessage));
		QCOMPARE(repeated.data.value("vars").toObject(), variables);
	}
	void limitsAndInvalidNodes()
	{
		const QJsonArray invalid{
			QJsonObject{{"loop", QJsonObject{{"var", "n"}, {"from", 0}, {"to", 513}}}, {"steps", QJsonArray{}}},
			QJsonObject{{"loop", QJsonObject{{"var", "n"}, {"from", 0}, {"to", 2}, {"step", 0}}}, {"steps", QJsonArray{}}},
			QJsonObject{{"cmd", "song.setTempo"}, {"assert", QJsonObject{{"expr", true}}}},
			QJsonObject{{"let", "n"}, {"value", QJsonObject{{"expr", "$unknown"}}}},
			step("agent.runScript", {{"script", "four_on_floor"}})
		};
		for (const auto& node : invalid)
		{
			QVERIFY(!run({node}).ok);
			QVERIFY(!Engine::projectJournal()->canUndo());
			QVERIFY(!CommandBus::instance().isBatchActive());
		}
	}
	void builtInComposition()
	{
		for (const auto& name : {"four_on_floor_drums", "pop_chord_progression", "arpeggio_16th", "bassline_root_octave"})
		{
			const auto result = CommandBus::instance().execute("agent.runScript", {{"script", name}});
			QVERIFY2(result.ok, qPrintable(result.errorMessage + " " + result.data.value("location").toString()));
		}
		QCOMPARE(Engine::getSong()->tracks().size(), std::size_t(4));
		QVERIFY(!ScriptRunner::loadBuiltIn("four_on_floor").isEmpty());
		QVERIFY(ScriptRunner::loadBuiltIn("../foo").isEmpty());
	}
	void highLevelAndAllAssets()
	{
		auto& bus = CommandBus::instance();
		const auto composed = bus.execute("compose.chordProgression", {{"progression", "I-V-vi-IV"}, {"bars", 4}});
		QVERIFY2(composed.ok, qPrintable(composed.errorMessage));
		const auto notes = bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}});
		QVERIFY(notes.ok);
		QCOMPARE(notes.data.value("total").toInt(), 12);
		QVERIFY(bus.execute("scale.set", {{"index", 0}, {"type", "major"}, {"root", 0}}).ok);
		for (const auto& name : {"humanize_groove", "scale_snap", "arrange_verse_to_chorus", "mix_gain_staging"})
		{
			const auto result = bus.execute("agent.runScript", {{"script", name}});
			QVERIFY2(result.ok, qPrintable(QString(name) + ": " + result.errorMessage));
		}
		const auto render = bus.execute("agent.runScript", {{"script", "render_preview"}, {"dryRun", true}});
		QVERIFY2(render.ok, qPrintable(render.errorMessage));
		for (const auto& name : {"compose.arpeggio", "compose.drumPattern", "compose.bassline"})
		{
			const auto result = bus.execute(name, {{"bars", 1}});
			QVERIFY2(result.ok, qPrintable(QString(name) + ": " + result.errorMessage));
		}
		QVERIFY(bus.execute("edit.quantize", {{"track", 0}, {"clip", 0}, {"grid", 16}}).ok);
		QVERIFY(bus.execute("edit.transpose", {{"track", 0}, {"clip", 0}, {"semitones", 1}}).ok);
		QVERIFY(bus.execute("edit.humanize", {{"track", 0}, {"clip", 0}, {"seed", 42}}).ok);
		QVERIFY(bus.execute("compose.melodyVariation", {{"track", 0}, {"clip", 0}, {"seed", 42}}).ok);
		QVERIFY(bus.execute("arrange.insertBars", {{"startBar", 2}, {"bars", 1}}).ok);
		QVERIFY(bus.execute("arrange.deleteBars", {{"startBar", 2}, {"endBar", 3}}).ok);
	}
	void nestedCalls()
	{
		const auto result = run({QJsonObject{{"call", QJsonObject{{"script", "four_on_floor"}, {"params", QJsonObject{{"bars", 1}}}}},
			{"let", "section"}}, QJsonObject{{"assert", QJsonObject{{"expr", "$section.track.index == 0"}}}}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(Engine::getSong()->tracks().size(), std::size_t(1));
		QVERIFY(CommandBus::instance().execute("history.undo").ok);
		QVERIFY(Engine::getSong()->tracks().empty());
	}
	void arpeggiatesExistingChordClip()
	{
		auto& bus = CommandBus::instance();
		QVERIFY(bus.execute("compose.chordProgression", {{"bars", 1}, {"progression", "Am7"}}).ok);
		const auto original = bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}}).data;
		const auto result = bus.execute("compose.arpeggio", {{"track", 0}, {"clip", 0}, {"rate", 16}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		const auto notes = bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}}).data.value("notes").toArray();
		QCOMPARE(notes.size(), qsizetype(16));
		QCOMPARE(notes[0].toObject().value("key").toInt(), 57);
		QCOMPARE(notes[1].toObject().value("key").toInt(), 60);
		QCOMPARE(notes[1].toObject().value("position").toInt(), 12);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}}).data, original);
	}
	void arrangementSplitsAndRestoresClips()
	{
		auto& bus = CommandBus::instance();
		QVERIFY(bus.execute("compose.chordProgression", {{"bars", 4}}).ok);
		const auto original = bus.execute("clip.list", {{"track", 0}}).data;
		const auto insertion = bus.execute("arrange.insertBars", {{"startBar", 2}, {"bars", 1}});
		QVERIFY2(insertion.ok, qPrintable(insertion.errorMessage));
		const auto clips = bus.execute("clip.list", {{"track", 0}}).data.value("clips").toArray();
		QCOMPARE(clips.size(), qsizetype(2));
		QCOMPARE(clips[0].toObject().value("length").toInt(), 384);
		QCOMPARE(clips[1].toObject().value("start").toInt(), 576);
		QCOMPARE(clips[1].toObject().value("startTimeOffset").toInt(), -384);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(bus.execute("clip.list", {{"track", 0}}).data, original);
		const auto deletion = bus.execute("arrange.deleteBars", {{"startBar", 1}, {"endBar", 3}});
		QVERIFY2(deletion.ok, qPrintable(deletion.errorMessage));
		const auto remaining = bus.execute("clip.list", {{"track", 0}}).data.value("clips").toArray();
		QCOMPARE(remaining.size(), qsizetype(2));
		QCOMPARE(remaining[1].toObject().value("startTimeOffset").toInt(), -576);
		QCOMPARE(remaining[1].toObject().value("start").toInt(), 192);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(bus.execute("clip.list", {{"track", 0}}).data, original);
	}
	void rendersPreviewScriptAndProtectsExternalBindings()
	{
		QTemporaryDir directory;
		QFile file(directory.filePath("source.wav"));
		QVERIFY(file.open(QIODevice::WriteOnly));
		QDataStream data(&file); data.setByteOrder(QDataStream::LittleEndian);
		file.write("RIFF", 4); data << quint32(36 + 352800); file.write("WAVEfmt ", 8);
		data << quint32(16) << quint16(1) << quint16(1) << quint32(44100) << quint32(88200) << quint16(2) << quint16(16);
		file.write("data", 4); data << quint32(352800);
		for (int frame = 0; frame < 176400; ++frame) { data << qint16(8000 * std::sin(6.283185307179586 * 440 * frame / 44100)); }
		file.close();
		auto& bus = CommandBus::instance();
		QVERIFY(bus.execute("import.sampleToTrack", {{"path", file.fileName()}}).ok);
		QVERIFY(bus.execute("arrange.duplicateSection", {{"startBar", 1}, {"endBar", 2}, {"destinationBar", 0}}).ok);
		QVERIFY(bus.execute("clip.remove", {{"track", 0}, {"clip", 0}}).ok);
		const auto history = bus.execute("history.status").data;
		const auto preview = bus.execute("agent.runScript", {{"script", "render_preview"}, {"vars", QJsonObject{{"end", 192}}}});
		QVERIFY2(preview.ok, qPrintable(preview.errorMessage));
		const auto task = preview.data.value("lastResult").toObject().value("task").toString();
		QVERIFY(!task.isEmpty());
		QTRY_VERIFY_WITH_TIMEOUT(bus.execute("export.status", {{"task", task}}).data.value("status").toString() != "running", 15000);
		const auto status = bus.execute("export.status", {{"task", task}});
		QCOMPARE(status.data.value("status").toString(), QString("completed"));
		const auto path = status.data.value("path").toString();
		QFile rendered(path);
		QVERIFY(rendered.open(QIODevice::ReadOnly));
		QVERIFY(rendered.size() > 1000);
		rendered.close();
		QVERIFY(bus.execute("import.sampleToTrack", {{"path", path}}).ok);
		QVERIFY(bus.execute("sample.getInfo", {{"track", 1}, {"clip", 0}}).data.value("peak").toDouble() > 0.01);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(bus.execute("history.status").data.value("undoDepth"), history.value("undoDepth"));
		QVERIFY(QFile::remove(path));
		auto write = step("song.save", {{"path", directory.filePath("should-not-exist.mmp")}});
		write.insert("let", "bad.name");
		QVERIFY(!run({write}).ok);
		QVERIFY(!QFile::exists(directory.filePath("should-not-exist.mmp")));
	}
};
QTEST_GUILESS_MAIN(A4CommandsTest)
#include "A4CommandsTest.moc"
