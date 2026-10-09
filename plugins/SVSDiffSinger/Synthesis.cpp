#include "Synthesis.h"
#include "Speaker.h"
#include "WordTiming.h"
#include "svs_curve.hpp"
#include <cmath>
#include <numeric>
#include <set>
namespace diffsinger {
namespace {
Tensor floats(const std::vector<float>& values)
{
	return Tensor::make(ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, int64_t(values.size())}, values);
}
Tensor longs(const std::vector<int64_t>& values)
{
	return Tensor::make(ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, {1, int64_t(values.size())}, values);
}
Tensor booleans(const std::vector<uint8_t>& values, std::vector<int64_t> shape)
{
	return Tensor::make(ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL, std::move(shape), values);
}
void acceleration(CpuModel& model, Tensors& in, const StageConfig& config, int steps)
{
	if (model.accepts("steps"))
	{
		in["steps"] = Tensor::make<int64_t>(ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, {}, {steps});
	}
	if (model.accepts("depth"))
	{
		in["depth"]
			= Tensor::make<float>(ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {}, {config.values.value("max_depth", 1.f)});
	}
	if (model.accepts("speedup"))
	{
		int speed = std::max(1, 1000 / steps);
		while (1000 % speed)
		{
			--speed;
		}
		in["speedup"] = Tensor::make<int64_t>(ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, {}, {speed});
	}
}
class Curves
{
public:
	explicit Curves(const Json& input)
		: m_parameters(input.value("clipParameters", Json::object()))
	{
		const auto source = input.value("curves", Json::object());
		if (!source.is_object())
		{
			throw std::runtime_error("Curves must be an object");
		}
		for (auto it = source.begin(); it != source.end(); ++it)
		{
			if (it.key() != "svs.pitch" && it.key() != "svs.pitchDeviation" && it.key().rfind("diffsinger.", 0) != 0)
			{
				continue;
			}
			const auto& value = it.value();
			svs_sdk::Curve curve;
			const auto interpolation = value.value("interpolation", std::string("linear"));
			curve.interpolation = interpolation == "hermite" ? svs_sdk::Interpolation::Hermite
				: interpolation == "step"					 ? svs_sdk::Interpolation::Step
															 : svs_sdk::Interpolation::Linear;
			curve.constrainTangents = value.value("constrained", true);
			const auto points = value.value("points", Json::array());
			if (!points.is_array() || points.size() > 1000000)
			{
				throw std::runtime_error("Curve points exceed bound");
			}
			double previous = -INFINITY;
			for (const auto& point : points)
			{
				svs_sdk::CurvePoint p;
				p.tick = point.at("tick").get<double>();
				p.value = point.at("value").get<double>();
				if (!std::isfinite(p.tick) || !std::isfinite(p.value) || p.tick <= previous)
				{
					throw std::runtime_error("Invalid curve point");
				}
				previous = p.tick;
				p.tangentIn = point.value("in", 0.);
				p.tangentOut = point.value("out", 0.);
				p.automatic = point.value("automatic", true);
				p.breakAfter = point.value("breakAfter", false);
				const auto segment = point.value("segment", std::string());
				if (!segment.empty())
				{
					p.segmentInterpolation = segment == "hermite" ? 1 : segment == "step" ? 2 : 0;
				}
				curve.points.push_back(p);
			}
			for (const auto& gap : value.value("gaps", Json::array()))
			{
				curve.gaps.push_back({gap.at("start").get<double>(), gap.at("end").get<double>()});
			}
			m_curves.emplace(it.key(), std::move(curve));
		}
	}
	svs_sdk::CurveSample sample(const std::string& id, double tick) const
	{
		const auto found = m_curves.find(id);
		return found == m_curves.end() ? svs_sdk::CurveSample{} : found->second.evaluate(tick);
	}
	double value(const std::string& id, double tick, double fallback) const
	{
		const auto result = sample(id, tick);
		return result.covered ? result.value : m_parameters.value(id, fallback);
	}

private:
	Json m_parameters;
	std::map<std::string, svs_sdk::Curve> m_curves;
};
void speaker(CpuModel& model, Tensors& in, const VoicePackage& voice, const StageConfig& stage, const Json& params,
	int64_t frames)
{
	if (!model.accepts("spk_embed"))
	{
		return;
	}
	const auto embedding = speakerEmbedding(voice, stage, params);
	std::vector<float> values;
	values.reserve(size_t(frames) * embedding.size());
	for (int64_t f = 0; f < frames; ++f)
	{
		values.insert(values.end(), embedding.begin(), embedding.end());
	}
	in["spk_embed"]
		= Tensor::make<float>(ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, frames, int64_t(embedding.size())}, values);
}
Json feedbackCurve(
	const std::string& id, const std::string& unit, const std::vector<float>& values, const std::vector<double>& ticks)
{
	Json points = Json::array();
	for (size_t f = 0; f < values.size(); ++f)
	{
		points.push_back({{"tick", ticks[f]}, {"value", values[f]}});
	}
	return {{"id", id}, {"scope", "clip"}, {"type", "float"}, {"unit", unit}, {"mode", "absolute"},
		{"interpolation", "linear"}, {"points", points}};
}
}
Synthesis::Synthesis(Ort::Env& environment, std::shared_ptr<const VoicePackage> voice)
	: m_environment(environment)
	, m_voice(std::move(voice))
	, m_pronunciation(m_voice)
{
}
CpuModel& Synthesis::model(const std::string& stage, const std::string& role)
{
	const auto key = stage + "/" + role;
	auto& result = m_models[key];
	if (!result)
	{
		result = std::make_unique<CpuModel>(m_environment, m_voice->stages.at(stage).models.at(role), key, m_seed);
	}
	return *result;
}
void Synthesis::declareParameters(const VoicePackage& voice, Json& schema)
{
	schema["pitch"] = {{"input", "absolute"}, {"feedback", true}, {"unit", "semitone"}};
	auto parameter = [&](const std::string& id, const std::string& name, const std::string& unit, double minimum,
						 double maximum, double value, bool feedback) {
		Json p{{"id", id}, {"name", name}, {"group", "DiffSinger"}, {"scope", "clip"}, {"type", "float"},
			{"unit", unit}, {"min", minimum}, {"max", maximum}, {"default", value}, {"curve", true},
			{"interpolation", "linear"}, {"mode", "absolute"}};
		const bool offset = id.find(".offset") != std::string::npos;
		p["color"] = id.find("energy") != std::string::npos ? (offset ? "#F5A3C5" : "#E573A5")
			: id.find("breathiness") != std::string::npos	? (offset ? "#A3F5DB" : "#73E5C2")
			: id.find("voicing") != std::string::npos		? (offset ? "#DBF5A3" : "#C2E573")
			: id.find("tension") != std::string::npos		? (offset ? "#C5A3F5" : "#A573E5")
			: id == "diffsinger.gender"						? "#73B8E5"
			: id == "diffsinger.velocity"					? "#E5AD73"
															: "#E5DD73";
		schema["parameters"].push_back(p);
		if (feedback)
		{
			auto reference = p;
			const auto suffix = reference["name"].get<std::string>().find(" (absolute)");
			if (suffix != std::string::npos)
			{
				reference["name"] = reference["name"].get<std::string>().substr(0, suffix);
			}
			reference["writable"] = false;
			reference["color"] = id == "diffsinger.energy" ? "#E573A5"
				: id == "diffsinger.breathiness"		   ? "#73E5C2"
				: id == "diffsinger.voicing"			   ? "#C2E573"
														   : "#A573E5";
			schema["feedbackParameters"].push_back(reference);
		}
	};
	const auto& config = voice.stages.at("acoustic").values;
	for (const std::string name : {"energy", "breathiness", "voicing", "tension"})
	{
		if (config.value("use_" + name + "_embed", false))
		{
			const bool predicted
				= voice.stages.count("variance") && voice.stages.at("variance").values.value("predict_" + name, false);
			parameter("diffsinger." + name, name + " (absolute)", name == "tension" ? "ratio" : "dB",
				name == "tension" ? -10 : -96, name == "tension" ? 10 : 0, 0, predicted);
			parameter("diffsinger." + name + ".offset", name + " offset", name == "tension" ? "ratio" : "dB",
				name == "tension" ? -5 : -12, name == "tension" ? 5 : 12, 0, false);
		}
	}
	if (config.value("use_key_shift_embed", false))
	{
		parameter("diffsinger.gender", "Gender", "ratio", -1, 1, 0, false);
	}
	if (config.value("use_speed_embed", false))
	{
		parameter("diffsinger.velocity", "Velocity", "ratio", .2, 3, 1, false);
	}
	if (voice.stages.count("pitch") && voice.stages.at("pitch").values.value("use_expr", false))
	{
		parameter("diffsinger.expr", "Expressiveness", "ratio", 0, 1, 1, false);
	}
}
SynthesisResult Synthesis::render(const DurationPlan& plan, const std::vector<NoteInput>& notes, const Json& input,
	const svs_sdk::TempoMap& tempo, double origin, uint32_t rate, const std::atomic<bool>& cancelled)
{
	if (cancelled.load())
	{
		throw std::runtime_error("Cancelled");
	}
	if (rate < 8000 || rate > 192000)
	{
		throw std::runtime_error("Invalid output sample rate");
	}
	SynthesisResult result;
	if (plan.phones.empty())
	{
		return result;
	}
	const auto& acoustic = m_voice->stages.at("acoustic");
	const auto sourceRate = acoustic.values.at("sample_rate").get<int>();
	const auto hop = acoustic.values.at("hop_size").get<int>();
	if (sourceRate < 8000 || sourceRate > 192000 || hop < 1 || hop > 8192)
	{
		throw std::runtime_error("Invalid acoustic frame grid");
	}
	const double frameSeconds = double(hop) / sourceRate;
	auto phones = plan.phones;
	std::stable_sort(phones.begin(), phones.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
	const double start = phones.front().start - .5, end = phones.back().end + .5;
	const int64_t frames = int64_t(std::ceil((end - start) / frameSeconds));
	if (frames < 1 || frames > 60000)
	{
		throw std::runtime_error("Render frame count exceeds bound");
	}
	const auto outputBound = std::ceil(double(frames) * hop * rate / sourceRate);
	if (outputBound > 128 * 1024 * 1024 / 8)
	{
		throw std::runtime_error("PCM output exceeds bound before inference");
	}
	// Split long requests only at silence long enough to hold both 0.5 s pads.
	// Continuous phrases exceeding this context are explicitly unsupported.
	constexpr int64_t MaximumContextFrames = 4096;
	if (frames > MaximumContextFrames)
	{
		std::vector<DurationPlan> parts(1);
		bool restBoundary = false;
		for (const auto& phone : phones)
		{
			if (phone.symbol == "SP" && phone.end - phone.start >= 1.)
			{
				restBoundary = true;
				if (!parts.back().phones.empty())
					parts.emplace_back();
				continue;
			}
			if (!parts.back().phones.empty() && phone.start - parts.back().phones.back().end >= 1.)
			{
				restBoundary = true;
				parts.emplace_back();
			}
			parts.back().phones.push_back(phone);
		}
		if (parts.back().phones.empty())
			parts.pop_back();
		if (!restBoundary)
		{
			throw std::runtime_error(
				"Continuous phrase exceeds 4096 model frames; split at a rest of at least one second");
		}
		for (const auto& part : parts)
		{
			if (std::ceil((part.phones.back().end - part.phones.front().start + 1.) / frameSeconds)
				> MaximumContextFrames)
			{
				throw std::runtime_error(
					"Continuous phrase exceeds 4096 model frames; split at a rest of at least one second");
			}
		}
		result.start = start;
		result.stereo.resize(size_t(outputBound) * 2, 0);
		result.feedback["pitch"] = Json::array();
		result.feedback["curves"] = Json::object();
		for (const auto& part : parts)
		{
			const auto partStart = part.phones.front().start, partEnd = part.phones.back().end;
			std::vector<NoteInput> partNotes;
			for (const auto& note : notes)
			{
				if (note.start + note.duration <= partStart + 1e-6 || note.start >= partEnd - 1e-6)
					continue;
				// A long rest crossing a chunk's padding is represented by
				// the padding mask; do not add a zero-duration extra note.
				if (note.lyric.empty()
					&& (note.start < partStart - 1e-6 || note.start + note.duration > partEnd + 1e-6))
					continue;
				partNotes.push_back(note);
			}
			auto chunk = render(part, partNotes, input, tempo, origin, rate, cancelled);
			const size_t offset = size_t(std::llround((chunk.start - start) * rate)) * 2;
			if (offset > 128 * 1024 * 1024 / 4 || chunk.stereo.size() > 128 * 1024 * 1024 / 4 - offset)
			{
				throw std::runtime_error("Chunk PCM placement exceeds output bound");
			}
			if (offset + chunk.stereo.size() > result.stereo.size())
				result.stereo.resize(offset + chunk.stereo.size(), 0);
			std::copy(chunk.stereo.begin(), chunk.stereo.end(), result.stereo.begin() + offset);
			for (const auto& sample : chunk.feedback.at("pitch"))
				result.feedback["pitch"].push_back(sample);
			for (auto it = chunk.feedback.at("curves").begin(); it != chunk.feedback.at("curves").end(); ++it)
			{
				auto& curve = result.feedback["curves"][it.key()];
				if (curve.is_null())
				{
					curve = it.value();
					continue;
				}
				// ordered_json stores object members in a vector. Add fields
				// before keeping a reference to the points member.
				if (!curve.contains("gaps"))
					curve["gaps"] = Json::array();
				auto& points = curve["points"];
				const auto& next = it.value().at("points");
				if (!points.empty() && !next.empty())
				{
					curve["gaps"].push_back({{"start", points.back().at("tick")}, {"end", next.front().at("tick")}});
				}
				for (const auto& point : next)
					points.push_back(point);
			}
		}
		return result;
	}
	result.start = start;
	std::vector<Phone> spans;
	spans.push_back({"SP", phones.front().language, "", start, phones.front().start, phones.front().pitch, false});
	for (const auto& phone : phones)
	{
		if (phone.start < spans.back().end - 1e-6)
		{
			throw std::runtime_error("Overlapping phonemes at note " + phone.noteId);
		}
		if (phone.start > spans.back().end + 1e-6)
		{
			spans.push_back({"SP", phone.language, "", spans.back().end, phone.start, phone.pitch, false});
		}
		spans.push_back(phone);
	}
	spans.push_back({"SP", phones.back().language, "", phones.back().end, start + frames * frameSeconds,
		phones.back().pitch, false});
	std::vector<int64_t> durations;
	int64_t previous = 0;
	for (const auto& span : spans)
	{
		const auto boundary
			= std::clamp(int64_t(std::floor((span.end - start) / frameSeconds + 1e-7)), previous, frames);
		durations.push_back(boundary - previous);
		previous = boundary;
	}
	durations.back() += frames - previous;
	std::vector<double> ticks(size_t(frames), 0);
	for (int64_t f = 0; f < frames; ++f)
	{
		ticks[size_t(f)] = tempo.tickAt(tempo.secondsAt(origin) + start + f * frameSeconds) - origin;
	}
	const Curves curves(input);
	const auto parameters = input.value("trackParameters", Json::object());
	const int steps = input.value("engineSettings", Json::object()).value("diffsinger.renderSteps", 20);
	if (steps < 1 || steps > 100)
	{
		throw std::runtime_error("Invalid rendering steps");
	}
	const auto seedValue = input.value("seed", Json(1));
	if (!seedValue.is_number_unsigned() && !seedValue.is_number_integer())
	{
		throw std::runtime_error("Seed must be an integer");
	}
	const auto seed = seedValue.get<int64_t>();
	if (seed < 0 || seed > UINT32_MAX)
	{
		throw std::runtime_error("Seed exceeds uint32 range");
	}
	if (m_seed != uint32_t(seed))
	{
		m_models.clear();
		m_seed = uint32_t(seed);
	}
	TensorCache cache(fs::u8path(input.value("cacheDirectory", std::string())));
	auto run = [&](const std::string& stage, const std::string& role, const Tensors& inputs) {
		auto& target = model(stage, role);
		const auto key = TensorCache::key("CPU/ORT1.23.0/native.v2/seed=" + std::to_string(m_seed) + "/pinyin621f8ca9/"
				+ m_voice->fingerprint + "/" + stage + "/" + role,
			inputs);
		Tensors out;
		if (cancelled.load())
		{
			throw std::runtime_error("Cancelled");
		}
		if (!cache.load(key, out))
		{
			out = target.run(inputs, cancelled);
			cache.save(key, out);
		}
		if (cancelled.load())
		{
			throw std::runtime_error("Cancelled");
		}
		return out;
	};
	auto tokens = [&](const std::string& stage) {
		std::vector<int64_t> values;
		for (const auto& span : spans)
		{
			const auto mapped = m_pronunciation.map({span.symbol}, span.language, stage);
			values.push_back(m_voice->stages.at(stage).phonemes.at(mapped.at(0)).get<int64_t>());
		}
		return values;
	};
	auto linguistic = [&](const std::string& stage) {
		Tensors in{{"tokens", longs(tokens(stage))}};
		auto& target = model(stage, "linguistic");
		if (target.accepts("word_div") || target.accepts("word_dur"))
		{
			if (!target.accepts("word_div") || !target.accepts("word_dur"))
			{
				throw std::runtime_error(stage + " linguistic requires both word_div and word_dur");
			}
			std::vector<bool> vowels;
			for (const auto& span : spans)
			{
				const auto symbol = m_pronunciation.map({span.symbol}, span.language, stage).at(0);
				vowels.push_back(!span.noteId.empty() && m_pronunciation.type(symbol, span.language, stage) == "vowel");
			}
			const auto timing = wordTiming(durations, vowels);
			in["word_div"] = longs(timing.first);
			in["word_dur"] = longs(timing.second);
		}
		else
		{
			in["ph_dur"] = longs(durations);
		}
		if (target.accepts("languages"))
		{
			std::vector<int64_t> language;
			for (const auto& span : spans)
			{
				language.push_back(
					span.symbol == "SP" ? 0 : m_voice->stages.at(stage).languages.at(span.language).get<int64_t>());
			}
			in["languages"] = longs(language);
		}
		return run(stage, "linguistic", in);
	};
	std::vector<float> pitch(size_t(frames), 60.f);
	std::vector<NoteInput> ordered = notes;
	std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
	std::vector<float> noteMidi;
	std::vector<int64_t> noteDuration;
	std::vector<uint8_t> noteRest;
	int64_t noteFrame = 0;
	auto addNote = [&](double until, float midi, bool rest) {
		const auto boundary = std::clamp(int64_t(std::floor((until - start) / frameSeconds + 1e-7)), noteFrame, frames);
		noteMidi.push_back(midi);
		noteDuration.push_back(boundary - noteFrame);
		noteRest.push_back(rest ? 1 : 0);
		for (auto f = noteFrame; f < boundary; ++f)
		{
			pitch[size_t(f)] = midi;
		}
		noteFrame = boundary;
	};
	for (size_t n = 0; n < ordered.size(); ++n)
	{
		const auto& note = ordered[n];
		if (note.start > start + noteFrame * frameSeconds)
		{
			addNote(note.start, float(note.pitch), true);
		}
		const auto finish = n + 1 < ordered.size() ? std::min(note.start + note.duration, ordered[n + 1].start)
												   : note.start + note.duration;
		addNote(finish, float(note.pitch), note.lyric.empty());
	}
	addNote(start + frames * frameSeconds, noteMidi.empty() ? 60.f : noteMidi.back(), true);
	if (m_voice->stages.count("pitch"))
	{
		const auto encoded = linguistic("pitch");
		auto& target = model("pitch", "pitch");
		Tensors in{{"encoder_out", encoded.at("encoder_out")}, {"ph_dur", longs(durations)},
			{"note_midi", floats(noteMidi)}, {"note_dur", longs(noteDuration)},
			{"pitch", floats(std::vector<float>(size_t(frames), 60.f))},
			{"retake", booleans(std::vector<uint8_t>(size_t(frames), 1), {1, frames})}};
		if (target.accepts("note_rest"))
		{
			in["note_rest"] = booleans(noteRest, {1, int64_t(noteRest.size())});
		}
		if (target.accepts("expr"))
		{
			std::vector<float> expr(size_t(frames), 1);
			for (size_t f = 0; f < expr.size(); ++f)
			{
				expr[f] = float(std::clamp(curves.value("diffsinger.expr", ticks[f], 1), 0., 1.));
			}
			in["expr"] = floats(expr);
		}
		acceleration(target, in, m_voice->stages.at("pitch"), steps);
		speaker(target, in, *m_voice, m_voice->stages.at("pitch"), parameters, frames);
		pitch = run("pitch", "pitch", in).at("pitch_pred").values<float>();
	}
	if (pitch.size() != size_t(frames))
	{
		throw std::runtime_error("Pitch output frame mismatch");
	}
	std::vector<float> f0(size_t(frames), 0);
	for (size_t f = 0; f < pitch.size(); ++f)
	{
		const auto drawn = curves.sample("svs.pitch", ticks[f]);
		if (drawn.covered)
		{
			pitch[f] = float(drawn.value);
		}
		const auto deviation = curves.sample("svs.pitchDeviation", ticks[f]);
		if (deviation.covered)
		{
			pitch[f] += float(deviation.value);
		}
		pitch[f] = std::clamp(pitch[f], 0.f, 127.f);
		f0[f] = 440.f * std::pow(2.f, (pitch[f] - 69.f) / 12.f);
	}
	std::set<std::string> predicted;
	std::map<std::string, std::vector<float>> variance;
	for (const std::string name : {"energy", "breathiness", "voicing", "tension"})
	{
		variance[name] = std::vector<float>(size_t(frames), 0);
	}
	auto& ac = model("acoustic", "acoustic");
	// A package may contain predictors whose outputs its acoustic model does
	// not consume. Do not load or execute an unused optional variance stage.
	const bool needsVariance
		= std::any_of(variance.begin(), variance.end(), [&](const auto& item) { return ac.accepts(item.first); });
	if (needsVariance && m_voice->stages.count("variance"))
	{
		const auto& config = m_voice->stages.at("variance");
		const auto encoded = linguistic("variance");
		auto& target = model("variance", "variance");
		Tensors in{{"encoder_out", encoded.at("encoder_out")}, {"ph_dur", longs(durations)}, {"pitch", floats(pitch)}};
		int64_t channels = 0;
		for (auto& item : variance)
		{
			if (config.values.value("predict_" + item.first, false))
			{
				in[item.first] = floats(item.second);
				++channels;
			}
		}
		in["retake"] = booleans(std::vector<uint8_t>(size_t(frames * channels), 1), {1, frames, channels});
		acceleration(target, in, config, steps);
		speaker(target, in, *m_voice, config, parameters, frames);
		const auto out = run("variance", "variance", in);
		for (auto& item : variance)
		{
			if (out.count(item.first + "_pred"))
			{
				predicted.insert(item.first);
				item.second = out.at(item.first + "_pred").values<float>();
				if (item.second.size() != size_t(frames))
				{
					throw std::runtime_error("Variance output frame mismatch");
				}
			}
		}
	}
	Tensors in{{"tokens", longs(tokens("acoustic"))}, {"durations", longs(durations)}, {"f0", floats(f0)}};
	result.feedback["curves"] = Json::object();
	for (auto& item : variance)
	{
		if (!ac.accepts(item.first))
		{
			continue;
		}
		for (size_t f = 0; f < item.second.size(); ++f)
		{
			item.second[f] += float(curves.value("diffsinger." + item.first + ".offset", ticks[f], 0));
			const auto drawn = curves.sample("diffsinger." + item.first, ticks[f]);
			if (drawn.covered)
			{
				item.second[f] = float(drawn.value);
			}
			else if (input.value("clipParameters", Json::object()).contains("diffsinger." + item.first))
			{
				item.second[f] = float(curves.value("diffsinger." + item.first, ticks[f], item.second[f]));
			}
			item.second[f] = std::clamp(
				item.second[f], item.first == "tension" ? -10.f : -96.f, item.first == "tension" ? 10.f : 0.f);
		}
		in[item.first] = floats(item.second);
		if (predicted.count(item.first))
			result.feedback["curves"]["diffsinger." + item.first] = feedbackCurve(
				"diffsinger." + item.first, item.first == "tension" ? "ratio" : "dB", item.second, ticks);
	}
	for (const std::string name : {"gender", "velocity"})
	{
		if (!ac.accepts(name))
		{
			continue;
		}
		std::vector<float> values(size_t(frames), name == "velocity" ? 1.f : 0.f);
		for (size_t f = 0; f < values.size(); ++f)
		{
			const double value = curves.value("diffsinger." + name, ticks[f], values[f]);
			if (name == "velocity")
			{
				values[f] = float(std::clamp(value, .2, 3.));
			}
			else
			{
				const double gender = std::clamp(value, -1., 1.);
				const auto range = acoustic.values.value("augmentation_args", Json::object())
									   .value("random_pitch_shifting", Json::object())
									   .value("range", Json::array({-12, 12}));
				values[f] = float(gender >= 0 ? gender * range[0].get<double>() : -gender * range[1].get<double>());
			}
		}
		in[name] = floats(values);
	}
	acceleration(ac, in, acoustic, steps);
	speaker(ac, in, *m_voice, acoustic, parameters, frames);
	const auto mel = run("acoustic", "acoustic", in).at("mel");
	if (mel.dimensions != std::vector<int64_t>({1, frames, acoustic.values.at("num_mel_bins").get<int64_t>()}))
	{
		throw std::runtime_error("Acoustic mel shape mismatch");
	}
	const auto waveform = run("vocoder", "model", {{"mel", mel}, {"f0", floats(f0)}}).at("waveform").values<float>();
	if (waveform.size() != size_t(frames * hop))
	{
		throw std::runtime_error("Vocoder sample count mismatch");
	}
	const size_t outputFrames = size_t(std::ceil(double(waveform.size()) * rate / sourceRate));
	if (outputFrames > 128 * 1024 * 1024 / 8)
	{
		throw std::runtime_error("PCM output exceeds bound");
	}
	result.stereo.resize(outputFrames * 2);
	for (size_t f = 0; f < outputFrames; ++f)
	{
		if ((f % 4096) == 0 && cancelled.load())
		{
			throw std::runtime_error("Cancelled");
		}
		const double source = double(f) * sourceRate / rate;
		const size_t left = std::min(size_t(source), waveform.size() - 1),
					 right = std::min(left + 1, waveform.size() - 1);
		const float value = float(waveform[left] + (waveform[right] - waveform[left]) * (source - left));
		result.stereo[f * 2] = result.stereo[f * 2 + 1] = value;
	}
	result.feedback["pitch"] = Json::array();
	size_t noteIndex = 0;
	for (size_t f = 0; f < pitch.size() && !ordered.empty(); ++f)
	{
		const double seconds = start + f * frameSeconds;
		while (noteIndex + 1 < ordered.size() && seconds >= ordered[noteIndex + 1].start)
		{
			++noteIndex;
		}
		const auto& note = ordered[noteIndex];
		if (seconds < note.start || seconds >= note.start + note.duration || note.lyric.empty())
		{
			continue;
		}
		result.feedback["pitch"].push_back({{"noteId", note.id}, {"startSeconds", seconds},
			{"durationSeconds", std::min(frameSeconds, note.start + note.duration - seconds)}, {"value", pitch[f]}});
	}
	return result;
}
}
