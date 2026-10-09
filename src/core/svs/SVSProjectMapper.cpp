#include "SVSProjectMapper.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace lmms::svs {
namespace {
QString entityId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QString json(const QJsonObject& value)
{
	return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact));
}
bool finiteNumber(const QJsonValue& value)
{
	return value.isDouble() && std::isfinite(value.toDouble());
}
bool integer(const QJsonValue& value)
{
	return finiteNumber(value) && std::floor(value.toDouble()) == value.toDouble();
}
bool hostTick(double value)
{
	return std::isfinite(value) && value >= 0 && value <= std::numeric_limits<int>::max() - 1.;
}
QDomElement child(QDomDocument& document, QDomElement parent, const QString& name)
{
	auto result = document.createElement(name);
	parent.appendChild(result);
	return result;
}
}

double ProjectMapper::audioLengthTicks(const QJsonObject& project, double position, double seconds)
{
	if (!hostTick(position) || !std::isfinite(seconds) || seconds <= 0)
		return NAN;
	const auto tempos = project["song_tempo_list"].toArray();
	double start = position, remaining = seconds;
	for (int i = 0; i < tempos.size(); ++i)
	{
		const auto tempo = tempos[i].toObject();
		const double at = std::round(tempo["position"].toDouble() / 10.);
		const double next
			= i + 1 < tempos.size() ? std::round(tempos[i + 1].toObject()["position"].toDouble() / 10.) : INFINITY;
		if (next <= position)
			continue;
		const double from = std::max(position, at), secondsPerTick = 60. / (std::round(tempo["bpm"].toDouble()) * 48.);
		const double available = (next - from) * secondsPerTick;
		if (remaining <= available)
			return from + remaining / secondsPerTick - start;
		remaining -= available;
		position = next;
	}
	return NAN;
}

ProjectImport ProjectMapper::prepareImport(
	const QJsonObject& project, const ProjectVoice& voice, const QMap<QString, ProjectAudio>& audio)
{
	ProjectImport result;
	auto reject = [&](const QString& reason) {
		result.error = reason;
		result.document.clear();
		return result;
	};
	if (voice.pluginId.isEmpty() != voice.voiceId.isEmpty())
		return reject(QStringLiteral("未选择有效的默认声库"));
	const auto meters = project["time_signature_list"].toArray();
	if (meters.isEmpty())
		return reject(QStringLiteral("工程没有拍号"));
	const auto first = meters.first().toObject();
	if (first["bar_index"].toInt(-1) != 0 || !integer(first["numerator"]) || !integer(first["denominator"]))
		return reject(QStringLiteral("初始拍号无效"));
	const int numerator = first["numerator"].toInt(), denominator = first["denominator"].toInt();
	if (numerator < 1 || numerator > 32 || denominator < 1 || denominator > 32 || (192 * numerator) % denominator)
		return reject(QStringLiteral("拍号无法精确映射到 LMMS 小节时间"));
	for (const auto& entry : meters)
	{
		const auto meter = entry.toObject();
		if (!integer(meter["bar_index"]) || meter["bar_index"].toDouble() < 0
			|| meter["numerator"] != first["numerator"] || meter["denominator"] != first["denominator"])
			return reject(QStringLiteral("当前宿主不能准确导入变化拍号，工程未替换"));
	}
	auto root = result.document.createElement("preparedProject");
	result.document.appendChild(root);
	auto head = child(result.document, root, "head");
	head.setAttribute("timesig_numerator", numerator);
	head.setAttribute("timesig_denominator", denominator);
	const auto tempos = project["song_tempo_list"].toArray();
	if (tempos.isEmpty())
		return reject(QStringLiteral("工程没有速度"));
	auto automation = child(result.document, head, "automationclip");
	auto bpm = child(result.document, automation, "bpm");
	bpm.setAttribute("prog", 0);
	double previous = -1;
	int previousRounded = -1;
	for (const auto& entry : tempos)
	{
		const auto tempo = entry.toObject();
		if (!finiteNumber(tempo["position"]) || !finiteNumber(tempo["bpm"]))
			return reject(QStringLiteral("速度数据无效"));
		const double tick = tempo["position"].toDouble() / 10., value = tempo["bpm"].toDouble();
		if (!hostTick(tick) || tick <= previous || value < 10 || value > 999 || (previous < 0 && tick != 0))
			return reject(QStringLiteral("速度位置或 BPM 超出宿主可表达范围"));
		const int rounded = int(std::round(tick));
		if (rounded <= previousRounded)
			return reject(QStringLiteral("速度变化在整数 tick 边界发生冲突"));
		if (tick != rounded || value != std::round(value))
			result.losses << QStringLiteral("速度 %1 tick / %2 BPM 量化为 %3 tick / %4 BPM")
								 .arg(tick, 0, 'g', 17)
								 .arg(value, 0, 'g', 17)
								 .arg(rounded)
								 .arg(std::round(value));
		auto point = child(result.document, bpm, "time");
		point.setAttribute("pos", rounded);
		point.setAttribute("value", int(std::round(value)));
		point.setAttribute("outValue", int(std::round(value)));
		if (previous < 0)
			head.setAttribute("bpm", int(std::round(value)));
		previous = tick;
		previousRounded = rounded;
	}
	auto content = child(result.document, root, "song");
	auto runtime = child(result.document, content, "journalRuntime");
	runtime.setAttribute("fileName", "");
	runtime.setAttribute("oldFileName", "");
	auto container = child(result.document, content, "trackcontainer");
	const double firstBar = 480. * 4 * numerator / denominator;
	const auto tracks = project["track_list"].toArray();
	if (tracks.isEmpty())
		return reject(QStringLiteral("工程没有可导入轨道"));
	for (const auto& entry : tracks)
	{
		const auto source = entry.toObject();
		const auto type = source["type_"].toString();
		if (type != "Singing" && type != "Instrumental")
			return reject(QStringLiteral("未知轨道类型：%1").arg(type));
		const auto title = source["title"].toString().isEmpty()
			? (voice.name.isEmpty() ? QStringLiteral("SVS") : voice.name)
			: source["title"].toString();
		auto track = child(result.document, container, "track");
		track.setAttribute("type", type == "Singing" ? 7 : 2);
		track.setAttribute("name", title);
		track.setAttribute("muted", source["mute"].toBool() ? 1 : 0);
		track.setAttribute("solo", source["solo"].toBool() ? 1 : 0);
		track.setAttribute("mutedBeforeSolo", source["mute"].toBool() ? 1 : 0);
		auto settings = child(result.document, track, type == "Singing" ? "svstrack" : "sampletrack");
		const double volume = source["volume"].toDouble(1.), pan = source["pan"].toDouble(0.);
		if (!std::isfinite(volume) || volume < 0 || volume > 2 || !std::isfinite(pan) || pan < -1 || pan > 1)
			return reject(QStringLiteral("轨道 %1 音量或声像超出宿主范围").arg(title));
		settings.setAttribute("vol", QString::number(volume * 100, 'g', 17));
		settings.setAttribute("pan", QString::number(pan * 100, 'g', 17));
		if (type == "Instrumental")
		{
			const auto sourcePath = source["audio_file_path"].toString();
			if (!audio.contains(sourcePath))
			{
				result.losses << QStringLiteral("轨道 %1：音频缺失或不可解码：%2；将省略该轨道").arg(title, sourcePath);
				container.removeChild(track);
				continue;
			}
			const auto resource = audio.value(sourcePath);
			const double position = source["offset"].toDouble() / 10.;
			const double duration = resource.durationSeconds > 0
				? audioLengthTicks(project, std::round(position), resource.durationSeconds)
				: resource.lengthTicks;
			if (!hostTick(position) || !hostTick(duration) || duration <= 0 || !hostTick(position + duration))
				return reject(QStringLiteral("轨道 %1 音频时间范围无效").arg(title));
			if (position != std::round(position))
				result.losses << QStringLiteral("轨道 %1 音频起点量化到整数 tick").arg(title);
			auto clip = child(result.document, track, "sampleclip");
			clip.setAttribute("src", resource.path);
			clip.setAttribute("pos", int(std::round(position)));
			clip.setAttribute("len", int(std::ceil(duration)));
			clip.setAttribute("autoresize", 0);
			clip.setAttribute("off", 0);
			continue;
		}
		settings.setAttribute("schemaVersion", 1);
		settings.setAttribute("pluginId", voice.pluginId);
		settings.setAttribute("voiceId", voice.voiceId);
		settings.setAttribute("nameMode", "custom");
		settings.setAttribute("language", voice.language);
		auto clip = child(result.document, track, "svsclip");
		clip.setAttribute("schemaVersion", 1);
		clip.setAttribute("id", entityId());
		clip.setAttribute("pos", 0);
		clip.setAttribute("off", 0);
		clip.setAttribute("autoresize", 0);
		clip.setAttribute("name", title);
		auto notes = child(result.document, clip, "notes");
		double extent = 1;
		for (const auto& entry : source["note_list"].toArray())
		{
			const auto note = entry.toObject();
			if (!finiteNumber(note["start_pos"]) || !finiteNumber(note["length"]) || !integer(note["key_number"]))
				return reject(QStringLiteral("轨道 %1 音符数据无效").arg(title));
			const double tick = note["start_pos"].toDouble() / 10., duration = note["length"].toDouble() / 10.,
						 pitch = note["key_number"].toDouble();
			if (!hostTick(tick) || duration <= 0 || !hostTick(tick + duration) || pitch < 0 || pitch > 127)
				return reject(QStringLiteral("轨道 %1 音符时间或音高超出范围").arg(title));
			auto target = child(result.document, notes, "note");
			target.setAttribute("id", entityId());
			target.setAttribute("tick", QString::number(tick, 'g', 17));
			target.setAttribute("duration", QString::number(duration, 'g', 17));
			target.setAttribute("pitch", int(pitch));
			target.setAttribute("lyric", note["lyric"].toString());
			target.setAttribute("pronunciation", note["pronunciation"].toString());
			target.setAttribute("language", voice.language);
			extent = std::max(extent, tick + duration);
			if (!note["head_tag"].toString().isEmpty()
				|| !note["edited_phones"].isNull() && !note["edited_phones"].isUndefined()
				|| !note["vibrato"].isNull() && !note["vibrato"].isUndefined())
				result.losses << QStringLiteral("轨道 %1：音符私有标签、音素时长或独立颤音参数无法映射；已解析音高保留")
									 .arg(title);
		}
		const auto params = source["edited_params"].toObject();
		QJsonArray points;
		bool interrupted = true;
		double last = -std::numeric_limits<double>::infinity();
		for (const auto& entry : params["pitch"].toObject()["points"].toArray())
		{
			const auto point = entry.toArray();
			if (point.size() != 2 || !finiteNumber(point[0]) || !finiteNumber(point[1]))
				return reject(QStringLiteral("轨道 %1 音高断点数据无效").arg(title));
			const double x = point[0].toDouble(), y = point[1].toDouble();
			if (y == -100)
			{
				if (!points.isEmpty())
				{
					auto previousPoint = points.last().toObject();
					previousPoint["breakAfter"] = true;
					points.replace(points.size() - 1, previousPoint);
				}
				interrupted = true;
				continue;
			}
			const double tick = (x - firstBar) / 10., value = y / 100.;
			if (!hostTick(tick) || value < 0 || value > 127 || tick < last)
				return reject(QStringLiteral("轨道 %1 音高时间或数值超出范围").arg(title));
			if (tick == last)
			{
				if (interrupted)
					return reject(QStringLiteral("轨道 %1 同一时间存在不同音高片段，无法无损表达").arg(title));
				auto previousPoint = points.last().toObject();
				if (previousPoint["value"].toDouble() != value)
					return reject(QStringLiteral("轨道 %1 同一时间存在多个音高值").arg(title));
				continue;
			}
			points.append(QJsonObject{{"tick", tick}, {"value", value}, {"automatic", true}, {"breakAfter", false}});
			last = tick;
			interrupted = false;
			extent = std::max(extent, tick);
		}
		QJsonObject curves;
		if (!points.isEmpty())
			curves["svs.pitch"]
				= QJsonObject{{"id", "svs.pitch"}, {"unit", "semitone"}, {"scope", "clip"}, {"type", "float"},
					{"mode", "absolute"}, {"interpolation", "linear"}, {"points", points}, {"gaps", QJsonArray{}}};
		clip.setAttribute("curves", json(curves));
		clip.setAttribute("len", int(std::ceil(extent)));
		for (const auto& parameter : QStringList{"volume", "breath", "gender", "strength"})
			if (!params[parameter].toObject()["points"].toArray().isEmpty())
				result.losses
					<< QStringLiteral("轨道 %1：参数 %2 与当前声库没有确定的单位对应，将省略").arg(title, parameter);
	}
	result.losses.removeDuplicates();
	return result;
}
}
