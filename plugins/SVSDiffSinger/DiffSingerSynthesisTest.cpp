#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <set>

#include "Hash.h"
#include "InferenceComparison.h"
#include "Synthesis.h"
#include "WordTiming.h"
using namespace diffsinger;
namespace {
void require(bool value, const std::string& message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}
void routingFaults(Ort::Env& env, const fs::path& path, const std::string& device)
{
	std::atomic<bool> cancelled{false};
	const auto values = Tensor::make<float>(ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, 4}, {1, 2, 3, 4});
	const auto second = Tensor::make<float>(ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, 4}, {4, 3, 2, 1});
	const Tensors inputs{{"x", values}, {"y", second}};
	const Json policy{{"effectiveBackend", "directml"}, {"effectiveDevice", device}};
	struct ObserverGuard
	{
		InferenceObserver previous = exchangeInferenceObserver({});
		~ObserverGuard() { exchangeInferenceObserver(std::move(previous)); }
	} observerGuard;
	{
		auto missing = policy;
		missing["effectiveDevice"] = "dxgi:ffffffff:ffffffff";
		CpuModel model(env, path, "fixture", 1, missing);
		require(model.run(inputs, cancelled).at("z").values<float>() == std::vector<float>(4, 5),
				"Missing-device stage fallback output differs");
		require(model.execution().at("effectiveBackend") == "cpu" && !model.execution().at("fallbackReason").empty(),
				"Missing-device fallback identity/reason missing");
	}
	for (const std::string reason : {"GPU OOM 0x8007000E", "Unsupported DML graph", "GPU initialization failed"})
	{
		CpuModel model(env, path, "fixture", 1, policy);
		int gpuRuns = 0, cpuRuns = 0;
		exchangeInferenceObserver(
			[&](const fs::path&, const std::string&, uint32_t, const Tensors&, const Tensors&, const Json& execution) {
				if (execution.at("effectiveBackend") == "directml")
				{
					++gpuRuns;
					throw svs_compute::Error(SVSC_BACKEND_FAILURE, reason);
				}
				++cpuRuns;
			});
		require(model.run(inputs, cancelled).at("z").values<float>() == std::vector<float>(4, 5),
				"Backend failure whole-stage CPU replay differs");
		require(gpuRuns == 1 && cpuRuns == 1 && model.execution().at("fallbackReason") == reason,
				"Backend failure did not replay exactly once");
		exchangeInferenceObserver({});
		model.beginRequest();
		require(model.run(inputs, cancelled).at("z").values<float>() == std::vector<float>(4, 5)
					&& model.execution().at("effectiveBackend") == "directml",
				"Fresh request permanently stayed on CPU");
		std::cout << "PASS controlled backend error / one CPU replay / next-request DML recovery: " << reason
				  << std::endl;
	}
	{
		CpuModel model(env, path, "fixture", 1, policy);
		int gpuRuns = 0, cpuRuns = 0;
		exchangeInferenceObserver(
			[&](const fs::path&, const std::string&, uint32_t, const Tensors&, const Tensors&, const Json& execution) {
				if (execution.at("effectiveBackend") == "directml")
				{
					++gpuRuns;
				}
				else
				{
					++cpuRuns;
				}
				throw svs_compute::Error(SVSC_BACKEND_FAILURE, "Both backend error fixture");
			});
		try
		{
			model.run(inputs, cancelled);
			throw std::runtime_error("CPU failure accepted");
		}
		catch (const std::exception& error)
		{
			require(std::string(error.what()).find("Both backend error fixture") != std::string::npos,
					"CPU failure lost diagnostic");
		}
		require(gpuRuns == 1 && cpuRuns == 1, "CPU failure triggered repeated fallback");
		exchangeInferenceObserver({});
	}
	{
		CpuModel model(env, path, "fixture", 1, policy);
		auto bad = inputs;
		bad.at("x").dimensions = {4, 1};
		try
		{
			model.run(bad, cancelled);
			throw std::runtime_error("Bad shape accepted");
		}
		catch (const std::exception& error)
		{
			require(std::string(error.what()).find("Bad shape accepted") == std::string::npos, "Invalid data accepted");
		}
		require(model.computeIdentity().rfind("directml/", 0) == 0, "Invalid data caused CPU fallback");
		cancelled = true;
		try
		{
			model.run(inputs, cancelled);
			throw std::runtime_error("Cancellation accepted");
		}
		catch (const std::exception& error)
		{
			require(std::string(error.what()).find("Cancelled") != std::string::npos, "Cancellation lost diagnostic");
		}
		require(model.computeIdentity().rfind("directml/", 0) == 0, "Cancellation caused CPU fallback");
		cancelled = false;
	}
	{
		CpuModel model(env, path, "fixture", 1, policy);
		int cpuRuns = 0;
		exchangeInferenceObserver(
			[&](const fs::path&, const std::string&, uint32_t, const Tensors&, const Tensors&, const Json& execution) {
				if (execution.at("effectiveBackend") == "cpu")
				{
					++cpuRuns;
				}
				cancelled = true;
				throw svs_compute::Error(SVSC_BACKEND_FAILURE, "GPU error concurrent with cancellation");
			});
		try
		{
			model.run(inputs, cancelled);
			throw std::runtime_error("Concurrent cancellation accepted");
		}
		catch (const std::exception& error)
		{
			require(std::string(error.what()).find("Cancelled") != std::string::npos,
					"Concurrent cancellation lost diagnostic");
		}
		require(cpuRuns == 0, "Cancelled failed GPU stage launched CPU replay");
		cancelled = false;
		exchangeInferenceObserver({});
	}
	for (int batch = 0; batch < 10; ++batch)
	{
		std::vector<std::future<bool>> runs;
		for (int i = 0; i < 4; ++i)
		{
			runs.push_back(std::async(std::launch::async, [&] {
				CpuModel model(env, path, "fixture", 1, policy);
				try
				{
					require(model.run(inputs, cancelled).at("z").values<float>() == std::vector<float>(4, 5),
							"Concurrent same-device output differs");
					return true;
				}
				catch (const std::exception& error)
				{
					require(std::string(error.what()).find("Context shared-buffer/result budget exceeded")
								!= std::string::npos,
							"Concurrent run failed outside the declared allocation budget");
					return false;
				}
			}));
		}
		int successful = 0;
		for (auto& run : runs)
		{
			successful += run.get() ? 1 : 0;
		}
		require(successful > 0, "Concurrent batch made no progress");
		CpuModel recovered(env, path, "fixture", 1, policy);
		require(recovered.run(inputs, cancelled).at("z").values<float>() == std::vector<float>(4, 5),
				"Concurrent batch leaked its allocation budget");
		std::cout << "PASS bounded batch " << batch << " successful=" << successful << "/4; budget recovered"
				  << std::endl;
	}
	std::cout << "PASS actual missing LUID init / bounded CPU retry failure / invalid tensor / cancellation / 10x4 "
				 "same-device sessions"
			  << std::endl;
}
void wordTimingFixture()
{
	const std::vector<int64_t> durations{10, 3, 20, 4, 21, 7, 6, 10};
	const auto grouped = wordTiming(durations, {false, false, true, false, true, false, true, false});
	require(grouped.first == std::vector<int64_t>({2, 2, 2, 2}),
			"Word divisions differ from OpenUtau vowel boundaries");
	require(grouped.second == std::vector<int64_t>({13, 24, 28, 16}), "Word durations lost padding/gap/AP frames");
	const auto noVowels = wordTiming(durations, std::vector<bool>(8, false));
	require(noVowels.first == std::vector<int64_t>({6, 2}) && noVowels.second == std::vector<int64_t>({65, 16}),
			"Consonant-only fallback differs from OpenUtau");
	const auto zero = wordTiming({0, 0, 10}, {false, true, false});
	require(zero.first == std::vector<int64_t>({1, 2}) && zero.second == std::vector<int64_t>({0, 10}),
			"Zero-frame phone lost alignment");
	try
	{
		wordTiming({1, 2, 3}, {false});
		throw std::runtime_error("Word duration mismatch accepted");
	}
	catch (const std::exception& error)
	{
		require(std::string(error.what()).find("matching padded") != std::string::npos,
				"Unexpected word timing validation error");
	}
	std::cout
		<< "PASS OpenUtau word boundaries / grouped frames / padding / gaps / AP / no-vowel / zero-frame / mismatch"
		<< std::endl;
}
void wordModels(Ort::Env& env, const std::shared_ptr<const VoicePackage>& voice)
{
	std::atomic<bool> cancel{false};
	Pronunciation pronunciation(voice);
	const std::vector<std::string> symbols{"SP", "l", "a", "l", "a", "SP", "AP", "SP"};
	const std::vector<int64_t> durations{10, 3, 20, 4, 21, 7, 6, 10};
	const auto timing = wordTiming(durations, {false, false, true, false, true, false, true, false});
	auto longs = [](const std::vector<int64_t>& values) {
		return Tensor::make<int64_t>(ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, {1, int64_t(values.size())}, values);
	};
	for (const std::string stage : {"duration", "variance"})
	{
		const auto& config = voice->stages.at(stage);
		CpuModel encoder(env, config.models.at("linguistic"), stage + "/linguistic");
		require(encoder.accepts("word_div") && encoder.accepts("word_dur") && !encoder.accepts("ph_dur"),
				"Expected actual word-mode ONNX encoder");
		std::vector<int64_t> tokens;
		for (const auto& symbol : pronunciation.map(symbols, "zh", stage))
			tokens.push_back(config.phonemes.at(symbol).get<int64_t>());
		Tensors inputs{
			{"tokens", longs(tokens)}, {"word_div", longs(timing.first)}, {"word_dur", longs(timing.second)}};
		if (encoder.accepts("languages"))
			inputs["languages"] = longs(std::vector<int64_t>(tokens.size(), config.languages.at("zh").get<int64_t>()));
		const auto encoded = encoder.run(inputs, cancel);
		require(encoded.at("encoder_out").dimensions.at(1) == int64_t(tokens.size()),
				"Word encoder lost token alignment");
		for (const auto value : encoded.at("encoder_out").values<float>())
			require(std::isfinite(value), "Non-finite word encoder output");
		if (stage == "variance")
		{
			CpuModel predictor(env, config.models.at("variance"), "variance/variance");
			auto floats = [](float value) {
				return Tensor::make<float>(ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, 81}, std::vector<float>(81, value));
			};
			const auto out = predictor.run(
				{{"encoder_out", encoded.at("encoder_out")},
				 {"ph_dur", longs(durations)},
				 {"pitch", floats(60)},
				 {"breathiness", floats(0)},
				 {"retake",
				  Tensor::make<uint8_t>(ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL, {1, 81, 1}, std::vector<uint8_t>(81, 1))},
				 {"steps", Tensor::make<int64_t>(ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, {}, {5})}},
				cancel);
			const auto values = out.at("breathiness_pred").values<float>();
			require(values.size() == 81, "Word-mode variance output frame mismatch");
			for (const auto value : values)
				require(std::isfinite(value), "Non-finite word variance output");
		}
		std::cout << "PASS actual " << stage << " word-mode ONNX inference" << std::endl;
	}
}
std::string digest(const std::vector<float>& audio)
{
	Sha256 hash;
	hash.add(audio.data(), audio.size() * sizeof(float));
	return hash.finish();
}
void audition(const fs::path& path, const std::vector<float>& audio)
{
	std::ostringstream out(std::ios::out | std::ios::binary);
	auto u16 = [&](uint16_t value) {
		out.put(char(value));
		out.put(char(value >> 8));
	};
	auto u32 = [&](uint32_t value) {
		u16(uint16_t(value));
		u16(uint16_t(value >> 16));
	};
	out.write("RIFF", 4);
	u32(uint32_t(36 + audio.size() * 2));
	out.write("WAVEfmt ", 8);
	u32(16);
	u16(1);
	u16(2);
	u32(48000);
	u32(48000 * 4);
	u16(4);
	u16(16);
	out.write("data", 4);
	u32(uint32_t(audio.size() * 2));
	for (float value : audio)
	{
		u16(uint16_t(int16_t(std::lround(std::clamp(value, -1.f, 1.f) * 32767))));
	}
	const auto bytes = out.str(), sha = hashText(bytes);
	const auto destination = path.parent_path() / (sha + ".wav");
	if (!fs::exists(destination))
	{
		std::ofstream file(destination, std::ios::binary);
		file.write(bytes.data(), std::streamsize(bytes.size()));
		if (!file)
		{
			throw std::runtime_error("Cannot save audition WAV");
		}
	}
	require(hashFile(destination) == sha, "Audition filename does not match file SHA-256");
	std::cout << "AUDITION " << path.stem().u8string() << " " << destination.u8string() << std::endl;
}
void check(const SynthesisResult& result)
{
	require(result.stereo.size() > 48000, "PCM too short");
	double energy = 0;
	for (float value : result.stereo)
	{
		require(std::isfinite(value), "Non-finite PCM");
		energy += double(value) * value;
	}
	require(energy / result.stereo.size() > 1e-10, "Silent PCM");
	const auto extrema = std::minmax_element(result.stereo.begin(), result.stereo.end());
	require(*extrema.second - *extrema.first > 1e-6, "Static PCM");
	require(result.feedback.at("pitch").size() > 10, "Missing pitch feedback");
	require(result.feedback.at("curves").size() == 3, "Missing variance feedback");
}
void tensorCacheFixture()
{
	const auto fixture = packageDirectory()
		/ fs::u8path("a3-tensor-fixture-"
					 + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	require(!fs::exists(fixture), "Fixture collision");
	fs::create_directory(fixture);
	const auto owned = fs::canonical(fixture);
	require(owned.parent_path() == fs::canonical(packageDirectory()), "Fixture escaped test directory");
	struct Cleanup
	{
		fs::path path;
		~Cleanup()
		{
			std::error_code error;
			fs::remove_all(path, error);
		}
	} cleanup{owned};
	const auto root = owned / "cache" / "SVS" / "DiffSinger";
	TensorCache cache(root, 500);
	const auto values = Tensor::make<float>(ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, 2}, {.25f, .5f});
	const Tensors inputs{{"in", values}}, outputs{{"out", values}};
	const auto key = TensorCache::key("fixture-v1", inputs);
	require(key != TensorCache::key("fixture-v2", inputs), "Tensor identity ignored");
	auto changed = inputs;
	changed.at("in").dimensions = {2, 1};
	require(key != TensorCache::key("fixture-v1", changed), "Tensor shape ignored");
	changed.at("in").bytes[0] ^= 1;
	require(key != TensorCache::key("fixture-v1", changed), "Tensor bytes ignored");
	cache.save(key, outputs);
	Tensors read;
	require(cache.load(key, read) && read.at("out").bytes == values.bytes, "Tensor roundtrip failed");
	const auto file = root / (key + ".tensor");
	{
		std::fstream corrupt(file, std::ios::in | std::ios::out | std::ios::binary);
		corrupt.put('X');
	}
	require(!cache.load(key, read) && !fs::exists(file), "Corrupt tensor did not become a miss");
	std::ofstream(root / "audition.wav") << "preserve";
	for (int i = 0; i < 8; ++i)
	{
		cache.save(TensorCache::key("fixture-" + std::to_string(i), inputs), outputs);
	}
	uint64_t bytes = 0;
	for (const auto& entry : fs::directory_iterator(root))
	{
		if (entry.path().extension() == ".tensor")
		{
			bytes += entry.file_size();
		}
	}
	require(bytes <= 500 && fs::exists(root / "audition.wav"), "Tensor LRU budget crossed its boundary");
	std::cout << "PASS tensor codec / identity / corruption / bounded LRU / audition preservation" << std::endl;
}
} // namespace
int run(int argc, char** argv)
{
	try
	{
		Json policy = Json::object();
		const bool singleVoice = argc == 5 && std::string(argv[1]) == "--directml-one";
		if (argc == 4 && std::string(argv[1]) == "--routing-faults")
		{
			initializeRuntime();
			Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "DiffSingerRoutingFaultTest"};
			routingFaults(env, fs::absolute(fs::u8path(argv[3])), argv[2]);
			return 0;
		}
		if (argc == 5 && (std::string(argv[1]) == "--directml" || singleVoice))
		{
			policy = {{"effectiveBackend", "directml"}, {"effectiveDevice", argv[2]}};
			argc -= 2;
			argv += 2;
		}
		wordTimingFixture();
		const bool testWords = argc == 5 && std::string(argv[1]) == "--word-models";
		if (argc == 5 && (std::string(argv[1]) == "--voice" || testWords))
		{
			initializeRuntime();
			Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "DiffSingerExternalVoiceTest"};
			const auto catalog = scan(
				{{"engineSettings",
				  {{"diffsinger.voicebankDirectories", Json::array({fs::absolute(fs::u8path(argv[2])).u8string()})},
				   {"diffsinger.vocoderDirectories", Json::array({fs::absolute(fs::u8path(argv[4])).u8string()})}}}},
				1);
			require(catalog->voices.size() == 1, "Expected one external voice: " + catalog->diagnostics.dump());
			auto voice = catalog->voices.front();
			if (testWords)
			{
				wordModels(env, voice);
				// In-memory fixture exercises the production shared linguistic
				// caller through a real pitch/acoustic/vocoder render. No user
				// configuration or model files are modified.
				auto fixture = std::make_shared<VoicePackage>(*voice);
				auto& pitch = fixture->stages.at("pitch");
				const auto& word = fixture->stages.at("variance");
				pitch.source = word.source;
				pitch.phonemes = word.phonemes;
				pitch.languages = word.languages;
				pitch.models["linguistic"] = word.models.at("linguistic");
				fixture->fingerprint += "/word-mode-pitch-fixture";
				voice = fixture;
			}
			svs_sdk::TempoMap tempo;
			require(tempo.setPoints({{0, 1. / 96.}}), "Invalid test tempo");
			std::atomic<bool> cancel{false};
			NoteInput note;
			note.id = "external-la";
			note.lyric = "la";
			note.reading = "la";
			note.language = "zh";
			note.durationTick = 96;
			note.duration = 1;
			note.pitch = 60;
			const std::vector<NoteInput> notes{note};
			Duration duration(env, voice);
			Synthesis synthesis(env, voice);
			const auto plan = duration.predict(notes, tempo, 0, Json::object(), cancel);
			const Json input{{"cacheDirectory", fs::absolute(fs::u8path(argv[3])).u8string()},
							 {"engineSettings", {{"diffsinger.renderSteps", 5}}}};
			const auto result = synthesis.render(plan, notes, input, tempo, 0, 48000, cancel);
			require(result.stereo.size() > 48000, "External PCM too short");
			double energy = 0;
			for (const auto value : result.stereo)
			{
				require(std::isfinite(value), "Non-finite external PCM");
				energy += double(value) * value;
			}
			require(energy / result.stereo.size() > 1e-10, "Silent external PCM");
			require(result.feedback.at("pitch").size() > 10, "Missing external pitch feedback");
			const auto repeated = synthesis.render(plan, notes, input, tempo, 0, 48000, cancel);
			require(digest(result.stereo) == digest(repeated.stereo), "External cached PCM changed");
			if (voice->stages.count("pitch"))
			{
				const auto cacheRoot = fs::absolute(fs::u8path(argv[3]));
				auto tensorFiles = [&] {
					std::set<fs::path> files;
					for (const auto& entry : fs::directory_iterator(cacheRoot))
					{
						if (entry.path().extension() == ".tensor")
						{
							files.insert(entry.path());
						}
					}
					return files;
				};
				const auto before = tensorFiles();
				auto repredict = input;
				repredict["pitchPredictionRequests"]
					= {{note.id,
						{{"request", std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())},
						 {"seed", UINT32_MAX},
						 {"take", 1}}}};
				const auto predicted = synthesis.render(plan, notes, repredict, tempo, 0, 48000, cancel);
				require(predicted.feedback.at("pitch") != result.feedback.at("pitch"),
						"New random prediction request did not change the predicted pitch");
				const auto after = tensorFiles();
				size_t fresh = 0;
				for (const auto& path : after)
				{
					if (!before.count(path))
					{
						++fresh;
					}
				}
				require(fresh > 0, "Re-prediction reused all previous tensors instead of running pitch");
				require(digest(synthesis.render(plan, notes, repredict, tempo, 0, 48000, cancel).stereo)
							== digest(predicted.stereo),
						"Repeating one prediction request changed its audio");
				require(tensorFiles() == after, "Repeating one prediction request did not reuse its tensors");
				auto fixedRepeat = repredict;
				fixedRepeat["pitchPredictionRequests"][note.id]["request"] = "fixed-seed-another-recording";
				fixedRepeat["pitchPredictionRequests"][note.id]["take"] = 2;
				require(synthesis.render(plan, notes, fixedRepeat, tempo, 0, 48000, cancel).feedback.at("pitch")
							== predicted.feedback.at("pitch"),
						"Manual fixed seed was not honored across recordings");
				std::cout << "PASS fresh pitch request executed inference and wrote " << fresh
						  << " new SHA256 tensors; repeated request reused them" << std::endl;
			}
			auto uncached = input;
			uncached.erase("cacheDirectory");
			require(digest(result.stereo)
						== digest(synthesis.render(plan, notes, uncached, tempo, 0, 48000, cancel).stereo),
					"External uncached seeded PCM changed");
			Json schema{{"parameters", Json::array()}, {"feedbackParameters", Json::array()}};
			Synthesis::declareParameters(*voice, schema);
			require(result.feedback.at("curves").size() == schema.at("feedbackParameters").size(),
					"External variance feedback differs from declared voice capability");
			audition(fs::absolute(fs::u8path(argv[3])) / "external-la-CPU.wav", result.stereo);
			std::cout << "PASS external voice " << voice->metadata.at("name").get<std::string>()
					  << " frames=" << result.stereo.size() / 2 << " curves=" << result.feedback.at("curves").size()
					  << std::endl;
			return 0;
		}
		if (argc != 3)
		{
			throw std::runtime_error("Usage: DiffSingerSynthesisTest <six-package root> <cache/SVS/DiffSinger>");
		}
		initializeRuntime();
		tensorCacheFixture();
		Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "DiffSingerSynthesisTest"};
		const auto catalog = scan(
			{{"engineSettings",
			  {{"diffsinger.voicebankDirectories", Json::array({fs::absolute(fs::u8path(argv[1])).u8string()})}}}},
			1);
		require(catalog->voices.size() == 6, "Six voices missing");
		svs_sdk::TempoMap tempo;
		require(tempo.setPoints({{0, 1. / 96.}}), "Invalid test tempo");
		std::atomic<bool> cancel{false};
		Json input{{"secondsPerTick", 1. / 96.},
				   {"cacheDirectory", fs::absolute(fs::u8path(argv[2])).u8string()},
				   {"engineSettings", {{"diffsinger.renderSteps", 5}}}};
		input["computePolicy"] = policy;
		if (!policy.empty())
		{
			input["seed"] = 1234;
		}
		for (const auto& voice : catalog->voices)
		{
			if (singleVoice && voice != catalog->voices.front())
			{
				break;
			}
			NoteInput a;
			a.id = "n1";
			a.lyric = "你";
			a.language = "zh";
			a.durationTick = 48;
			a.duration = .5;
			a.pitch = 60;
			NoteInput b = a;
			b.id = "n2";
			b.lyric = "好";
			b.tick = 48;
			b.start = .5;
			b.pitch = 62;
			const std::vector<NoteInput> notes{a, b};
			const auto coldStart = std::chrono::steady_clock::now();
			Duration duration(env, voice, policy);
			Synthesis synthesis(env, voice);
			std::unique_ptr<InferenceComparison> comparison;
			if (!policy.empty())
			{
				comparison = std::make_unique<InferenceComparison>(env, *voice, cancel);
			}
			const auto plan = duration.predict(notes, tempo, 0, Json::object(), cancel);
			auto cold = input;
			if (comparison)
			{
				cold.erase("cacheDirectory");
			}
			auto result = synthesis.render(plan, notes, cold, tempo, 0, 48000, cancel);
			if (comparison)
			{
				require(comparison->count() == 8, "Not all eight actual models were compared");
				const auto coldMs
					= std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - coldStart).count()
					- comparison->replayMilliseconds();
				std::cout << "PERFORMANCE DML cold duration+render cpu-replay-excluded ms=" << coldMs << std::endl;
				comparison.reset();
				Duration cpuDuration(env, voice);
				const auto cpuPlan = cpuDuration.predict(notes, tempo, 0, Json::object(), cancel);
				require(cpuPlan.phones.size() == plan.phones.size(), "CPU/DML phoneme count differs");
				const auto frame = voice->stages.at("acoustic").values.value("hop_size", 512.)
					/ voice->stages.at("acoustic").values.value("sample_rate", 44100.);
				for (size_t i = 0; i < plan.phones.size(); ++i)
				{
					const auto& a = plan.phones[i];
					const auto& b = cpuPlan.phones[i];
					require(a.symbol == b.symbol && a.noteId == b.noteId && std::abs(a.start - b.start) <= frame
								&& std::abs(a.end - b.end) <= frame,
							"CPU/DML phoneme layout differs");
				}
				auto cpuInput = cold;
				cpuInput["computePolicy"] = Json::object();
				Synthesis cpuSynthesis(env, voice);
				const auto cpuStart = std::chrono::steady_clock::now();
				const auto cpuResult = cpuSynthesis.render(cpuPlan, notes, cpuInput, tempo, 0, 48000, cancel);
				std::cout
					<< "PERFORMANCE CPU render ms="
					<< std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cpuStart).count()
					<< std::endl;
				InferenceComparison::pcm(result, cpuResult);
				if (voice == catalog->voices.front())
				{
					auto constrained = std::make_shared<VoicePackage>(*voice);
					constrained->stages.at("vocoder").values["force_on_cpu"] = true;
					Synthesis constrainedSynthesis(env, constrained);
					const auto forced = constrainedSynthesis.render(plan, notes, cold, tempo, 0, 48000, cancel);
					bool cpuVocoder = false, dmlAcoustic = false;
					for (const auto& execution : forced.feedback.at("computeStages"))
					{
						if (execution.at("stage") == "vocoder/model")
						{
							require(execution.at("effectiveBackend") == "cpu"
										&& execution.at("fallbackReason") == "Voice configuration force_on_cpu=true",
									"Real vocoder ignored force_on_cpu=true");
							cpuVocoder = true;
						}
						if (execution.at("stage") == "acoustic/acoustic"
							&& execution.at("effectiveBackend") == "directml"
							&& execution.at("providerEvidence").at("dmlNodes").get<int>() > 0)
						{
							dmlAcoustic = true;
						}
					}
					require(cpuVocoder && dmlAcoustic, "CPU vocoder constraint affected other stages");
					InferenceComparison::pcm(forced, result);
					std::cout << "PASS real vocoder force_on_cpu / other stages remain DML" << std::endl;
				}
			}
			if (!policy.empty())
			{
				Json stages = result.feedback.at("computeStages");
				for (const auto& execution : plan.computeStages)
				{
					stages.push_back(execution);
				}
				bool acousticDml = false, vocoderCpu = false;
				for (const auto& execution : stages)
				{
					const auto stage = execution.value("stage", std::string{});
					if (stage.rfind("acoustic/", 0) == 0 && execution.value("effectiveBackend", "") == "directml"
						&& execution.value("providerEvidence", Json::object()).value("dmlNodes", 0) > 0)
					{
						acousticDml = true;
					}
					if (stage.rfind("vocoder/", 0) == 0)
					{
						if (voice->stages.at("vocoder").values.value("force_on_cpu", false))
						{
							require(execution.value("effectiveBackend", "") == "cpu", "Vocoder escaped CPU constraint");
						}
						vocoderCpu = true;
					}
				}
				for (auto& execution : stages)
				{
					if (execution.contains("providerEvidence"))
					{
						execution["providerEvidence"].erase("nodes");
					}
				}
				std::cout << "COMPUTE " << voice->id << " " << stages.dump() << std::endl;
				require(acousticDml && vocoderCpu, "No actual acoustic DML / constrained vocoder evidence");
			}
			check(result);
			const auto hash = digest(result.stereo);
			std::cout << "PASS " << voice->metadata.at("name").get<std::string>()
					  << " frames=" << result.stereo.size() / 2 << " sha256=" << hash << std::endl;
			const auto warmStart = std::chrono::steady_clock::now();
			const auto cached = synthesis.render(plan, notes, input, tempo, 0, 48000, cancel);
			std::cout << "PERFORMANCE warm render/cache ms="
					  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - warmStart).count()
					  << std::endl;
			require(hash == digest(cached.stereo), "Cached PCM changed");
			audition(fs::absolute(fs::u8path(argv[2])) / (voice->root.filename().u8string() + "-A3-nihao-CPU.wav"),
					 result.stereo);
			auto drawn = input;
			drawn["curves"]["svs.pitch"]
				= {{"points", Json::array({{{"tick", -96}, {"value", 72}}, {{"tick", 192}, {"value", 72}}})}};
			const auto changed = synthesis.render(plan, notes, drawn, tempo, 0, 48000, cancel);
			check(changed);
			require(hash != digest(changed.stereo), "Pitch edit did not change audio");
			std::cout << "PASS cached replay and actual pitch-conditioned PCM change" << std::endl;
			auto noCache = input;
			noCache.erase("cacheDirectory");
			const auto recomputed = synthesis.render(plan, notes, noCache, tempo, 0, 48000, cancel);
			require(hash == digest(recomputed.stereo), "Seeded uncached recomputation changed PCM");
			auto control = input;
			control["clipParameters"] = {{"diffsinger.velocity", 1.3}, {"diffsinger.breathiness.offset", 6}};
			const auto controlled = synthesis.render(plan, notes, control, tempo, 0, 48000, cancel);
			check(controlled);
			require(hash != digest(controlled.stereo), "Voice controls did not change PCM");
			auto handNotes = notes;
			handNotes[0].phonemes["symbols"] = Json::array({"zh/a"});
			const auto handPlan = duration.predict(handNotes, tempo, 0, Json::object(), cancel);
			const auto hand = synthesis.render(handPlan, handNotes, input, tempo, 0, 48000, cancel);
			check(hand);
			require(hash != digest(hand.stereo), "Manual phonemes did not change PCM");
			cancel = true;
			try
			{
				synthesis.render(plan, notes, input, tempo, 0, 48000, cancel);
				throw std::runtime_error("Cancellation ignored");
			}
			catch (const std::exception& error)
			{
				require(std::string(error.what()).find("Cancelled") != std::string::npos,
						"Unexpected cancellation error");
			}
			cancel = false;
			std::cout << "PASS stable seed without cache / model controls / manual phonemes / cancellation"
					  << std::endl;
			if (voice == catalog->voices.front())
			{
				auto seeded = input;
				seeded["seed"] = 2;
				const auto another = synthesis.render(plan, notes, seeded, tempo, 0, 48000, cancel);
				require(hash != digest(another.stereo), "Seed change did not change PCM");
				require(hash == digest(synthesis.render(plan, notes, input, tempo, 0, 48000, cancel).stereo),
						"Returning to default seed changed PCM");
				auto longNotes = notes;
				for (auto note : notes)
				{
					note.id += "-later";
					note.tick += 4800;
					note.start += 50;
					longNotes.push_back(note);
				}
				auto longPlan = plan;
				for (auto phone : plan.phones)
				{
					phone.noteId += "-later";
					phone.start += 50;
					phone.end += 50;
					longPlan.phones.push_back(phone);
				}
				const auto split = synthesis.render(longPlan, longNotes, input, tempo, 0, 48000, cancel);
				check(split);
				require(split.stereo.size() > 48000 * 50 * 2, "Chunk placement lost silence");
				const size_t later = size_t(std::llround(50. * 48000)) * 2;
				require(std::equal(result.stereo.begin(), result.stereo.end(), split.stereo.begin())
							&& std::equal(result.stereo.begin(), result.stereo.end(), split.stereo.begin() + later),
						"Chunk placement changed PCM");
				require(split.feedback.at("curves").at("diffsinger.tension").at("gaps").size() == 1,
						"Chunk curve gap missing");
				auto restPlan = longPlan;
				restPlan.phones.insert(restPlan.phones.begin() + plan.phones.size(),
									   {"SP", "zh", "rest", plan.phones.back().end, 50, 60, false});
				auto restNotes = longNotes;
				auto rest = notes.front();
				rest.id = "rest";
				rest.lyric = "";
				rest.tick = 96;
				rest.start = 1;
				rest.durationTick = 4704;
				rest.duration = 49;
				restNotes.push_back(rest);
				require(digest(split.stereo)
							== digest(synthesis.render(restPlan, restNotes, input, tempo, 0, 48000, cancel).stereo),
						"Explicit long rest changed chunk placement");
				svs_sdk::TempoMap varied;
				require(varied.setPoints({{0, 1. / 96.}, {216, 1. / 48.}}), "Invalid varied tempo");
				const double origin = 192;
				auto mappedNotes = notes;
				for (auto& note : mappedNotes)
				{
					note.start = varied.secondsAt(origin + note.tick) - varied.secondsAt(origin);
					note.duration = varied.secondsAt(origin + note.tick + note.durationTick)
						- varied.secondsAt(origin + note.tick);
				}
				const auto mappedPlan = duration.predict(mappedNotes, varied, origin, Json::object(), cancel);
				const auto mapped = synthesis.render(mappedPlan, mappedNotes, input, varied, origin, 48000, cancel);
				check(mapped);
				require(mapped.stereo.size() > result.stereo.size(), "Tempo change did not change duration");
				const auto& points = mapped.feedback.at("curves").at("diffsinger.tension").at("points");
				const auto expected
					= varied.tickAt(varied.secondsAt(origin) + mapped.start + (points.size() - 1) * 512. / 44100.)
					- origin;
				require(std::abs(points.back().at("tick").get<double>() - expected) < 1e-8,
						"Feedback did not use frozen tempo and content-local ticks");
				auto tooLong = plan;
				tooLong.phones.back().end = 500;
				try
				{
					synthesis.render(tooLong, notes, input, tempo, 0, 48000, cancel);
					throw std::runtime_error("PCM bound ignored");
				}
				catch (const std::exception& error)
				{
					require(std::string(error.what()).find("before inference") != std::string::npos,
							"Unexpected preflight error");
				}
				std::cout << "PASS seed identity / natural-rest chunk placement / tempo with nonzero content origin / "
							 "PCM preflight bound"
						  << std::endl;
			}
		}
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAIL " << error.what() << std::endl;
		return 1;
	}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
	std::vector<std::string> utf8;
	std::vector<char*> arguments;
	for (int i = 0; i < argc; ++i)
	{
		utf8.push_back(fs::path(argv[i]).u8string());
	}
	for (auto& value : utf8)
	{
		arguments.push_back(value.data());
	}
	return run(argc, arguments.data());
}
#else
int main(int argc, char** argv)
{
	return run(argc, argv);
}
#endif
