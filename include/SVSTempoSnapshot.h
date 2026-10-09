#ifndef LMMS_SVS_TEMPO_SNAPSHOT_H
#define LMMS_SVS_TEMPO_SNAPSHOT_H
#include <QJsonArray>
#include <QJsonObject>
#include <memory>
#include <optional>
#include <vector>
namespace lmms {
class Song;
namespace svs {
class RenderControl;
// Captured on the owner thread; evaluation never touches a project QObject.
struct TempoSnapshot
{
	struct Node
	{
		int tick = 0;
		float in = 0, out = 0, inTangent = 0, outTangent = 0;
	};
	struct Layer
	{
		int start = 0, length = 0, offset = 0, progression = 0, patternPeriod = 0, patternBase = 0;
		bool inPattern = false;
		float tension = 1;
		std::vector<Node> nodes;
		std::vector<Layer> children;
	};
	int baseTempo = 140, minimum = 10, maximum = 999;
	std::vector<Layer> layers;
	static std::shared_ptr<const TempoSnapshot> capture(Song&, int baseTempo);
	std::optional<int> automatedTempoAt(int tick) const;
	int tempoAt(int tick) const;
	double tickAfterSeconds(double projectTick, double seconds) const;
	QJsonObject toJson() const;
	bool buildMap(int lastTick, QJsonArray&, QString&, const std::shared_ptr<RenderControl>& control = {}) const;
};
}
}
#endif
