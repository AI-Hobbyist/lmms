#ifndef DIFFSINGER_INFERENCE_COMPARISON_H
#define DIFFSINGER_INFERENCE_COMPARISON_H

#include <cmath>
#include <iostream>

#include "Synthesis.h"

namespace diffsinger {
// B0 limits are fixed in sdk/svs/docs/compute.md. Replay uses identical tensors and seed.
class InferenceComparison
{
public:
	InferenceComparison(Ort::Env& environment, const VoicePackage& voice, const std::atomic<bool>& cancelled)
		: m_environment(environment)
		, m_voice(voice)
		, m_cancelled(cancelled)
	{
		m_previous
			= exchangeInferenceObserver([this](const fs::path& path, const std::string& stage, uint32_t seed,
											   const Tensors& inputs, const Tensors& outputs, const Json& execution) {
				  auto observer = exchangeInferenceObserver({});
				  try
				  {
					  const auto replayStart = std::chrono::steady_clock::now();
					  CpuModel cpu(m_environment, path, stage, seed);
					  compare(stage, outputs, cpu.run(inputs, m_cancelled));
					  m_replayMilliseconds
						  += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - replayStart)
								 .count();
					  ++m_count;
					  if (execution.value("effectiveBackend", "") == "directml")
					  {
						  ensure(execution.value("providerEvidence", Json::object()).value("dmlNodes", 0) > 0,
								 "Missing actual DML nodes: " + stage);
					  }
				  }
				  catch (...)
				  {
					  exchangeInferenceObserver(std::move(observer));
					  throw;
				  }
				  exchangeInferenceObserver(std::move(observer));
			  });
	}
	~InferenceComparison() { exchangeInferenceObserver(std::move(m_previous)); }
	size_t count() const { return m_count; }
	double replayMilliseconds() const { return m_replayMilliseconds; }
	static void ensure(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}
	static void pcm(const SynthesisResult& gpu, const SynthesisResult& cpu)
	{
		ensure(gpu.stereo.size() == cpu.stereo.size(), "Full chain PCM shape differs");
		std::cout << "COMPARE origin cpu=" << cpu.start << " dml=" << gpu.start
				  << " deltaSeconds=" << std::abs(gpu.start - cpu.start) << std::endl;
		// The host places PCM at its rounded output sample, including negative preroll.
		ensure(std::llround(gpu.start * 48000) == std::llround(cpu.start * 48000),
			   "Full chain PCM sample origin differs");
		double reference = 0, actual = 0, difference = 0, cross = 0;
		for (size_t i = 0; i < cpu.stereo.size(); ++i)
		{
			const double a = gpu.stereo[i], b = cpu.stereo[i];
			ensure(std::isfinite(a) && std::isfinite(b), "Non-finite full chain PCM");
			reference += b * b;
			actual += a * a;
			difference += (a - b) * (a - b);
			cross += a * b;
		}
		ensure(reference > 0 && actual > 0, "Silent full chain comparison");
		const double correlation = cross / std::sqrt(reference * actual);
		const double normalized = std::sqrt(difference / reference);
		const double level = std::abs(10 * std::log10(actual / reference));
		std::cout << "COMPARE PCM corr=" << correlation << " normalizedRms=" << normalized << " levelDb=" << level
				  << std::endl;
		ensure(correlation >= .80 && normalized <= .50 && level <= 3, "Full chain exceeds frozen B0 tolerances");
		const auto& actualPitch = gpu.feedback.at("pitch");
		const auto& expectedPitch = cpu.feedback.at("pitch");
		ensure(actualPitch.size() == expectedPitch.size(), "Pitch feedback frame count differs");
		double pitchSquared = 0, pitchMaximum = 0;
		for (size_t i = 0; i < actualPitch.size(); ++i)
		{
			ensure(actualPitch[i].at("noteId") == expectedPitch[i].at("noteId"),
				   "Pitch feedback note identity differs");
			const double error
				= std::abs(actualPitch[i].at("value").get<double>() - expectedPitch[i].at("value").get<double>());
			ensure(std::isfinite(error), "Non-finite pitch feedback");
			pitchSquared += error * error;
			pitchMaximum = std::max(pitchMaximum, error);
		}
		ensure(actualPitch.empty() || (std::sqrt(pitchSquared / actualPitch.size()) <= .25 && pitchMaximum <= 1),
			   "Full chain pitch feedback exceeds B0 tolerance");
		const auto& actualCurves = gpu.feedback.at("curves");
		const auto& expectedCurves = cpu.feedback.at("curves");
		ensure(actualCurves.size() == expectedCurves.size(), "Variance feedback capability differs");
		for (auto curve = expectedCurves.begin(); curve != expectedCurves.end(); ++curve)
		{
			const auto& a = actualCurves.at(curve.key()).at("points");
			const auto& b = curve.value().at("points");
			ensure(a.size() == b.size(), "Variance feedback frame count differs");
			const double range = curve.key().find("tension") != std::string::npos ? 20 : 96;
			double squared = 0, maximum = 0;
			for (size_t i = 0; i < a.size(); ++i)
			{
				const double error = std::abs(a[i].at("value").get<double>() - b[i].at("value").get<double>()) / range;
				ensure(std::isfinite(error), "Non-finite variance feedback");
				squared += error * error;
				maximum = std::max(maximum, error);
			}
			ensure(a.empty() || (std::sqrt(squared / a.size()) <= .03 && maximum <= .15),
				   "Full chain variance feedback exceeds B0 tolerance");
		}
	}

private:
	void compare(const std::string& stage, const Tensors& actual, const Tensors& reference)
	{
		ensure(actual.size() == reference.size(), "Output count differs: " + stage);
		for (const auto& item : reference)
		{
			const auto& a = actual.at(item.first);
			const auto& b = item.second;
			ensure(a.type == b.type && a.dimensions == b.dimensions && a.bytes.size() == b.bytes.size(),
				   "Tensor shape/type differs: " + stage + "/" + item.first);
			if (a.type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
			{
				ensure(a.bytes == b.bytes, "Integer/bool tensor differs: " + stage + "/" + item.first);
				continue;
			}
			const auto values = a.values<float>(), expected = b.values<float>();
			double squared = 0, maximum = 0;
			const bool linguistic = stage.find("linguistic") != std::string::npos;
			const bool duration = stage == "duration";
			const bool pitch = stage == "pitch/pitch";
			const bool variance = stage == "variance/variance";
			const bool vocoder = stage.rfind("vocoder/", 0) == 0;
			const double scale = duration ? m_voice.stages.at("duration").values.value("hop_size", 512.)
					/ m_voice.stages.at("duration").values.value("sample_rate", 44100.)
				: variance ? (item.first.find("tension") != std::string::npos ? 20. : 96.)
						   : 1.;
			for (size_t i = 0; i < values.size(); ++i)
			{
				ensure(std::isfinite(values[i]) && std::isfinite(expected[i]), "Non-finite tensor: " + stage);
				const double difference = std::abs(double(values[i]) - expected[i]);
				if (linguistic || duration)
				{
					ensure(difference * scale <= (duration ? .05 : .002) + .02 * std::abs(expected[i] * scale),
						   "Absolute/relative tolerance failed: " + stage + "/" + item.first);
				}
				const double normalized = variance ? difference / scale : difference;
				squared += normalized * normalized;
				maximum = std::max(maximum, normalized);
			}
			const double rms = values.empty() ? 0 : std::sqrt(squared / values.size());
			std::cout << "COMPARE " << stage << "/" << item.first << " rms=" << rms << " max=" << maximum << std::endl;
			if (pitch)
			{
				ensure(rms <= .25 && maximum <= 1, "Pitch tolerance failed");
			}
			if (variance)
			{
				ensure(rms <= .03 && maximum <= .15, "Variance tolerance failed");
			}
			if (stage.rfind("acoustic/", 0) == 0)
			{
				ensure(rms <= .15 && maximum <= 1, "Mel tolerance failed");
			}
			if (vocoder)
			{
				ensure(rms <= .02 && maximum <= .15, "Isolated vocoder tolerance failed");
			}
		}
	}
	Ort::Env& m_environment;
	const VoicePackage& m_voice;
	const std::atomic<bool>& m_cancelled;
	InferenceObserver m_previous;
	size_t m_count = 0;
	double m_replayMilliseconds = 0;
};
} // namespace diffsinger
#endif
