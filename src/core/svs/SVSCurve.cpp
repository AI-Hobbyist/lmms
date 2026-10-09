#include "SVSCurve.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>

namespace lmms::svs {
Curve withParameterBase(const Curve& source, const Parameter& p, const QJsonValue& base)
{
	auto curve = source;
	if (p.type != "float" && p.type != "int")
		return curve;
	const double offset = base.toDouble() - p.defaultValue.toDouble();
	for (auto& point : curve.evaluator.points)
		point.value = std::clamp(point.value + offset, p.minimum, p.maximum);
	return curve;
}

std::optional<QJsonValue> Curve::valueAt(double tick) const
{
	const auto sample = evaluator.evaluate(tick);
	if (!sample.covered)
		return {};
	if (type == "enum")
		return QString::fromStdString(sample.valueId ? *sample.valueId : std::string{});
	if (type == "bool")
		return sample.value != 0;
	return sample.value;
}
double Curve::derivativeAt(double tick) const
{
	return evaluator.evaluate(tick).derivative;
}
void Curve::insert(double tick, const QJsonValue& value)
{
	if (!std::isfinite(tick))
		return;
	svs_sdk::CurvePoint point;
	point.tick = tick;
	if (value.isString())
		point.valueId = value.toString().toStdString();
	else
		point.value = value.isBool() ? (value.toBool() ? 1 : 0) : value.toDouble();
	auto& points = evaluator.points;
	auto location = std::lower_bound(
		points.begin(), points.end(), tick, [](const auto& point, double tick) { return point.tick < tick; });
	if (location != points.end() && location->tick == tick)
	{
		point.automatic = location->automatic;
		point.tangentIn = location->tangentIn;
		point.tangentOut = location->tangentOut;
		point.breakAfter = location->breakAfter;
		point.segmentInterpolation = location->segmentInterpolation;
		*location = point;
	}
	else
		points.insert(location, point);
}
void Curve::erase(double start, double end)
{
	if (start > end)
		std::swap(start, end);
	if (start == end)
		return;
	pinTangents();
	auto left = valueAt(start), right = valueAt(end);
	auto leftDerivative = derivativeAt(start), rightDerivative = derivativeAt(end);
	auto& points = evaluator.points;
	points.erase(std::remove_if(points.begin(), points.end(),
					 [&](const auto& point) { return point.tick > start && point.tick < end; }),
		points.end());
	if (left)
	{
		insert(start, *left);
		auto i
			= std::lower_bound(points.begin(), points.end(), start, [](const auto& p, double t) { return p.tick < t; });
		i->automatic = false;
		i->tangentIn = i->tangentOut = leftDerivative;
	}
	if (right)
	{
		insert(end, *right);
		auto i
			= std::lower_bound(points.begin(), points.end(), end, [](const auto& p, double t) { return p.tick < t; });
		i->automatic = false;
		i->tangentIn = i->tangentOut = rightDerivative;
	}
	evaluator.gaps.push_back({start, end});
}
void Curve::pinTangents()
{
	std::vector<double> tangents;
	for (size_t i = 0; i < evaluator.points.size(); ++i)
		tangents.push_back(evaluator.automaticTangent(i));
	for (size_t i = 0; i < evaluator.points.size(); ++i)
		if (evaluator.points[i].automatic)
		{
			auto& point = evaluator.points[i];
			point.automatic = false;
			point.tangentIn = point.tangentOut = tangents[i];
		}
}
void Curve::replaceRange(double start, double end, const Curve& source)
{
	if (!std::isfinite(start) || !std::isfinite(end) || start > end)
		return;
	pinTangents();
	// Preserve the outside limits independently of the newly drawn endpoint values.
	// Qt's JSON writer rounds nextafter(0) (a subnormal) to zero, collapsing
	// the boundary gap and making a stroke at the content origin invalid.
	const double originBoundary = std::numeric_limits<double>::epsilon();
	const double leftTick = start == 0 ? -originBoundary : std::nextafter(start, -INFINITY),
				 rightTick = end == 0 ? originBoundary : std::nextafter(end, INFINITY);
	const auto left = valueAt(leftTick), right = valueAt(rightTick);
	const auto leftSlope = derivativeAt(leftTick), rightSlope = derivativeAt(rightTick);
	auto segmentAt = [&](double tick) {
		auto upper = std::upper_bound(
			evaluator.points.begin(), evaluator.points.end(), tick, [](double t, const auto& p) { return t < p.tick; });
		return upper == evaluator.points.begin() ? -1 : std::prev(upper)->segmentInterpolation;
	};
	const auto leftSegment = segmentAt(leftTick), rightSegment = segmentAt(rightTick);
	auto& points = evaluator.points;
	for (size_t i = 0; i + 1 < points.size(); ++i)
		if (points[i].breakAfter)
		{
			evaluator.gaps.push_back({points[i].tick, points[i + 1].tick});
			points[i].breakAfter = false;
		}
	points.erase(std::remove_if(points.begin(), points.end(),
					 [&](const auto& point) { return point.tick >= start && point.tick <= end; }),
		points.end());
	auto retainBoundary = [&](double tick, const std::optional<QJsonValue>& value, double slope, int segment) {
		if (!value)
			return;
		insert(tick, *value);
		auto point
			= std::lower_bound(points.begin(), points.end(), tick, [](const auto& p, double t) { return p.tick < t; });
		point->automatic = false;
		point->tangentIn = point->tangentOut = slope;
		point->segmentInterpolation = segment;
	};
	retainBoundary(leftTick, left, leftSlope, leftSegment);
	retainBoundary(rightTick, right, rightSlope, rightSegment);
	connect(start, end);
	for (const auto& point : source.evaluator.points)
	{
		auto copied = point;
		copied.tick += start;
		if (copied.tick < start || copied.tick > end)
			continue;
		auto where = std::lower_bound(
			points.begin(), points.end(), copied.tick, [](const auto& p, double t) { return p.tick < t; });
		if (where != points.end() && where->tick == copied.tick)
			*where = copied;
		else
			points.insert(where, copied);
	}
	for (const auto& gap : source.evaluator.gaps)
		evaluator.gaps.push_back({start + gap.start, start + gap.end});
	if (source.evaluator.points.empty())
		evaluator.gaps.push_back({leftTick, rightTick});
	else
	{
		const auto first = start + source.evaluator.points.front().tick,
				   last = start + source.evaluator.points.back().tick;
		if (left)
			evaluator.gaps.push_back({leftTick, first});
		if (right)
			evaluator.gaps.push_back({last, rightTick});
	}
}
void Curve::connect(double start, double end)
{
	if (start > end)
		std::swap(start, end);
	auto& points = evaluator.points;
	for (size_t i = 0; i + 1 < points.size(); ++i)
		if (points[i].breakAfter)
		{
			evaluator.gaps.push_back({points[i].tick, points[i + 1].tick});
			points[i].breakAfter = false;
		}
	std::vector<svs_sdk::Gap> retained;
	for (const auto& gap : evaluator.gaps)
	{
		if (gap.end <= start || gap.start >= end)
			retained.push_back(gap);
		else
		{
			if (gap.start < start)
				retained.push_back({gap.start, start});
			if (gap.end > end)
				retained.push_back({end, gap.end});
		}
	}
	evaluator.gaps = std::move(retained);
}
Curve Curve::slice(double start, double end, bool rebase) const
{
	Curve result = *this;
	result.evaluator.points.clear();
	result.evaluator.gaps.clear();
	if (start > end)
		std::swap(start, end);
	const double origin = rebase ? start : 0;
	for (size_t index = 0; index < evaluator.points.size(); ++index)
	{
		const auto& point = evaluator.points[index];
		if (point.tick >= start && point.tick <= end)
		{
			auto copied = point;
			copied.tick -= origin;
			if (copied.automatic)
			{
				copied.automatic = false;
				copied.tangentIn = copied.tangentOut = evaluator.automaticTangent(index);
			}
			result.evaluator.points.push_back(copied);
		}
	}
	auto boundary = [&](double time) {
		if (auto value = valueAt(time))
		{
			result.insert(time - origin, *value);
			auto& points = result.evaluator.points;
			auto i = std::lower_bound(
				points.begin(), points.end(), time - origin, [](const auto& p, double t) { return p.tick < t; });
			i->automatic = false;
			i->tangentIn = i->tangentOut = derivativeAt(time);
			auto upper = std::upper_bound(evaluator.points.begin(), evaluator.points.end(), time,
				[](double t, const auto& p) { return t < p.tick; });
			if (upper != evaluator.points.begin())
				i->segmentInterpolation = std::prev(upper)->segmentInterpolation;
		}
	};
	boundary(start);
	boundary(end);
	for (const auto& gap : evaluator.gaps)
		if (gap.end > start && gap.start < end)
			result.evaluator.gaps.push_back({std::max(gap.start, start) - origin, std::min(gap.end, end) - origin});
	return result;
}
QJsonObject Curve::toJson() const
{
	auto object = extra;
	object["id"] = id;
	object["unit"] = unit;
	object["scope"] = scope;
	object["type"] = type;
	object["mode"] = mode;
	object["interpolation"] = interpolation;
	QJsonArray points, gaps;
	for (const auto& point : evaluator.points)
	{
		QJsonValue value = point.value;
		if (type == "enum")
			value = QString::fromStdString(point.valueId);
		else if (type == "bool")
			value = point.value != 0;
		QJsonObject anchor{{"tick", point.tick}, {"value", value}, {"automatic", point.automatic},
			{"in", point.tangentIn}, {"out", point.tangentOut}, {"breakAfter", point.breakAfter}};
		if (point.segmentInterpolation >= 0)
			anchor["segment"] = point.segmentInterpolation == 0 ? "linear"
				: point.segmentInterpolation == 1				? "hermite"
																: "step";
		points.append(anchor);
	}
	for (const auto& gap : evaluator.gaps)
		gaps.append(QJsonObject{{"start", gap.start}, {"end", gap.end}});
	object["points"] = points;
	object["gaps"] = gaps;
	if (!evaluator.constrainTangents)
		object["constrained"] = false;
	return object;
}
bool Curve::fromJson(const QJsonObject& object, Curve& output, QString& error, const Parameter* descriptor)
{
	Curve result;
	result.extra = object;
	result.id = object["id"].toString();
	result.unit = object["unit"].toString();
	result.scope = object["scope"].toString("clip");
	result.type = object["type"].toString("float");
	result.mode = object["mode"].toString();
	result.interpolation = object["interpolation"].toString("linear");
	if (result.id.isEmpty() || !object["points"].isArray()
		|| !QStringList{"linear", "hermite", "step"}.contains(result.interpolation)
		|| !QStringList{"float", "int", "bool", "enum"}.contains(result.type))
	{
		error = "Invalid curve metadata";
		return false;
	}
	if (result.type != "float" && result.interpolation != "step")
	{
		error = "Discrete curve requires step interpolation";
		return false;
	}
	result.evaluator.interpolation = result.interpolation == "hermite" ? svs_sdk::Interpolation::Hermite
		: result.interpolation == "step"							   ? svs_sdk::Interpolation::Step
																	   : svs_sdk::Interpolation::Linear;
	result.evaluator.constrainTangents = object["constrained"].toBool(true);
	if (!result.evaluator.constrainTangents && result.mode != "offset")
	{
		error = "Only derived pitch offsets may use unconstrained difference tangents";
		return false;
	}
	double previous = -INFINITY;
	for (const auto& item : object["points"].toArray())
	{
		const auto p = item.toObject();
		const auto tick = p["tick"].toDouble(NAN);
		const auto value = p["value"];
		if (!std::isfinite(tick) || tick <= previous || (descriptor && !descriptor->accepts(value))
			|| (result.type == "enum" && !value.isString()) || (result.type == "bool" && !value.isBool())
			|| ((result.type == "float" || result.type == "int")
				&& (!value.isDouble() || !std::isfinite(value.toDouble()))))
		{
			error = "Invalid curve anchor";
			return false;
		}
		result.insert(tick, value);
		auto& point = result.evaluator.points.back();
		point.automatic = p["automatic"].toBool(true);
		point.tangentIn = p["in"].toDouble();
		point.tangentOut = p["out"].toDouble();
		point.breakAfter = p["breakAfter"].toBool();
		if (p.contains("segment"))
		{
			const auto segment = p["segment"].toString();
			if (!QStringList{"linear", "hermite", "step"}.contains(segment)
				|| (result.type != "float" && segment != "step"))
			{
				error = "Invalid segment interpolation";
				return false;
			}
			point.segmentInterpolation = segment == "linear" ? 0 : segment == "hermite" ? 1 : 2;
		}
		if ((result.type == "int" && value.toDouble() != std::floor(value.toDouble()))
			|| (p.contains("in") && !p["in"].isDouble()) || (p.contains("out") && !p["out"].isDouble())
			|| !std::isfinite(point.tangentIn) || !std::isfinite(point.tangentOut))
		{
			error = "Invalid curve tangent or integer value";
			return false;
		}
		previous = tick;
	}
	for (const auto& item : object["gaps"].toArray())
	{
		const auto gap = item.toObject();
		auto start = gap["start"].toDouble(NAN), end = gap["end"].toDouble(NAN);
		if (!std::isfinite(start) || !std::isfinite(end) || start >= end)
		{
			error = "Invalid curve gap";
			return false;
		}
		result.evaluator.gaps.push_back({start, end});
	}
	output = std::move(result);
	error.clear();
	return true;
}
QJsonObject curvesToJson(const Curves& curves)
{
	QJsonObject result;
	for (auto i = curves.begin(); i != curves.end(); ++i)
		result[i.key()] = i.value().toJson();
	return result;
}
bool absolutePitchToOffset(const Curve& absolute, const Curve& reference, Curve& output, QString& error)
{
	if (absolute.type != "float" || absolute.mode != "absolute" || reference.type != "float"
		|| reference.mode != "absolute" || reference.unit != "semitone" || reference.evaluator.points.empty())
	{
		error = "Offset pitch requires an explicit absolute semitone reference curve";
		return false;
	}
	Curve result = absolute;
	result.mode = "offset";
	result.interpolation = "hermite";
	result.evaluator = {};
	result.evaluator.interpolation = svs_sdk::Interpolation::Hermite;
	result.evaluator.constrainTangents = false;
	if (absolute.evaluator.points.empty())
	{
		output = result;
		error.clear();
		return true;
	}
	const auto from = absolute.evaluator.points.front().tick, to = absolute.evaluator.points.back().tick;
	std::vector<double> ticks;
	auto add = [&](double tick) {
		if (tick >= from && tick <= to)
			ticks.push_back(tick);
	};
	auto boundaries = [&](const Curve& curve) {
		for (size_t i = 0; i < curve.evaluator.points.size(); ++i)
		{
			const auto& point = curve.evaluator.points[i];
			add(point.tick);
			if (i)
				add(std::nextafter(point.tick, -INFINITY));
			if (i + 1 < curve.evaluator.points.size() && point.breakAfter)
				result.evaluator.gaps.push_back({point.tick, curve.evaluator.points[i + 1].tick});
		}
		for (const auto& gap : curve.evaluator.gaps)
		{
			add(gap.start);
			add(gap.end);
			result.evaluator.gaps.push_back(gap);
		}
	};
	boundaries(absolute);
	boundaries(reference);
	std::sort(ticks.begin(), ticks.end());
	ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());
	for (size_t i = 0; i < ticks.size(); ++i)
	{
		const auto tick = ticks[i];
		const auto a = absolute.evaluator.evaluate(tick), r = reference.evaluator.evaluate(tick);
		if (a.covered && !r.covered)
		{
			error = "Reference pitch does not cover the edited pitch range";
			return false;
		}
		if (i + 1 < ticks.size())
		{
			const auto middle = std::midpoint(tick, ticks[i + 1]);
			if (absolute.evaluator.evaluate(middle).covered && !reference.evaluator.evaluate(middle).covered)
			{
				error = "Reference pitch has a gap in the edited range";
				return false;
			}
		}
		if (!a.covered)
			continue;
		result.insert(tick, a.value - r.value);
		auto& point = result.evaluator.points.back();
		point.automatic = false;
		auto slope = [&](double at) {
			auto x = absolute.evaluator.evaluate(at), y = reference.evaluator.evaluate(at);
			return x.covered && y.covered ? x.derivative - y.derivative : a.derivative - r.derivative;
		};
		point.tangentIn = slope(std::nextafter(tick, -INFINITY));
		point.tangentOut = slope(std::nextafter(tick, INFINITY));
	}
	output = std::move(result);
	error.clear();
	return true;
}
Curves curvesFromJson(const QJsonObject& object, QStringList* diagnostics)
{
	Curves result;
	for (auto i = object.begin(); i != object.end(); ++i)
	{
		Curve curve;
		QString error;
		if (Curve::fromJson(i.value().toObject(), curve, error))
			result[i.key()] = curve;
		else if (diagnostics)
			*diagnostics << i.key() + ": " + error;
	}
	return result;
}
}
