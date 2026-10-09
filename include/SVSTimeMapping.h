#ifndef LMMS_SVS_TIME_MAPPING_H
#define LMMS_SVS_TIME_MAPPING_H
#include "svs_time.hpp"
#include <memory>
#include <QJsonObject>
#include <QJsonArray>
namespace lmms::svs {
// Content offset is positive for a left crop. LMMS Clip stores its negation.
struct TimeMapping
{
	double position = 0, contentOffset = 0, secondsPerTick = 0;
	std::shared_ptr<const svs_sdk::TempoMap> tempo;
	double projectTick(double localTick) const { return position + localTick - contentOffset; }
	double localTick(double projectTick) const { return projectTick - position + contentOffset; }
	double globalSeconds(double localTick) const
	{
		return tempo ? tempo->secondsAt(projectTick(localTick)) : projectTick(localTick) * secondsPerTick;
	}
	double resultStartTick(double globalSeconds) const
	{
		return localTick(tempo ? tempo->tickAt(globalSeconds) : globalSeconds / secondsPerTick);
	}
	double localSeconds(double localTick) const { return globalSeconds(localTick) - globalSeconds(0); }
	double tickAtLocalSeconds(double seconds) const { return resultStartTick(globalSeconds(0) + seconds); }
	double samplePosition(double localTick, double resultStartTick, double rate) const
	{
		return (globalSeconds(localTick) - globalSeconds(resultStartTick)) * rate;
	}
};
inline bool readTimeMapping(const QJsonObject& document, double secondsPerTick, TimeMapping& mapping, QString& error)
{
	mapping = {document["position"].toDouble(), document["contentOffset"].toDouble(), secondsPerTick};
	if (!std::isfinite(mapping.position) || !std::isfinite(mapping.contentOffset) || !std::isfinite(secondsPerTick)
		|| secondsPerTick <= 0)
	{
		error = "Invalid SVS time origin";
		return false;
	}
	if (!document.contains("tempoMap"))
		return true;
	if (!document["tempoMap"].isArray() || document["tempoMap"].toArray().size() > 2 * 1024 * 1024)
	{
		error = "Invalid SVS tempo map";
		return false;
	}
	std::vector<svs_sdk::TempoPoint> points;
	for (const auto& value : document["tempoMap"].toArray())
	{
		const auto object = value.toObject();
		points.push_back({object["tick"].toDouble(NAN), object["secondsPerTick"].toDouble(NAN)});
	}
	auto tempo = std::make_shared<svs_sdk::TempoMap>();
	if (!tempo->setPoints(points))
	{
		error = "Invalid SVS tempo map";
		return false;
	}
	mapping.tempo = std::move(tempo);
	return true;
}
}
#endif
