#include "SVSTempoSnapshot.h"
#include "SVSModel.h"
#include "Song.h"
#include "Engine.h"
#include "AutomationTrack.h"
#include "AutomationClip.h"
#include "PatternTrack.h"
#include "PatternClip.h"
#include "PatternStore.h"
#include <QDomDocument>
#include <algorithm>
#include <cmath>
namespace lmms::svs {
namespace {
std::vector<TempoSnapshot::Layer> captureLayers(
	const TrackContainer::TrackList& tracks, IntModel& tempo, int pattern = -1)
{
	Track::clipVector clips;
	for (auto* track : tracks)
	{
		if (track->isMuted()
			|| (track->type() != Track::Type::Automation && track->type() != Track::Type::HiddenAutomation
				&& track->type() != Track::Type::Pattern))
			continue;
		if (pattern < 0)
			track->getClipsInRange(clips, 0, MaxSongLength);
		else if (track->numOfClips() > pattern)
			clips.push_back(track->getClip(pattern));
	}
	std::vector<TempoSnapshot::Layer> layers;
	for (auto* clip : clips)
	{
		if (clip->isMuted())
			continue;
		TempoSnapshot::Layer layer;
		layer.start = int(clip->startPosition());
		layer.length = int(clip->length());
		layer.offset = int(clip->startTimeOffset());
		layer.inPattern = clip->isInPattern();
		if (auto* automation = dynamic_cast<AutomationClip*>(clip))
		{
			bool relevant = false;
			for (const auto& object : automation->objects())
				if (object && object.data() == &tempo)
					relevant = true;
			if (!relevant || !automation->hasAutomation())
				continue;
			// saveSettings provides the existing clip mutex; only its numeric nodes are retained.
			QDomDocument document;
			auto node = document.createElement("automationclip");
			automation->saveSettings(document, node);
			layer.progression = node.attribute("prog").toInt();
			layer.tension = node.attribute("tens").toFloat();
			for (auto point = node.firstChildElement("time"); !point.isNull(); point = point.nextSiblingElement("time"))
				layer.nodes.push_back({point.attribute("pos").toInt(), point.attribute("value").toFloat(),
					point.attribute("outValue").toFloat(), point.attribute("inTan").toFloat(),
					point.attribute("outTan").toFloat()});
		}
		else if (dynamic_cast<PatternClip*>(clip) && pattern < 0)
		{
			const auto index = static_cast<PatternTrack*>(clip->getTrack())->patternIndex();
			layer.patternPeriod = Engine::patternStore()->lengthOfPattern(index) * TimePos::ticksPerBar();
			layer.patternBase = index * TimePos::ticksPerBar();
			if (layer.patternPeriod <= 0)
				continue;
			layer.children = captureLayers(Engine::patternStore()->tracks(), tempo, index);
			if (layer.children.empty())
				continue;
		}
		else
			continue;
		layers.push_back(std::move(layer));
	}
	return layers;
}
float valueAt(const TempoSnapshot::Layer& layer, int tick)
{
	const auto& nodes = layer.nodes;
	auto next = std::lower_bound(
		nodes.begin(), nodes.end(), tick, [](const auto& node, int time) { return node.tick < time; });
	if (next != nodes.end() && next->tick == tick)
		return next->in;
	if (next == nodes.begin())
		return 0;
	const auto& previous = *std::prev(next);
	if (next == nodes.end() || layer.progression == 0)
		return previous.out;
	const int offset = tick - previous.tick, span = next->tick - previous.tick;
	if (layer.progression == 1)
	{
		const float slope = (next->in - previous.out) / span;
		return previous.out + offset * slope;
	}
	const float t = float(offset) / span, t2 = t * t, t3 = t2 * t, m1 = previous.outTangent * span * layer.tension,
				m2 = next->inTangent * span * layer.tension;
	return (2 * t3 - 3 * t2 + 1) * previous.out + (t3 - 2 * t2 + t) * m1 + (-2 * t3 + 3 * t2) * next->in
		+ (t3 - t2) * m2;
}
std::optional<float> evaluate(const std::vector<TempoSnapshot::Layer>& layers, int tick)
{
	std::optional<float> result;
	for (const auto& layer : layers)
	{
		if (tick < layer.start)
			continue;
		if (layer.patternPeriod > 0)
		{
			const auto child = evaluate(
				layer.children, layer.patternBase + std::min(tick - layer.start, layer.length) % layer.patternPeriod);
			if (child)
				result = child;
		}
		else if (!layer.nodes.empty())
		{
			int local = tick - layer.start - layer.offset;
			if (!layer.inPattern)
				local = std::min(local, layer.length - layer.offset);
			result = valueAt(layer, local);
		}
	}
	return result;
}
QJsonArray serialize(const std::vector<TempoSnapshot::Layer>& layers)
{
	QJsonArray array;
	for (const auto& layer : layers)
	{
		QJsonArray nodes;
		for (const auto& node : layer.nodes)
			nodes.append(QJsonObject{{"tick", node.tick}, {"in", node.in}, {"out", node.out},
				{"inTangent", node.inTangent}, {"outTangent", node.outTangent}});
		array.append(QJsonObject{{"start", layer.start}, {"length", layer.length}, {"offset", layer.offset},
			{"progression", layer.progression}, {"tension", layer.tension}, {"inPattern", layer.inPattern},
			{"patternPeriod", layer.patternPeriod}, {"patternBase", layer.patternBase}, {"nodes", nodes},
			{"children", serialize(layer.children)}});
	}
	return array;
}
}
std::shared_ptr<const TempoSnapshot> TempoSnapshot::capture(Song& song, int baseTempo)
{
	auto snapshot = std::make_shared<TempoSnapshot>();
	snapshot->baseTempo = baseTempo;
	snapshot->minimum = song.tempoModel().minValue();
	snapshot->maximum = song.tempoModel().maxValue();
	auto tracks = TrackContainer::TrackList{song.globalAutomationTrack()};
	tracks.insert(tracks.end(), song.tracks().begin(), song.tracks().end());
	snapshot->layers = captureLayers(tracks, song.tempoModel());
	return snapshot;
}
std::optional<int> TempoSnapshot::automatedTempoAt(int tick) const
{
	const auto value = evaluate(layers, tick);
	return value && std::isfinite(*value) ? std::optional<int>{int(std::clamp(*value, float(minimum), float(maximum)))}
										  : std::nullopt;
}
int TempoSnapshot::tempoAt(int tick) const
{
	return automatedTempoAt(tick).value_or(std::clamp(baseTempo, minimum, maximum));
}
double TempoSnapshot::tickAfterSeconds(double tick, double seconds) const
{
	if (!std::isfinite(tick) || !std::isfinite(seconds))
		return tick;
	if (layers.empty())
		return tick + seconds * tempoAt(0) * 48 / 60.;
	const int direction = seconds < 0 ? -1 : 1;
	double remaining = std::abs(seconds);
	// Visit only the interval needed for the editable minimum/lead constraint;
	// never build a whole-song map on the GUI thread.
	while (remaining > 0)
	{
		if ((tick <= 0 && direction < 0) || (tick >= MaxSongLength && direction > 0))
			return tick + direction * remaining * tempoAt(tick <= 0 ? 0 : MaxSongLength) * 48 / 60.;
		const double boundary = direction > 0 ? std::floor(tick) + 1 : std::ceil(tick) - 1;
		const int segment
			= int(std::clamp(direction > 0 ? std::floor(tick) : std::ceil(tick) - 1, 0., double(MaxSongLength)));
		const double secondsPerTick = 60. / (tempoAt(segment) * 48), span = std::abs(boundary - tick),
					 available = span * secondsPerTick;
		if (remaining <= available)
			return tick + direction * remaining / secondsPerTick;
		remaining -= available;
		tick = boundary;
	}
	return tick;
}
QJsonObject TempoSnapshot::toJson() const
{
	return {{"baseTempo", baseTempo}, {"minimum", minimum}, {"maximum", maximum}, {"layers", serialize(layers)}};
}
bool TempoSnapshot::buildMap(
	int lastTick, QJsonArray& points, QString& error, const std::shared_ptr<RenderControl>& control) const
{
	points = {};
	if (lastTick < 0 || lastTick > MaxSongLength)
	{
		error = "SVS tempo snapshot exceeds supported song length";
		return false;
	}
	int previous = -1;
	for (int tick = 0; tick <= lastTick; ++tick)
	{
		if ((tick % 1024) == 0 && control && control->cancelled)
		{
			error = "Cancelled";
			return false;
		}
		const auto current = tempoAt(tick);
		if (current <= 0)
		{
			error = "Invalid SVS tempo snapshot";
			return false;
		}
		if (current != previous)
		{
			points.append(QJsonObject{{"tick", tick}, {"secondsPerTick", 60. / (current * 48)}});
			previous = current;
		}
	}
	return true;
}
}
