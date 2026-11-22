#ifndef LMMS_NODE_WORKFLOW_INPUTS_H
#define LMMS_NODE_WORKFLOW_INPUTS_H

#include <QDomDocument>
#include <QList>
#include <QSet>

#include <algorithm>

#include "NodeWorkflowSourceGraph.h"

namespace lmms
{

inline bool setNodeInputDestination(QDomElement workflow, const QString& inputId,
	const QString& signalType, const QString& trackId, const QString& port, bool connected)
{
	auto input = workflow.firstChildElement("aggregate-input");
	while (!input.isNull() && input.attribute("id") != inputId)
	{
		input = input.nextSiblingElement("aggregate-input");
	}
	if (!input.isNull() && input.attribute("signal") != signalType)
	{
		return false;
	}

	bool present = false;
	auto edge = input.firstChildElement("destination");
	while (!edge.isNull())
	{
		const auto next = edge.nextSiblingElement("destination");
		if (edge.attribute("track") == trackId && edge.attribute("port") == port)
		{
			if (!connected || present)
			{
				input.removeChild(edge);
			}
			present = true;
		}
		edge = next;
	}
	if (connected && !present)
	{
		auto document = workflow.ownerDocument();
		if (input.isNull())
		{
			input = document.createElement("aggregate-input");
			input.setAttribute("id", inputId);
			input.setAttribute("signal", signalType);
			workflow.appendChild(input);
		}
		edge = document.createElement("destination");
		edge.setAttribute("track", trackId);
		edge.setAttribute("port", port);
		input.appendChild(edge);
	}
	return true;
}

inline QList<QDomElement> nodeInputDestinations(const QDomElement& workflow,
	const QString& inputId, const QString& signalType)
{
	QList<QDomElement> destinations;
	for (auto input = workflow.firstChildElement("aggregate-input"); !input.isNull();
		input = input.nextSiblingElement("aggregate-input"))
	{
		if (input.attribute("id") != inputId || input.attribute("signal") != signalType)
		{
			continue;
		}

		for (auto edge = input.firstChildElement("destination"); !edge.isNull();
			edge = edge.nextSiblingElement("destination"))
		{
			const auto track = edge.attribute("track");
			const auto port = edge.attribute("port");
			if (track.isEmpty() || port.isEmpty())
			{
				continue;
			}

			const auto duplicate = std::ranges::any_of(destinations, [&](const auto& previous)
			{
				return previous.attribute("track") == track && previous.attribute("port") == port;
			});
			if (!duplicate)
			{
				destinations.append(edge);
			}
		}
		break;
	}
	return destinations;
}

inline QDomElement findNodeMidiBoundary(const QDomElement& workflow, const QString& trackId, const QString& port)
{
	if (!port.startsWith("midi-in/"))
	{
		return {};
	}
	const auto scope = QStringLiteral("source/") + trackId + '/';
	const auto id = port.mid(8);
	for (auto boundary = workflow.firstChildElement("boundary-port"); !boundary.isNull();
		boundary = boundary.nextSiblingElement("boundary-port"))
	{
		if (boundary.attribute("id") == id && boundary.attribute("graph") == scope
			&& boundary.attribute("direction") == "in" && boundary.attribute("signal") == "midi")
		{
			return boundary;
		}
	}
	return {};
}

inline QSet<QString> nodeInstrumentMidiTargets(const QDomElement& workflow, const QString& inputId,
	bool releasingNote)
{
	QSet<QString> targets;
	for (const auto& destination : nodeInputDestinations(workflow, inputId, "midi"))
	{
		const auto track = destination.attribute("track");
		const auto port = destination.attribute("port");
		const auto boundary = findNodeMidiBoundary(workflow, track, port);
		if (port == "midi-in" || (releasingNote && port.startsWith("midi-in/"))
			|| (!boundary.isNull() && boundary.attribute("track") == track))
		{
			targets.insert(track);
		}
	}

	if (releasingNote || targets.isEmpty())
	{
		return targets;
	}

	for (auto graph = workflow.firstChildElement("subgraph"); !graph.isNull();
		graph = graph.nextSiblingElement("subgraph"))
	{
		const auto track = graph.attribute("track");
		if (targets.contains(track) && !sourceMidiInputConnected(graph))
		{
			targets.remove(track);
		}
	}
	return targets;
}

} // namespace lmms

#endif
