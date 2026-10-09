#include "SVSSegmentedSynthesis.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

#include "SVSCache.h"
#include "SVSComputePolicy.h"
#include "SVSCurve.h"
namespace lmms::svs {
bool supportsSegmentedSynthesis(const Input& input)
{
	const auto declaration = input.document["capabilities"].toObject()["synthesis"].toObject()["segmented"].toObject();
	return declaration["split"].toString() == "rests" && declaration["version"].toInt() == 1;
}
QVector<SynthesisSegment> planSynthesisSegments(const Input& input, const TimeMapping& mapping, QString& error)
{
	error.clear();
	QVector<SynthesisSegment> result;
	if (!supportsSegmentedSynthesis(input))
		return result;
	const auto declaration = input.document["capabilities"].toObject()["synthesis"].toObject()["segmented"].toObject();
	const double padding = declaration["paddingSeconds"].toDouble(NAN);
	if (!std::isfinite(padding) || padding < 0 || padding > 2)
	{
		error = "Invalid SVS segment context padding";
		return {};
	}
	QVector<QVector<Note>> groups;
	auto notes = input.notes;
	std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
	double end = -INFINITY;
	bool rest = true;
	for (const auto& note : notes)
	{
		if (note.lyric.trimmed().isEmpty() && note.pronunciation.trimmed().isEmpty()
			&& !note.phonemes.contains("symbols") && note.tick >= end - 1e-7)
		{
			rest = true;
			continue;
		}
		if (rest || note.tick > end + 1e-7)
		{
			groups.append(QVector<Note>{});
			end = -INFINITY;
		}
		groups.back().append(note);
		end = std::max(end, note.tick + note.duration);
		rest = false;
	}
	if (groups.isEmpty())
		groups.append(notes);
	if (groups.size() > 10000)
	{
		error = "SVS segment count exceeds bound";
		return {};
	}
	const auto sourceCurves = input.document["curves"].toObject();
	QVector<double> begins, ends;
	for (const auto& group : groups)
	{
		double first = group.isEmpty() ? 0 : group.front().tick, last = first;
		for (const auto& note : group)
			last = std::max(last, note.tick + note.duration);
		begins.append(mapping.globalSeconds(first));
		ends.append(mapping.globalSeconds(last));
		SynthesisSegment segment;
		segment.input = input;
		segment.input.notes = group;
		segment.input.document["contentEndTick"] = last;
		segment.input.document["segmentedRenderingVersion"] = "svs.rests.v1";
		segment.input.duration = std::max(0., mapping.localSeconds(last));
		const double contextFirst = mapping.resultStartTick(begins.back() - padding),
					 contextLast = mapping.resultStartTick(ends.back() + padding);
		if (input.document.contains("tempoMap"))
		{
			QJsonArray points;
			const double lastProject = std::max(mapping.projectTick(contextLast), mapping.projectTick(0));
			for (const auto& point : input.document["tempoMap"].toArray())
				if (point.toObject()["tick"].toDouble() <= lastProject)
					points.append(point);
			segment.input.document["tempoMap"] = points;
		}
		QJsonObject curves;
		for (auto it = sourceCurves.begin(); it != sourceCurves.end(); ++it)
		{
			Curve curve;
			if (!Curve::fromJson(it.value().toObject(), curve, error))
				return {};
			// Segment inputs retain content-local ticks. Rebasing and adding the
			// origin back can collapse adjacent discontinuity anchors through rounding.
			auto local = curve.slice(contextFirst, contextLast, false);
			curves[it.key()] = local.toJson();
		}
		segment.input.document["curves"] = curves;
		if (input.document.contains("pitchPredictionRequests"))
		{
			const auto requests = input.document["pitchPredictionRequests"].toObject();
			QJsonObject localRequests;
			for (const auto& note : group)
			{
				if (requests.contains(note.id))
				{
					localRequests[note.id] = requests[note.id];
				}
			}
			segment.input.document.remove("pitchPredictionRequests");
			if (!localRequests.isEmpty())
			{
				segment.input.document["pitchPredictionRequests"] = localRequests;
			}
		}
		segment.signature = Cache::editableKey(segment.input);
		result.append(std::move(segment));
	}
	for (int index = 0; index < result.size(); ++index)
	{
		result[index].startSeconds = index ? (ends[index - 1] + begins[index]) * .5 : begins[index] - padding;
		result[index].endSeconds
			= index + 1 < result.size() ? (ends[index] + begins[index + 1]) * .5 : ends[index] + padding;
	}
	return result;
}
std::shared_ptr<const Audio> assembleSynthesisSegments(const Input& input, const TimeMapping& mapping,
													   const QVector<SynthesisSegment>& segments, QString& error,
													   bool complete)
{
	error.clear();
	if (segments.isEmpty())
		return {};
	bool ready = false;
	for (const auto& segment : segments)
		ready |= bool(segment.audio);
	if (!ready)
		return {};
	if (segments.size() == 1 && segments.front().audio)
	{
		auto audio = std::make_shared<Audio>(*segments.front().audio);
		audio->revision = input.revision;
		audio->complete = complete;
		if (segments.front().cached)
		{
			markComputeCacheHit(audio->feedback);
		}
		return audio;
	}
	const double begin = segments.front().startSeconds, end = segments.back().endSeconds;
	const double frames = std::ceil((end - begin) * input.rate);
	if (!std::isfinite(frames) || frames < 0 || frames > 16 * 1024 * 1024)
	{
		error = "Segmented SVS PCM extent exceeds bound";
		return {};
	}
	auto audio = std::make_shared<Audio>();
	audio->rate = input.rate;
	audio->revision = input.revision;
	audio->complete = complete;
	audio->mapping = mapping;
	audio->startSeconds = begin;
	audio->startTick = mapping.resultStartTick(begin);
	audio->samples.resize(size_t(frames) * 2, 0);
	QJsonArray states, pitch, phonemes, diagnostics, computeStages;
	QJsonObject curves, pronunciations;
	for (int index = 0; index < segments.size(); ++index)
	{
		const auto& segment = segments[index];
		states.append(QJsonObject{{"current", index + 1},
								  {"total", segments.size()},
								  {"ready", bool(segment.audio)},
								  {"cached", segment.cached},
								  {"signature", segment.signature}});
		if (!segment.audio)
			continue;
		const auto& part = *segment.audio;
		auto computeFeedback = part.feedback;
		if (segment.cached)
		{
			markComputeCacheHit(computeFeedback);
		}
		for (const auto& value : computeFeedback["computeStages"].toArray())
		{
			computeStages.append(value);
		}
		if (computeFeedback["computeExecution"].isObject())
		{
			computeStages.append(computeFeedback["computeExecution"]);
		}
		if (part.rate != input.rate)
		{
			error = "SVS segment sample rate mismatch";
			return {};
		}
		const qint64 offset = std::llround((part.startSeconds - begin) * input.rate),
					 first = std::max<qint64>(0, std::llround((segment.startSeconds - begin) * input.rate)),
					 last = std::min<qint64>(qint64(frames), std::llround((segment.endSeconds - begin) * input.rate));
		for (qint64 frame = first; frame < last; ++frame)
		{
			const auto source = frame - offset;
			if (source < 0 || source >= qint64(part.samples.size() / 2))
				continue;
			audio->samples[size_t(frame) * 2] = part.samples[size_t(source) * 2];
			audio->samples[size_t(frame) * 2 + 1] = part.samples[size_t(source) * 2 + 1];
		}
		for (const auto& value : part.feedback["phonemes"].toArray())
			phonemes.append(value);
		for (const auto& value : part.feedback["dictionaryDiagnostics"].toArray())
			diagnostics.append(value);
		for (const auto& value : part.feedback["pitch"].toArray())
		{
			const double time = mapping.globalSeconds(0) + value.toObject()["startSeconds"].toDouble();
			if (time >= segment.startSeconds && time < segment.endSeconds)
				pitch.append(value);
		}
		const auto readings = part.feedback["pronunciations"].toObject();
		for (auto it = readings.begin(); it != readings.end(); ++it)
			pronunciations[it.key()] = it.value();
		const auto references = part.feedback["curves"].toObject();
		for (auto it = references.begin(); it != references.end(); ++it)
		{
			Curve reference;
			if (!Curve::fromJson(it.value().toObject(), reference, error))
				return {};
			const double leftTick = mapping.resultStartTick(segment.startSeconds),
						 rightTick = mapping.resultStartTick(segment.endSeconds);
			auto sliced = reference.slice(leftTick, rightTick);
			for (auto& point : sliced.evaluator.points)
				point.tick += leftTick;
			for (auto& gap : sliced.evaluator.gaps)
			{
				gap.start += leftTick;
				gap.end += leftTick;
			}
			const auto next = sliced.toJson();
			auto merged = curves.value(it.key()).toObject();
			if (merged.isEmpty())
			{
				curves[it.key()] = next;
				continue;
			}
			auto points = merged["points"].toArray();
			const auto appended = next["points"].toArray();
			auto gaps = merged["gaps"].toArray();
			if (!points.isEmpty() && !appended.isEmpty())
			{
				const auto left = points.last().toObject()["tick"].toDouble(),
						   right = appended.first().toObject()["tick"].toDouble();
				if (right > left)
					gaps.append(QJsonObject{{"start", left}, {"end", right}});
			}
			for (const auto& value : appended)
			{
				if (points.isEmpty()
					|| value.toObject()["tick"].toDouble() > points.last().toObject()["tick"].toDouble())
					points.append(value);
			}
			for (const auto& value : next["gaps"].toArray())
				gaps.append(value);
			merged["points"] = points;
			merged["gaps"] = gaps;
			curves[it.key()] = merged;
		}
	}
	audio->feedback = {{"segments", states},
					   {"pitch", pitch},
					   {"phonemes", phonemes},
					   {"pronunciations", pronunciations},
					   {"dictionaryDiagnostics", diagnostics},
					   {"curves", curves}};
	if (!computeStages.isEmpty())
	{
		audio->feedback["computeStages"] = computeStages;
	}
	audio->waveform.build(audio->samples);
	return audio;
}
} // namespace lmms::svs
