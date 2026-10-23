#include "SvsPitchCommands.h"

#include "agent/CommandBus.h"
#include "CommandTargets.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <limits>

#include "SVSClip.h"
#include "SVSModel.h"
#include "Song.h"

namespace lmms::agent {
namespace {

// The piano-roll pitch line is stored as the clip curve "svs.pitch" with absolute
// semitone values. It is an editor curve, not a voicebank parameter, so it cannot go
// through svs.setCurve (which only accepts declared capability parameters).
QString pitchCurveId() { return QStringLiteral("svs.pitch"); }

CommandResult invalidArguments(const QString& message)
{
	return CommandResult::failure("invalid_arguments", message);
}

bool resolveSvsClipTarget(
	const QJsonObject& arguments, SVSClip*& clip, int& trackIndex, int& clipIndex, CommandResult& failure)
{
	auto* song = Engine::getSong();
	if (song == nullptr)
	{
		failure = CommandResult::failure("engine_unavailable", "The song model is unavailable.");
		return false;
	}

	ClipAddress address;
	if (!resolveClip(song, arguments, address, failure))
	{
		return false;
	}
	auto* svsClip = dynamic_cast<SVSClip*>(address.clip);
	if (svsClip == nullptr)
	{
		failure = CommandResult::failure("wrong_clip_type",
			QStringLiteral("Clip %1 on track %2 is not an SVS clip.").arg(address.clipIndex).arg(address.trackIndex));
		return false;
	}

	clip = svsClip;
	trackIndex = address.trackIndex;
	clipIndex = address.clipIndex;
	return true;
}

struct PitchPoint
{
	double tick = 0;
	double value = 0;
};

bool readPoints(const QJsonObject& arguments, QVector<PitchPoint>& points, QString& error)
{
	const auto value = arguments.value("points");
	if (!value.isArray() || value.toArray().isEmpty())
	{
		error = "'points' must be a non-empty array of {tick, value} objects.";
		return false;
	}

	double previous = -std::numeric_limits<double>::infinity();
	for (const auto& item : value.toArray())
	{
		const auto object = item.toObject();
		const double tick = object.value("tick").toDouble(std::nan(""));
		const double pitch = object.value("value").toDouble(std::nan(""));
		if (!std::isfinite(tick) || !std::isfinite(pitch))
		{
			error = "Each point requires a finite 'tick' and 'value'.";
			return false;
		}
		if (tick < 0)
		{
			error = "'tick' must not be negative.";
			return false;
		}
		if (tick <= previous)
		{
			error = "'points' must be sorted by strictly increasing 'tick'.";
			return false;
		}
		previous = tick;
		points.append({tick, pitch});
	}
	return true;
}

// Returns the pitch of the note that covers the tick, or of the last note starting at
// or before it, so that offsets can be resolved against the sustained note.
double notePitchAt(const QVector<svs::Note>& notes, double tick)
{
	const svs::Note* latest = nullptr;
	for (const auto& note : notes)
	{
		if (tick >= note.tick && tick < note.tick + note.duration)
		{
			return note.pitch;
		}
		if (note.tick <= tick && (latest == nullptr || note.tick >= latest->tick))
		{
			latest = &note;
		}
	}
	return latest != nullptr ? latest->pitch : std::nan("");
}

CommandResult executeSetPitchCurve(const QJsonObject& arguments)
{
	SVSClip* clip = nullptr;
	int trackIndex = -1;
	int clipIndex = -1;
	CommandResult failure;
	if (!resolveSvsClipTarget(arguments, clip, trackIndex, clipIndex, failure))
	{
		return failure;
	}
	if (clip->readOnly())
	{
		return CommandResult::failure("svs_clip_read_only", "The SVS clip is read-only.");
	}

	auto curves = clip->curves();
	if (arguments.value("clear").toBool(false))
	{
		if (curves.remove(pitchCurveId()) == 0)
		{
			return CommandResult::failure(
				"svs_curve_not_found", "The clip does not have a pitch curve to clear.");
		}
		clip->setEditorData(clip->notes(), curves);
		return CommandResult::success(QJsonObject{{"path", clipPath(trackIndex, clipIndex, clip->getTrack())},
			{"id", pitchCurveId()}, {"cleared", true}, {"curves", svs::curvesToJson(clip->curves())}});
	}

	QVector<PitchPoint> points;
	QString error;
	if (!readPoints(arguments, points, error))
	{
		return invalidArguments(error);
	}

	const QString interpolation = arguments.value("interpolation").toString("hermite");
	if (interpolation != "linear" && interpolation != "hermite" && interpolation != "step")
	{
		return invalidArguments("'interpolation' must be linear, hermite or step.");
	}

	const bool relative = arguments.value("relative").toBool(true);
	const double clipLength = static_cast<double>(clip->length().getTicks());
	QJsonArray jsonPoints;
	for (const auto& point : points)
	{
		if (point.tick > clipLength)
		{
			return invalidArguments(QStringLiteral("'tick' %1 exceeds the clip length %2.")
					.arg(point.tick)
					.arg(clipLength));
		}

		double pitch = point.value;
		if (relative)
		{
			const double anchor = notePitchAt(clip->notes(), point.tick);
			if (!std::isfinite(anchor))
			{
				return CommandResult::failure("svs_pitch_anchor_missing",
					QStringLiteral("No note starts at or before tick %1, so a relative pitch cannot be resolved.")
						.arg(point.tick));
			}
			pitch += anchor;
		}
		if (pitch < 0.0 || pitch > 127.0)
		{
			return invalidArguments(QStringLiteral("Resolved pitch %1 at tick %2 is outside 0..127.")
					.arg(pitch)
					.arg(point.tick));
		}
		jsonPoints.append(QJsonObject{{"tick", point.tick}, {"value", pitch}});
	}

	const QJsonObject object{{"id", pitchCurveId()}, {"unit", "semitone"}, {"scope", "clip"}, {"type", "float"},
		{"mode", ""}, {"interpolation", interpolation}, {"points", jsonPoints}, {"gaps", QJsonArray{}}};
	svs::Curve curve;
	if (!svs::Curve::fromJson(object, curve, error))
	{
		return CommandResult::failure("svs_curve_invalid", error);
	}

	curves[pitchCurveId()] = curve;
	clip->setEditorData(clip->notes(), curves);
	return CommandResult::success(QJsonObject{{"path", clipPath(trackIndex, clipIndex, clip->getTrack())},
		{"id", pitchCurveId()}, {"pointCount", jsonPoints.size()}, {"relative", relative},
		{"curves", svs::curvesToJson(clip->curves())}});
}

} // namespace

void registerSvsPitchCommands(CommandBus& commandBus)
{
	const auto trackSchema = QJsonObject{{"anyOf",
		QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "string"}}}}};
	const auto pointSchema = QJsonObject{{"type", "object"},
		{"properties",
			QJsonObject{{"tick", QJsonObject{{"type", "number"}, {"minimum", 0}}},
				{"value", QJsonObject{{"type", "number"}}}}},
		{"required", QJsonArray{"tick", "value"}}};

	CommandDescriptor descriptor;
	descriptor.name = "svs.setPitchCurve";
	descriptor.summary = "Write or clear the piano-roll pitch line (svs.pitch) of an SVS clip.";
	descriptor.argsSchema = QJsonObject{{"type", "object"},
		{"properties",
			QJsonObject{{"track", trackSchema}, {"clip", QJsonObject{{"type", "integer"}, {"minimum", 0}}},
				{"points", QJsonObject{{"type", "array"}, {"items", pointSchema}, {"maxItems", 4096}}},
				{"interpolation",
					QJsonObject{{"type", "string"}, {"enum", QJsonArray{"linear", "hermite", "step"}}}},
				{"relative", QJsonObject{{"type", "boolean"}}}, {"clear", QJsonObject{{"type", "boolean"}}}}},
		{"required", QJsonArray{"track", "clip"}}};
	descriptor.mutability = Mutability::Mutating;
	descriptor.scope = TxScope::Single;
	descriptor.handler = executeSetPitchCurve;
	commandBus.registerCommand(descriptor);
}

} // namespace lmms::agent
