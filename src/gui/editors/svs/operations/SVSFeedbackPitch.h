#ifndef LMMS_SVS_FEEDBACK_PITCH_H
#define LMMS_SVS_FEEDBACK_PITCH_H
#include "SVSMonotonicCurve.h"
#include "SVSTimeMapping.h"

namespace lmms::gui {
// Feedback entries carry a sampled pitch and its covered interval. Interpolate
// contiguous samples for display, never bridging a gap or a different note.
inline QVector<svs::Curve> feedbackPitchCurves(const QJsonArray& samples, const svs::TimeMapping& mapping)
{
	QVector<svs::Curve> result;
	svs::Curve curve;
	double previousEnd = -INFINITY;
	QString noteId;
	auto finish = [&] {
		if (curve.evaluator.points.empty())
			return;
		curve.insert(previousEnd, curve.evaluator.points.back().value);
		result.push_back(monotonicCurve(curve));
		curve.evaluator.points.clear();
	};
	for (const auto& item : samples)
	{
		const auto sample = item.toObject();
		const auto seconds = sample["startSeconds"].toDouble(NAN), duration = sample["durationSeconds"].toDouble(NAN),
				   value = sample["value"].toDouble(NAN);
		if (!std::isfinite(seconds) || !std::isfinite(duration) || !std::isfinite(value) || duration <= 0)
		{
			finish();
			continue;
		}
		const auto tick = mapping.tickAtLocalSeconds(seconds), end = mapping.tickAtLocalSeconds(seconds + duration);
		const auto id = sample["noteId"].toString();
		if (!curve.evaluator.points.empty()
			&& (id != noteId || std::abs(tick - previousEnd) >= 1e-3 || tick <= curve.evaluator.points.back().tick))
			finish();
		curve.insert(tick, value);
		previousEnd = end;
		noteId = id;
	}
	finish();
	return result;
}
}
#endif
