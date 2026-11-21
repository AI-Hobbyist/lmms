#include "DataFile.h"
#include "NodeWorkflowInputs.h"
#include "NodeWorkflowSourceGraph.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using lmms::DataFile;

class NodeWorkflowStorageTest : public QObject
{
	Q_OBJECT

private slots:
	void roundTrip_data();
	void roundTrip();
	void ordinaryAndLegacyProjects();
};

void NodeWorkflowStorageTest::roundTrip_data()
{
	QTest::addColumn<QString>("suffix");
	QTest::addColumn<bool>("compressed");
	QTest::newRow("XML") << QStringLiteral(".mmpn") << false;
	QTest::newRow("Compressed") << QStringLiteral(".mmpnz") << true;
	QTest::newRow("Template") << QStringLiteral(".mpt") << false;
	QTest::newRow("Recovery") << QStringLiteral(".mmp") << false;
}

void NodeWorkflowStorageTest::roundTrip()
{
	QFETCH(QString, suffix);
	QFETCH(bool, compressed);
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const auto path = directory.filePath("project" + suffix);

	DataFile project(DataFile::Type::SongProject);
	auto track = project.createElement("track");
	track.setAttribute("name", "Bass");
	const auto trackId = QStringLiteral("329fb9ad-1ffd-47bb-bdba-276157a57b97");
	track.setAttribute("nodeid", trackId);
	auto tracks = project.createElement("trackcontainer");
	tracks.appendChild(track);
	const auto secondInstrumentId = QStringLiteral("299d4abc-1592-475e-851a-2a016dfe99d2");
	auto secondInstrument = project.createElement("track");
	secondInstrument.setAttribute("name", "Bass");
	secondInstrument.setAttribute("nodeid", secondInstrumentId);
	auto patternTrack = project.createElement("track");
	auto patternSettings = project.createElement("patterntrack");
	auto patternStore = project.createElement("patternstore");
	patternStore.appendChild(secondInstrument);
	patternSettings.appendChild(patternStore);
	patternTrack.appendChild(patternSettings);
	tracks.appendChild(patternTrack);
	const QDir presets(qEnvironmentVariable("LMMS_DATA_DIR"));
	const DataFile bell(presets.filePath("presets/TripleOscillator/AnalogBell.xpf"));
	const DataFile dream(presets.filePath("presets/TripleOscillator/AnalogDreamz.xpf"));
	// DataFile exposes the preset content under the same tag used by the track serializer.
	auto instrumentSettings = [](const DataFile& preset)
	{
		return preset.documentElement().firstChildElement("instrumenttracksettings")
			.firstChildElement("instrumenttrack");
	};
	const auto bellSettings = instrumentSettings(bell);
	const auto dreamSettings = instrumentSettings(dream);
	QVERIFY(!bellSettings.isNull());
	QVERIFY(!dreamSettings.isNull());
	track.appendChild(project.importNode(bellSettings, true));
	secondInstrument.appendChild(project.importNode(dreamSettings, true));
	const auto voiceTrackId = QStringLiteral("26aa68c0-663f-4f3e-b4d9-d6c04a20e985");
	auto voiceTrack = project.createElement("track");
	voiceTrack.setAttribute("name", "Bass");
	voiceTrack.setAttribute("nodeid", voiceTrackId);
	tracks.appendChild(voiceTrack);
	project.content().appendChild(tracks);
	project.setNodeWorkflow();
	project.nodeWorkflow().setAttribute("masterOutputMask", 2);
	auto workflow = project.nodeWorkflow();
	lmms::saveDefaultSourceSubgraph(workflow, trackId, lmms::NodeWorkflowSourceKind::Instrument);
	lmms::saveDefaultSourceSubgraph(workflow, secondInstrumentId, lmms::NodeWorkflowSourceKind::Instrument);
	lmms::saveDefaultSourceSubgraph(workflow, voiceTrackId, lmms::NodeWorkflowSourceKind::SingingVoice);
	lmms::saveDefaultSourceSubgraph(workflow, "removed-track", lmms::NodeWorkflowSourceKind::Instrument);
	auto subgraph = workflow.firstChildElement("subgraph");
	subgraph.setAttribute("layout", "saved");
	lmms::saveDefaultSourceSubgraph(workflow, trackId, lmms::NodeWorkflowSourceKind::Instrument);
	const auto mixerId = QStringLiteral("118bb2e9-3361-487e-950f-07ee79ca4f13");
	auto cable = project.createElement("track-audio-route");
	cable.setAttribute("track", trackId);
	cable.setAttribute("side", "R");
	cable.setAttribute("mixer", mixerId);
	cable.setAttribute("input", "L");
	workflow.appendChild(cable);
	auto disconnected = project.createElement("track-audio-route");
	disconnected.setAttribute("track", trackId);
	disconnected.setAttribute("side", "L");
	workflow.appendChild(disconnected);
	auto patternRoute = cable.cloneNode(true).toElement();
	patternRoute.setAttribute("track", secondInstrumentId);
	workflow.appendChild(patternRoute);
	auto addInput = [&](const QString& id, const QString& signal, const QString& target, const QString& port)
	{
		auto input = project.createElement("aggregate-input");
		input.setAttribute("id", id);
		input.setAttribute("signal", signal);
		auto edge = project.createElement("destination");
		edge.setAttribute("track", target);
		edge.setAttribute("port", port);
		input.appendChild(edge);
		input.appendChild(edge.cloneNode(true));
		auto orphan = edge.cloneNode(true).toElement();
		orphan.setAttribute("track", "removed-track");
		input.appendChild(orphan);
		workflow.appendChild(input);
	};
	addInput("midi-1", "midi", trackId, "midi-in");
	addInput("midi-2", "midi", secondInstrumentId, "midi-in");
	addInput("audio-1", "audio", trackId, "audio-in:L");
	auto orphanRoute = cable.cloneNode(true).toElement();
	orphanRoute.setAttribute("track", "removed-track");
	workflow.appendChild(orphanRoute);
	lmms::removeUnownedTrackGraphData(workflow, project.content());
	QVERIFY(project.writeFile(path, false));

	QFile saved(path);
	QVERIFY(saved.open(QIODevice::ReadOnly));
	const auto bytes = saved.readAll();
	QVERIFY((compressed ? qUncompress(bytes) : bytes).startsWith("<?xml"));

	DataFile loaded(path);
	QVERIFY(loaded.isNodeWorkflow());
	const auto loadedTrack = loaded.content().firstChildElement("trackcontainer").firstChildElement("track");
	QCOMPARE(loadedTrack.attribute("name"), QStringLiteral("Bass"));
	QCOMPARE(loadedTrack.attribute("nodeid"), trackId);
	auto loadedWorkflow = loaded.nodeWorkflow();
	QCOMPARE(loadedWorkflow.attribute("masterOutputMask").toUInt(), 2u);
	const auto firstMidi = lmms::nodeInputDestinations(loadedWorkflow, "midi-1", "midi");
	const auto secondMidi = lmms::nodeInputDestinations(loadedWorkflow, "midi-2", "midi");
	QCOMPARE(firstMidi.size(), qsizetype{1});
	QCOMPARE(secondMidi.size(), qsizetype{1});
	QCOMPARE(firstMidi.front().attribute("track"), trackId);
	QCOMPARE(secondMidi.front().attribute("track"), secondInstrumentId);
	QVERIFY(lmms::nodeInputDestinations(loadedWorkflow, "midi-1", "audio").isEmpty());
	QVERIFY(!lmms::setNodeInputDestination(loadedWorkflow, "midi-1", "audio", trackId, "audio-in:L", true));
	QVERIFY(lmms::setNodeInputDestination(loadedWorkflow, "midi-1", "midi", trackId, "midi-in", true));
	QCOMPARE(firstMidi.front().parentNode().toElement().elementsByTagName("destination").count(), qsizetype{1});
	QVERIFY(lmms::setNodeInputDestination(loadedWorkflow, "midi-1", "midi", trackId, "midi-in", false));
	QVERIFY(lmms::nodeInputDestinations(loadedWorkflow, "midi-1", "midi").isEmpty());
	QCOMPARE(lmms::nodeInputDestinations(loadedWorkflow, "midi-2", "midi").size(), qsizetype{1});
	const auto audio = lmms::nodeInputDestinations(loadedWorkflow, "audio-1", "audio");
	QCOMPARE(audio.size(), qsizetype{1});
	QCOMPARE(audio.front().attribute("port"), QStringLiteral("audio-in:L"));
	QCOMPARE(loadedWorkflow.attribute("version"), QStringLiteral("1"));
	const auto loadedSubgraph = loadedWorkflow.firstChildElement("subgraph");
	QCOMPARE(loadedSubgraph.attribute("track"), loadedTrack.attribute("nodeid"));
	QCOMPARE(loadedSubgraph.attribute("layout"), QStringLiteral("saved"));
	QCOMPARE(loadedSubgraph.elementsByTagName("node").count(), qsizetype{4});
	const auto instrumentCables = loadedSubgraph.elementsByTagName("connection");
	QCOMPARE(instrumentCables.count(), qsizetype{4});
	QCOMPARE(instrumentCables.item(1).toElement().attribute("from"),
		QStringLiteral("pianoroll:midi-out"));
	QVERIFY(lmms::sourceEventConnected(loadedSubgraph, lmms::NodeWorkflowSourceKind::Instrument));
	QVERIFY(!lmms::sourceEventConnected(loadedSubgraph, lmms::NodeWorkflowSourceKind::SingingVoice));
	QCOMPARE(instrumentCables.item(2).toElement().attribute("to"), QStringLiteral("output:audio-in:L"));
	QCOMPARE(instrumentCables.item(3).toElement().attribute("to"), QStringLiteral("output:audio-in:R"));
	const auto defaultOutputs = lmms::sourceAudioOutputs(loadedSubgraph);
	QVERIFY(defaultOutputs[0] == 0);
	QVERIFY(defaultOutputs[1] == 1);
	auto splitSource = loadedSubgraph.cloneNode(true).toElement();
	lmms::setSourceAudioOutput(splitSource, 1, 0);
	lmms::setSourceAudioOutput(splitSource, 0, std::nullopt);
	const auto splitOutputs = lmms::sourceAudioOutputs(splitSource);
	QVERIFY(!splitOutputs[0].has_value());
	QVERIFY(splitOutputs[1] == 0);
	QVERIFY(lmms::sourceEventConnected(splitSource, lmms::NodeWorkflowSourceKind::Instrument));
	const auto loadedPattern = loadedTrack.nextSiblingElement("track");
	const auto loadedSecondInstrument = loadedPattern.firstChildElement("patterntrack")
		.firstChildElement("patternstore").firstChildElement("track");
	const auto secondGraph = loadedSubgraph.nextSiblingElement("subgraph");
	QCOMPARE(secondGraph.attribute("track"), loadedSecondInstrument.attribute("nodeid"));
	auto savedPlugin = [](const QDomElement& trackElement)
	{
		return trackElement.firstChildElement("instrumenttrack").firstChildElement("instrument");
	};
	const auto firstPlugin = savedPlugin(loadedTrack);
	const auto secondPlugin = savedPlugin(loadedSecondInstrument);
	QCOMPARE(firstPlugin.attribute("name"), QStringLiteral("tripleoscillator"));
	QCOMPARE(secondPlugin.attribute("name"), firstPlugin.attribute("name"));
	QCOMPARE(firstPlugin.firstChildElement("tripleoscillator").attribute("vol0"), QStringLiteral("49"));
	QCOMPARE(secondPlugin.firstChildElement("tripleoscillator").attribute("vol0"), QStringLiteral("25"));
	const auto voiceGraph = secondGraph.nextSiblingElement("subgraph");
	QCOMPARE(voiceGraph.attribute("track"),
		loadedPattern.nextSiblingElement("track").attribute("nodeid"));
	QCOMPARE(voiceGraph.elementsByTagName("connection").item(1).toElement().attribute("to"),
		QStringLiteral("source:voice-in"));
	QVERIFY(lmms::sourceEventConnected(voiceGraph, lmms::NodeWorkflowSourceKind::SingingVoice));
	lmms::setSourceEventConnected(voiceGraph, lmms::NodeWorkflowSourceKind::SingingVoice, false);
	QVERIFY(!lmms::sourceEventConnected(voiceGraph, lmms::NodeWorkflowSourceKind::SingingVoice));
	QCOMPARE(voiceGraph.elementsByTagName("connection").count(), qsizetype{3});
	lmms::setSourceEventConnected(voiceGraph, lmms::NodeWorkflowSourceKind::SingingVoice, true);
	lmms::setSourceEventConnected(voiceGraph, lmms::NodeWorkflowSourceKind::SingingVoice, true);
	QVERIFY(lmms::sourceEventConnected(voiceGraph, lmms::NodeWorkflowSourceKind::SingingVoice));
	QCOMPARE(voiceGraph.elementsByTagName("connection").count(), qsizetype{4});
	QVERIFY(voiceGraph.nextSiblingElement("subgraph").isNull());
	const auto loadedRoute = loadedWorkflow.firstChildElement("track-audio-route");
	QCOMPARE(loadedRoute.attribute("track"), trackId);
	QCOMPARE(loadedRoute.attribute("side"), QStringLiteral("R"));
	QCOMPARE(loadedRoute.attribute("mixer"), mixerId);
	QCOMPARE(loadedRoute.attribute("input"), QStringLiteral("L"));
	const auto loadedDisconnect = loadedRoute.nextSiblingElement("track-audio-route");
	QCOMPARE(loadedDisconnect.attribute("track"), trackId);
	QCOMPARE(loadedDisconnect.attribute("side"), QStringLiteral("L"));
	QVERIFY(!loadedDisconnect.hasAttribute("mixer"));
	const auto loadedPatternRoute = loadedDisconnect.nextSiblingElement("track-audio-route");
	QCOMPARE(loadedPatternRoute.attribute("track"), secondInstrumentId);
	QCOMPARE(loadedPatternRoute.attribute("mixer"), mixerId);
	QVERIFY(loadedPatternRoute.nextSiblingElement("track-audio-route").isNull());

	DataFile savedAgain(DataFile::Type::SongProject);
	savedAgain.setNodeWorkflow(loadedWorkflow);
	QCOMPARE(savedAgain.nodeWorkflow().childNodes().count(), loadedWorkflow.childNodes().count());
	const auto savedDisconnect = savedAgain.nodeWorkflow().firstChildElement("track-audio-route")
		.nextSiblingElement("track-audio-route");
	QCOMPARE(savedDisconnect.attribute("side"), QStringLiteral("L"));
	QVERIFY(!savedDisconnect.hasAttribute("mixer"));

	QVERIFY(lmms::sourceMidiInputConnected(splitSource));
	lmms::setSourceMidiInputConnected(splitSource, false);
	QVERIFY(!lmms::sourceMidiInputConnected(splitSource));
	QVERIFY(lmms::sourceEventConnected(splitSource, lmms::NodeWorkflowSourceKind::Instrument));
	lmms::setSourceMidiInputConnected(splitSource, true);
	lmms::setSourceMidiInputConnected(splitSource, true);
	QVERIFY(lmms::sourceMidiInputConnected(splitSource));
	QCOMPARE(splitSource.elementsByTagName("connection").count(), qsizetype{3});
	lmms::setSourceMidiInputConnected(splitSource, false);
	loadedWorkflow.replaceChild(splitSource, loadedSubgraph);
	lmms::setSourceEventConnected(voiceGraph, lmms::NodeWorkflowSourceKind::SingingVoice, false);
	const auto editedPath = directory.filePath("edited" + suffix);
	QVERIFY(loaded.writeFile(editedPath, false));
	DataFile edited(editedPath);
	const auto editedGraph = edited.nodeWorkflow().firstChildElement("subgraph");
	QCOMPARE(editedGraph.attribute("track"), trackId);
	const auto editedOutputs = lmms::sourceAudioOutputs(editedGraph);
	QVERIFY(!editedOutputs[0].has_value());
	QVERIFY(editedOutputs[1] == 0);
	QVERIFY(!lmms::sourceMidiInputConnected(editedGraph));
	QVERIFY(lmms::sourceEventConnected(editedGraph, lmms::NodeWorkflowSourceKind::Instrument));
	const auto editedVoice = editedGraph.nextSiblingElement("subgraph").nextSiblingElement("subgraph");
	QCOMPARE(editedVoice.attribute("track"), voiceTrackId);
	QVERIFY(!lmms::sourceEventConnected(editedVoice, lmms::NodeWorkflowSourceKind::SingingVoice));
	QVERIFY(lmms::nodeInputDestinations(edited.nodeWorkflow(), "midi-1", "midi").isEmpty());
	const auto editedSecondMidi = lmms::nodeInputDestinations(edited.nodeWorkflow(), "midi-2", "midi");
	QCOMPARE(editedSecondMidi.size(), qsizetype{1});
	QCOMPARE(editedSecondMidi.front().attribute("track"), secondInstrumentId);

	auto routing = edited.nodeWorkflow();
	QVERIFY(lmms::setNodeInputDestination(routing, "midi-1", "midi", trackId, "midi-in", true));
	QVERIFY(lmms::nodeInstrumentMidiTargets(routing, "midi-1", false).isEmpty());
	QCOMPARE(lmms::nodeInstrumentMidiTargets(routing, "midi-1", true), QSet<QString>{trackId});
	QCOMPARE(lmms::nodeInstrumentMidiTargets(routing, "midi-2", false), QSet<QString>{secondInstrumentId});
	lmms::setSourceMidiInputConnected(editedGraph, true);
	QCOMPARE(lmms::nodeInstrumentMidiTargets(routing, "midi-1", false), QSet<QString>{trackId});
}

void NodeWorkflowStorageTest::ordinaryAndLegacyProjects()
{
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const auto path = directory.filePath("ordinary.mmp");
	DataFile ordinary(DataFile::Type::SongProject);
	QVERIFY(ordinary.writeFile(path, false));
	DataFile loaded(path);
	QVERIFY(!loaded.head().isNull());
	QVERIFY(!loaded.isNodeWorkflow());
	QVERIFY(loaded.nodeWorkflow().isNull());

	loaded.head().setAttribute("nodeWorkflow", 1);
	QVERIFY(loaded.isNodeWorkflow());
	loaded.setNodeWorkflow();
	QVERIFY(!loaded.head().hasAttribute("nodeWorkflow"));
	QCOMPARE(loaded.nodeWorkflow().attribute("version"), QStringLiteral("1"));
}

QTEST_GUILESS_MAIN(NodeWorkflowStorageTest)
#include "NodeWorkflowStorageTest.moc"
