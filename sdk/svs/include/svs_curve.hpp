/* Header-only curve evaluation shared by host and standalone plugins.
 * Curve coordinates are fractional content-local ticks. No bar/note buckets. */
#ifndef SVS_CURVE_HPP
#define SVS_CURVE_HPP
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace svs_sdk {
enum class Interpolation
{
	Linear,
	Hermite,
	Step
};
struct CurvePoint
{
	double tick = 0, value = 0, tangentIn = 0, tangentOut = 0;
	bool automatic = true, breakAfter = false;
	std::string valueId;
	int segmentInterpolation = -1; // -1 inherits the curve; otherwise Interpolation.
};
struct Gap
{
	double start = 0, end = 0;
};
struct CurveSample
{
	bool covered = false;
	double value = 0, derivative = 0;
	const std::string* valueId = nullptr;
};

class Curve
{
public:
	std::vector<CurvePoint> points;
	std::vector<Gap> gaps;
	Interpolation interpolation = Interpolation::Linear;
	bool constrainTangents = true; // False only for host-derived absolute-minus-reference pitch.

	double automaticTangent(size_t index) const
	{
		if (points.size() < 2)
			return 0;
		auto slope = [this](size_t left) {
			const auto& a = points[left];
			const auto& b = points[left + 1];
			return a.breakAfter ? 0 : (b.value - a.value) / (b.tick - a.tick);
		};
		if (index == 0)
			return slope(0);
		if (index + 1 == points.size())
			return slope(index - 1);
		const auto left = slope(index - 1), right = slope(index);
		if (left * right <= 0)
			return 0;
		// Harmonic mean bounds the tangent relative to both neighboring secants.
		return 2 * left * right / (left + right);
	}

	CurveSample evaluate(double tick) const
	{
		if (!std::isfinite(tick) || points.empty() || tick < points.front().tick || tick > points.back().tick)
			return {};
		for (const auto& gap : gaps)
			if (tick > gap.start && tick < gap.end)
				return {};
		if (points.size() == 1)
			return {tick == points[0].tick, points[0].value, 0, &points[0].valueId};
		auto upper = std::upper_bound(
			points.begin(), points.end(), tick, [](double t, const CurvePoint& p) { return t < p.tick; });
		const size_t index = upper == points.end() ? points.size() - 2 : size_t(upper - points.begin() - 1);
		const auto& a = points[index];
		const auto& b = points[index + 1];
		if (a.breakAfter && tick != a.tick && tick != b.tick)
			return {};
		if (a.breakAfter)
			return tick == a.tick ? CurveSample{true, a.value, 0, &a.valueId}
								  : CurveSample{true, b.value, 0, &b.valueId};
		const double length = b.tick - a.tick;
		if (length <= 0)
			return {};
		const double u = (tick - a.tick) / length, secant = (b.value - a.value) / length;
		const auto segment
			= a.segmentInterpolation < 0 ? interpolation : static_cast<Interpolation>(a.segmentInterpolation);
		if (segment == Interpolation::Step)
		{
			const auto& point = tick == b.tick ? b : a;
			return {true, point.value, 0, &point.valueId};
		}
		if (segment == Interpolation::Linear)
			return {true, a.value + (b.value - a.value) * u, secant, nullptr};
		double left = a.automatic ? automaticTangent(index) : a.tangentOut;
		double right = b.automatic ? automaticTangent(index + 1) : b.tangentIn;
		if (constrainTangents)
		{
			if (secant == 0)
				left = right = 0;
			else
			{
				double alpha = left / secant, beta = right / secant;
				if (alpha < 0)
					left = 0;
				if (beta < 0)
					right = 0;
				alpha = left / secant;
				beta = right / secant;
				const double magnitude = alpha * alpha + beta * beta;
				if (magnitude > 9)
				{
					const auto scale = 3 / std::sqrt(magnitude);
					left *= scale;
					right *= scale;
				}
			}
		}
		const double u2 = u * u, u3 = u2 * u;
		const double value = (2 * u3 - 3 * u2 + 1) * a.value + (u3 - 2 * u2 + u) * length * left
			+ (-2 * u3 + 3 * u2) * b.value + (u3 - u2) * length * right;
		const double derivative = ((6 * u2 - 6 * u) * a.value + (3 * u2 - 4 * u + 1) * length * left
									  + (-6 * u2 + 6 * u) * b.value + (3 * u2 - 2 * u) * length * right)
			/ length;
		return {true, value, derivative, nullptr};
	}
};
}
#endif
