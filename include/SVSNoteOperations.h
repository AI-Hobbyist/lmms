#ifndef LMMS_SVS_NOTE_OPERATIONS_H
#define LMMS_SVS_NOTE_OPERATIONS_H
#include "SVSModel.h"
#include <QUuid>
#include <optional>
#include <utility>
#include <cmath>
namespace lmms::svs {
// A split creates two editable notes with the original lyric/reading/parameters.
// Timing-dependent phoneme overrides are re-parsed; the caller's single journal
// checkpoint retains the complete original, including all manual phoneme data.
inline std::optional<std::pair<Note, Note>> splitNoteForReparse(const Note& source, double tick)
{
	if (!std::isfinite(tick) || tick <= source.tick || tick >= source.tick + source.duration)
		return {};
	auto left = source, right = source;
	left.duration = tick - source.tick;
	right.tick = tick;
	right.duration = source.tick + source.duration - tick;
	right.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	left.phonemes = {};
	right.phonemes = {};
	return std::pair{left, right};
}
}
#endif
