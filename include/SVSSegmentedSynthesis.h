#ifndef LMMS_SVS_SEGMENTED_SYNTHESIS_H
#define LMMS_SVS_SEGMENTED_SYNTHESIS_H
#include "SVSModel.h"
namespace lmms::svs {
struct SynthesisSegment
{
	Input input;
	QString signature;
	double startSeconds = 0, endSeconds = 0;
	std::shared_ptr<const Audio> audio;
	bool cached = false;
};
bool supportsSegmentedSynthesis(const Input&);
QVector<SynthesisSegment> planSynthesisSegments(const Input&, const TimeMapping&, QString& error);
std::shared_ptr<const Audio> assembleSynthesisSegments(
	const Input&, const TimeMapping&, const QVector<SynthesisSegment>&, QString& error, bool complete = false);
}
#endif
