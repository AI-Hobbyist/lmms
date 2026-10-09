#ifndef LMMS_SVS_STRETCH_OPERATIONS_H
#define LMMS_SVS_STRETCH_OPERATIONS_H
#include "SVSClip.h"
#include "SVSTempoSnapshot.h"
#include <algorithm>
#include <cmath>

namespace lmms::gui::svsedit {
// Shared by the note canvas and the upper half of the phoneme strip.
// The caller supplies a fresh gesture snapshot on every move.
struct StretchLimits
{
	std::shared_ptr<const svs::TempoSnapshot> tempo;
	double origin = 0, minimumSeconds = .005, leadSeconds = 0;
	double shifted(double tick, double seconds) const
	{
		return tempo->tickAfterSeconds(origin + tick, seconds) - origin;
	}
	bool accepts(const QVector<svs::Note>& notes) const
	{
		for (const auto& note : notes)
		{
			double previous = -INFINITY;
			for (const auto& item : note.phonemes["segments"].toArray())
			{
				const auto segment = item.toObject();
				const auto start = note.tick + segment["startTick"].toDouble(),
						   end = start + segment["durationTicks"].toDouble();
				if (!std::isfinite(start) || !std::isfinite(end) || start < previous - 1e-8
					|| start < shifted(note.tick, -leadSeconds) - 1e-8 || end < shifted(start, minimumSeconds) - 1e-8
					|| end > note.tick + note.duration + 1e-8)
					return false;
				previous = end;
			}
		}
		return true;
	}
};
inline void stretchSegments(svs::Note& note, double oldDuration, const StretchLimits* limits)
{
	// Reading a missing key through mutable operator[] inserts a null override.
	// Retire that accidental override from notes stretched by older builds too.
	if (note.phonemes.value("segments").isNull())
		note.phonemes.remove("segments");
	auto segments = note.phonemes.value("segments").toArray();
	if (segments.isEmpty() || oldDuration <= 0)
		return;
	// Leading material is rigid; body material follows the musical duration.
	double previous = -INFINITY;
	for (int i = 0; i < segments.size(); ++i)
	{
		auto segment = segments[i].toObject();
		const auto start = segment["startTick"].toDouble(), end = start + segment["durationTicks"].toDouble();
		const auto map = [&](double tick) { return tick <= 0 ? tick : tick * note.duration / oldDuration; };
		const auto mappedStart = std::max(previous, map(start));
		const auto mappedEnd = limits
			? std::max(map(end), limits->shifted(note.tick + mappedStart, limits->minimumSeconds) - note.tick)
			: map(end);
		segment["startTick"] = mappedStart;
		segment["durationTicks"] = mappedEnd - mappedStart;
		segments[i] = segment;
		previous = mappedEnd;
	}
	note.phonemes["segments"] = segments;
}
inline void stretchNoteUnchecked(QVector<svs::Note>& notes, const QString& id, double boundary, bool head,
	double minimum, const QString& coupled, const StretchLimits* limits)
{
	const auto before = notes;
	int index = -1;
	for (int i = 0; i < notes.size(); ++i)
		if (notes[i].id == id)
			index = i;
	if (index < 0)
		return;
	auto& target = notes[index];
	const auto oldStart = target.tick, oldEnd = target.tick + target.duration, oldDuration = target.duration;
	int previous = -1;
	if (!coupled.isEmpty())
		for (int i = 0; i < notes.size(); ++i)
			if (notes[i].id == coupled)
				previous = i;
	if (head)
	{
		const auto lower = previous >= 0 ? notes[previous].tick + minimum : 0.;
		boundary = std::clamp(boundary, lower, std::max(lower, oldEnd - minimum));
		target.tick = boundary;
		target.duration = oldEnd - boundary;
	}
	else
	{
		boundary = std::max(oldStart + minimum, boundary);
		target.duration = boundary - oldStart;
	}
	stretchSegments(target, oldDuration, limits);
	if (previous >= 0)
	{
		auto& neighbor = notes[previous];
		const auto duration = neighbor.duration;
		neighbor.duration = boundary - neighbor.tick;
		stretchSegments(neighbor, duration, limits);
	}
	// Voice notes are monophonic: extension trims a partially covered neighbor,
	// but contraction leaves a gap. Fully covered notes disappear in the preview.
	for (int i = notes.size() - 1; i >= 0; --i)
	{
		if (notes[i].id == id || notes[i].id == coupled)
			continue;
		auto& neighbor = notes[i];
		const auto end = neighbor.tick + neighbor.duration, duration = neighbor.duration;
		if (head && boundary < oldStart && neighbor.tick < oldStart && end > boundary)
		{
			if (neighbor.tick >= boundary)
				notes.removeAt(i);
			else
			{
				neighbor.duration = boundary - neighbor.tick;
				stretchSegments(neighbor, duration, limits);
			}
		}
		else if (!head && boundary > oldEnd && neighbor.tick >= oldEnd && neighbor.tick < boundary)
		{
			if (end <= boundary)
				notes.removeAt(i);
			else
			{
				neighbor.tick = boundary;
				neighbor.duration = end - boundary;
				stretchSegments(neighbor, duration, limits);
			}
		}
	}
	// A terminal vowel that originally filled to a note end or the next
	// consonant remains a derived fill. Preserve deliberately detached tails.
	for (auto& note : notes)
	{
		const svs::Note* old = nullptr;
		for (const auto& item : before)
			if (item.id == note.id)
				old = &item;
		if (!old)
			continue;
		auto segments = note.phonemes.value("segments").toArray();
		const auto oldSegments = old->phonemes["segments"].toArray();
		if (segments.isEmpty() || oldSegments.isEmpty())
			continue;
		const auto oldLast = oldSegments.last().toObject();
		const auto oldTail = old->tick + oldLast["startTick"].toDouble() + oldLast["durationTicks"].toDouble();
		double oldFill = old->tick + old->duration, newFill = note.tick + note.duration;
		const svs::Note* oldNext = nullptr;
		const svs::Note* next = nullptr;
		for (const auto& item : before)
			if (item.tick > old->tick && (!oldNext || item.tick < oldNext->tick))
				oldNext = &item;
		for (const auto& item : notes)
			if (item.tick > note.tick && (!next || item.tick < next->tick))
				next = &item;
		if (oldNext && !oldNext->phonemes["segments"].toArray().isEmpty())
			oldFill = std::min(oldFill,
				oldNext->tick + oldNext->phonemes["segments"].toArray().first().toObject()["startTick"].toDouble());
		if (next && !next->phonemes["segments"].toArray().isEmpty())
			newFill = std::min(
				newFill, next->tick + next->phonemes["segments"].toArray().first().toObject()["startTick"].toDouble());
		if (std::abs(oldTail - oldFill) > 1e-8 || oldLast["stretchWeight"].toDouble(1) <= 0)
			continue;
		auto last = segments.last().toObject();
		const auto start = note.tick + last["startTick"].toDouble();
		if (newFill <= start)
			continue;
		last["durationTicks"] = newFill - start;
		segments[segments.size() - 1] = last;
		note.phonemes["segments"] = segments;
	}
}
inline void stretchNote(QVector<svs::Note>& notes, const QString& id, double boundary, bool head, double minimum,
	const QString& coupled = {}, const StretchLimits* limits = nullptr)
{
	const auto before = notes;
	stretchNoteUnchecked(notes, id, boundary, head, minimum, coupled, limits);
	if (!limits || limits->accepts(notes))
		return;
	notes = before;
	if (!limits->accepts(before))
		return;
	double safe = 0;
	bool found = false;
	for (const auto& note : before)
		if (note.id == id)
		{
			safe = head ? note.tick : note.tick + note.duration;
			found = true;
			break;
		}
	if (!found)
		return;
	double rejected = boundary;
	// Clamp the shared gesture, including any trimmed neighbor, to the last
	// boundary that still satisfies the voice's tempo-aware phoneme limits.
	for (int iteration = 0; iteration < 40; ++iteration)
	{
		const auto candidate = (safe + rejected) / 2;
		auto preview = before;
		stretchNoteUnchecked(preview, id, candidate, head, minimum, coupled, limits);
		if (limits->accepts(preview))
		{
			safe = candidate;
			notes = std::move(preview);
		}
		else
			rejected = candidate;
	}
}

}
#endif
