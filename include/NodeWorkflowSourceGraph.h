#ifndef LMMS_NODE_WORKFLOW_SOURCE_GRAPH_H
#define LMMS_NODE_WORKFLOW_SOURCE_GRAPH_H

#include <QDomDocument>
#include <QSet>

#include <array>
#include <optional>

namespace lmms
{

enum class NodeWorkflowSourceKind { Instrument, SingingVoice, Sample };

inline std::array<std::optional<unsigned>, 2> sourceAudioOutputs(const QDomElement& subgraph)
{
	std::array<std::optional<unsigned>, 2> outputs{};
	for (auto edge = subgraph.firstChildElement("connection"); !edge.isNull();
		edge = edge.nextSiblingElement("connection"))
	{
		const auto from = edge.attribute("from");
		const auto to = edge.attribute("to");
		if ((from == "source:audio-out:L" || from == "source:audio-out:R")
			&& (to == "output:audio-in:L" || to == "output:audio-in:R"))
		{
			outputs[from.endsWith(":L") ? 0 : 1] = to.endsWith(":L") ? 0 : 1;
		}
	}
	return outputs;
}

inline void setSourceAudioOutput(QDomElement subgraph, unsigned source, std::optional<unsigned> destination)
{
	const auto from = source == 0 ? QStringLiteral("source:audio-out:L") : QStringLiteral("source:audio-out:R");
	auto edge = subgraph.firstChildElement("connection");
	while (!edge.isNull())
	{
		const auto next = edge.nextSiblingElement("connection");
		if (edge.attribute("from") == from)
		{
			subgraph.removeChild(edge);
		}
		edge = next;
	}
	if (destination)
	{
		auto replacement = subgraph.ownerDocument().createElement("connection");
		replacement.setAttribute("from", from);
		replacement.setAttribute("to", *destination == 0 ? "output:audio-in:L" : "output:audio-in:R");
		subgraph.appendChild(replacement);
	}
}

inline bool sourceHasConnection(const QDomElement& subgraph, const QString& from, const QString& to)
{
	for (auto connection = subgraph.firstChildElement("connection"); !connection.isNull();
		connection = connection.nextSiblingElement("connection"))
	{
		if (connection.attribute("from") == from && connection.attribute("to") == to)
		{
			return true;
		}
	}
	return false;
}

inline void setSourceConnection(QDomElement subgraph, const QString& from, const QString& to, bool connected)
{
	if (connected)
	{
		if (!sourceHasConnection(subgraph, from, to))
		{
			auto edge = subgraph.ownerDocument().createElement("connection");
			edge.setAttribute("from", from);
			edge.setAttribute("to", to);
			subgraph.appendChild(edge);
		}
		return;
	}

	auto edge = subgraph.firstChildElement("connection");
	while (!edge.isNull())
	{
		const auto next = edge.nextSiblingElement("connection");
		if (edge.attribute("from") == from && edge.attribute("to") == to)
		{
			subgraph.removeChild(edge);
		}
		edge = next;
	}
}

inline bool sourceEventConnected(const QDomElement& subgraph, NodeWorkflowSourceKind kind)
{
	if (kind == NodeWorkflowSourceKind::SingingVoice)
	{
		return sourceHasConnection(subgraph, "pianoroll:voice-out", "source:voice-in");
	}
	return sourceHasConnection(subgraph, "pianoroll:midi-out", "source:midi-in");
}

inline void setSourceEventConnected(QDomElement subgraph, NodeWorkflowSourceKind kind, bool connected)
{
	if (kind == NodeWorkflowSourceKind::SingingVoice)
	{
		setSourceConnection(subgraph, "pianoroll:voice-out", "source:voice-in", connected);
	}
	else
	{
		setSourceConnection(subgraph, "pianoroll:midi-out", "source:midi-in", connected);
	}
}

inline bool sourceMidiInputConnected(const QDomElement& subgraph)
{
	return subgraph.isNull() || sourceHasConnection(subgraph, "input:midi-out", "pianoroll:midi-in");
}

inline void setSourceMidiInputConnected(QDomElement subgraph, bool connected)
{
	setSourceConnection(subgraph, "input:midi-out", "pianoroll:midi-in", connected);
}

inline void saveDefaultSourceSubgraph(QDomElement workflow, const QString& trackId,
	NodeWorkflowSourceKind kind)
{
	for (auto graph = workflow.firstChildElement("subgraph"); !graph.isNull();
		graph = graph.nextSiblingElement("subgraph"))
	{
		if (graph.attribute("track") == trackId)
		{
			return;
		}
	}

	auto document = workflow.ownerDocument();
	auto graph = document.createElement("subgraph");
	graph.setAttribute("track", trackId);
	workflow.appendChild(graph);

	const auto addNode = [&](const QString& id, const QString& type)
	{
		auto node = document.createElement("node");
		node.setAttribute("id", id);
		node.setAttribute("type", type);
		graph.appendChild(node);
	};
	const auto addConnection = [&](const QString& from, const QString& to)
	{
		auto connection = document.createElement("connection");
		connection.setAttribute("from", from);
		connection.setAttribute("to", to);
		graph.appendChild(connection);
	};

	const bool singingVoice = kind == NodeWorkflowSourceKind::SingingVoice;
	if (kind == NodeWorkflowSourceKind::Sample)
	{
		addNode("input", "audio-input");
		addNode("source", "sample-source");
		addNode("output", "audio-output");
		addConnection("source:audio-out:L", "output:audio-in:L");
		addConnection("source:audio-out:R", "output:audio-in:R");
		return;
	}
	addNode("input", "midi-input");
	addNode("pianoroll", singingVoice ? "svs-pianoroll" : "midi-pianoroll");
	addNode("source", singingVoice ? "svs-source" : "instrument-source");
	addNode("output", "audio-output");
	addConnection("input:midi-out", "pianoroll:midi-in");
	if (singingVoice)
	{
		addConnection("pianoroll:voice-out", "source:voice-in");
	}
	else
	{
		addConnection("pianoroll:midi-out", "source:midi-in");
	}
	addConnection("source:audio-out:L", "output:audio-in:L");
	addConnection("source:audio-out:R", "output:audio-in:R");
}

inline void removeUnownedTrackGraphData(QDomElement workflow, const QDomElement& project)
{
	if (workflow.isNull())
	{
		return;
	}

	QSet<QString> trackIds;
	const auto tracks = project.elementsByTagName("track");
	for (qsizetype index = 0; index < tracks.count(); ++index)
	{
		const auto id = tracks.item(index).toElement().attribute("nodeid");
		if (!id.isEmpty())
		{
			trackIds.insert(id);
		}
	}

	auto removeUnowned = [&](QDomElement parent, const QString& tag)
	{
		auto child = parent.firstChildElement(tag);
		while (!child.isNull())
		{
			const auto next = child.nextSiblingElement(tag);
			if (!trackIds.contains(child.attribute("track")))
			{
				parent.removeChild(child);
			}
			child = next;
		}
	};
	removeUnowned(workflow, "subgraph");
	removeUnowned(workflow, "track-audio-route");
	removeUnowned(workflow, "track-boundary-disconnect");
	removeUnowned(workflow, "track-position");
	for (auto port = workflow.firstChildElement("boundary-port"); !port.isNull();)
	{
		const auto next = port.nextSiblingElement("boundary-port");
		const auto graph = port.attribute("graph");
		if (graph.startsWith("source/") && !trackIds.contains(graph.section('/', 1, 1)))
		{
			workflow.removeChild(port);
		}
		port = next;
	}
	for (auto input = workflow.firstChildElement("aggregate-input"); !input.isNull();
		input = input.nextSiblingElement("aggregate-input"))
	{
		removeUnowned(input, "destination");
	}
}

} // namespace lmms

#endif
