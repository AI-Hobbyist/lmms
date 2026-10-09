/* Immutable tick/second mapping shared by host and standalone plugins.
 * Each segment uses the tempo effective for that project tick. */
#ifndef SVS_TIME_HPP
#define SVS_TIME_HPP
#include <algorithm>
#include <cmath>
#include <vector>
namespace svs_sdk {
struct TempoPoint
{
	double tick = 0, secondsPerTick = 0;
};
class TempoMap
{
public:
	bool setPoints(const std::vector<TempoPoint>& points)
	{
		if (points.empty() || points.front().tick != 0)
			return false;
		std::vector<Segment> segments;
		segments.reserve(points.size());
		double seconds = 0;
		for (size_t i = 0; i < points.size(); ++i)
		{
			const auto& point = points[i];
			if (!std::isfinite(point.tick) || !std::isfinite(point.secondsPerTick) || point.secondsPerTick <= 0
				|| (i && point.tick <= points[i - 1].tick))
				return false;
			if (i)
				seconds += (point.tick - points[i - 1].tick) * points[i - 1].secondsPerTick;
			if (!std::isfinite(seconds))
				return false;
			segments.push_back({point.tick, point.secondsPerTick, seconds});
		}
		m_segments = std::move(segments);
		return true;
	}
	bool empty() const { return m_segments.empty(); }
	size_t bytes() const { return m_segments.size() * sizeof(Segment); }
	double secondsAt(double tick) const
	{
		if (empty())
			return NAN;
		const auto& segment = atTick(tick);
		return segment.seconds + (tick - segment.tick) * segment.secondsPerTick;
	}
	double tickAt(double seconds) const
	{
		if (empty())
			return NAN;
		auto i = std::upper_bound(m_segments.begin(), m_segments.end(), seconds,
			[](double value, const Segment& segment) { return value < segment.seconds; });
		const auto& segment = i == m_segments.begin() ? *i : *std::prev(i);
		return segment.tick + (seconds - segment.seconds) / segment.secondsPerTick;
	}
	double secondsPerTickAt(double tick) const { return empty() ? NAN : atTick(tick).secondsPerTick; }

private:
	struct Segment
	{
		double tick, secondsPerTick, seconds;
	};
	const Segment& atTick(double tick) const
	{
		auto i = std::upper_bound(m_segments.begin(), m_segments.end(), tick,
			[](double value, const Segment& segment) { return value < segment.tick; });
		return i == m_segments.begin() ? *i : *std::prev(i);
	}
	std::vector<Segment> m_segments;
};
}
#endif
