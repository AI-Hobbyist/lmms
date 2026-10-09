// Synthetic AI compute-contract fixture, not a trained singing voicebank.
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>

#include "ComputeFixture.h"
#include "svs.h"
#include "svs_compute.hpp"

namespace {
namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;
fs::path package()
{
#ifdef _WIN32
	HMODULE module = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					   reinterpret_cast<const wchar_t*>(&package), &module);
	wchar_t path[32768];
	const auto length = GetModuleFileNameW(module, path, 32768);
	if (!length || length >= 32768)
	{
		throw std::runtime_error("Cannot locate compute fixture package");
	}
	return fs::path(path).parent_path();
#else
	Dl_info info{};
	if (!dladdr(reinterpret_cast<void*>(&package), &info))
	{
		throw std::runtime_error("Cannot locate compute fixture package");
	}
	return fs::path(info.dli_fname).parent_path();
#endif
}
struct Engine
{
	svs_compute::Library library;
	svs_compute::Context context;
	Engine()
#ifdef _WIN32
		: library(package().parent_path().parent_path() / "plugins" / "SVSCompute.dll")
#else
		: library(package().parent_path().parent_path() / "plugins" / "libSVSCompute.so")
#endif
		, context(library.context(package().parent_path() / "compute"))
	{
	}
};
struct Session
{
	Engine* engine;
	Json input;
	std::atomic<bool> cancelled{false};
	std::mutex mutex;
	std::condition_variable finished;
	bool active = false;
	std::shared_ptr<svs_compute::Run> run;
	uint32_t rate = 0;
	double duration = 0;
	explicit Session(Engine* owner)
		: engine(owner)
	{
	}
};
struct Audio
{
	std::vector<float> samples;
	std::string feedback, error;
};
svs_status text(const Json& data, const char** output)
{
	if (!output)
	{
		return SVS_INVALID_INPUT;
	}
	try
	{
		const auto value = data.dump();
		auto bytes = std::make_unique<char[]>(value.size() + 1);
		std::memcpy(bytes.get(), value.c_str(), value.size() + 1);
		*output = bytes.release();
		return SVS_OK;
	}
	catch (...)
	{
		return SVS_FAILED;
	}
}
svs_status SVS_CALL create(const svs_host*, svs_engine* output)
{
	if (!output)
	{
		return SVS_INVALID_INPUT;
	}
	*output = nullptr;
	try
	{
		*output = new Engine;
		return SVS_OK;
	}
	catch (...)
	{
		return SVS_FAILED;
	}
}
void SVS_CALL destroy(svs_engine engine)
{
	delete static_cast<Engine*>(engine);
}
svs_status SVS_CALL catalog(svs_engine engine, const char** output)
{
	if (!engine)
	{
		return SVS_INVALID_INPUT;
	}
	return text(
		{{"voices",
		  Json::array({{{"id", "compute-fixture"},
						{"name", "Shared AI compute test fixture"},
						{"version", "1.0"},
						{"languages", Json::array({"en"})},
						{"defaultLanguage", "en"},
						{"defaultLyric", "test"},
						{"description", "Synthetic ONNX contract fixture; not a trained singing voicebank"}}})}},
		output);
}
svs_status SVS_CALL capabilities(svs_engine engine, const char* voice, const char*, const char** output)
{
	if (!engine || !voice || std::strcmp(voice, "compute-fixture"))
	{
		return SVS_INVALID_INPUT;
	}
	return text({{"schemaVersion", 1},
				 {"languages", Json::array({"en"})},
				 {"defaultLanguage", "en"},
				 {"parameters", Json::array()},
				 {"feedbackParameters", Json::array()},
				 {"compute",
				  {{"protocolVersion", 1},
				   {"runtime", "svs-compute-1"},
				   {"supportedBackends", Json::array({"cpu", "directml"})}}},
				 {"synthesis", {{"cancel", true}, {"concurrent", false}, {"channels", 2}, {"format", "float32"}}}},
				output);
}
void SVS_CALL releaseString(svs_engine, const char* value)
{
	delete[] value;
}
svs_status SVS_CALL createSession(svs_engine engine, const char* voice, svs_session* output)
{
	if (!engine || !voice || std::strcmp(voice, "compute-fixture") || !output)
	{
		return SVS_INVALID_INPUT;
	}
	try
	{
		*output = new Session(static_cast<Engine*>(engine));
		return SVS_OK;
	}
	catch (...)
	{
		return SVS_FAILED;
	}
}
void SVS_CALL cancel(svs_session handle)
{
	if (!handle)
	{
		return;
	}
	auto& session = *static_cast<Session*>(handle);
	session.cancelled.store(true);
	std::lock_guard<std::mutex> lock(session.mutex);
	try
	{
		if (session.run)
		{
			session.run->cancel();
		}
	}
	catch (...)
	{
	}
}
void SVS_CALL destroySession(svs_session handle)
{
	if (!handle)
	{
		return;
	}
	cancel(handle);
	auto* session = static_cast<Session*>(handle);
	std::unique_lock<std::mutex> lock(session->mutex);
	session->finished.wait(lock, [&] { return !session->active; });
	lock.unlock();
	delete session;
}
svs_status SVS_CALL submit(svs_session handle, const svs_snapshot* snapshot)
{
	if (!handle || !snapshot || snapshot->size < sizeof(*snapshot) || !snapshot->input_json
		|| snapshot->sample_rate < 8000 || snapshot->sample_rate > 192000 || !std::isfinite(snapshot->duration_seconds)
		|| snapshot->duration_seconds < 0 || snapshot->duration_seconds > 10)
	{
		return SVS_INVALID_INPUT;
	}
	auto& session = *static_cast<Session*>(handle);
	std::lock_guard<std::mutex> lock(session.mutex);
	if (session.active)
	{
		return SVS_INVALID_INPUT;
	}
	try
	{
		session.input = Json::parse(snapshot->input_json);
		session.rate = snapshot->sample_rate;
		session.duration = snapshot->duration_seconds;
		session.cancelled.store(false);
		return SVS_OK;
	}
	catch (...)
	{
		return SVS_INVALID_INPUT;
	}
}
svs_status SVS_CALL render(svs_session handle, svs_result* output)
{
	if (!handle || !output || output->size < sizeof(*output))
	{
		return SVS_INVALID_INPUT;
	}
	auto& session = *static_cast<Session*>(handle);
	{
		std::lock_guard<std::mutex> lock(session.mutex);
		if (session.active || !session.rate)
		{
			return SVS_INVALID_INPUT;
		}
		session.active = true;
	}
	struct Completion
	{
		Session& session;
		~Completion()
		{
			std::lock_guard<std::mutex> lock(session.mutex);
			session.run.reset();
			session.active = false;
			session.finished.notify_all();
		}
	} completion{session};
	try
	{
		if (session.cancelled.load())
		{
			return SVS_CANCELLED;
		}
		const auto root = package().u8string();
		const svsc_model_desc modelDesc{sizeof(modelDesc), 1, root.c_str(), "fixture.onnx", "compute-example-add-v1"};
		const auto policy = session.input.value("computePolicy", Json::object());
		const auto backend = policy.value("effectiveBackend", "cpu");
		const auto device = policy.value("effectiveDevice", "cpu");
		const svsc_session_desc options{sizeof(options), 1, backend.c_str(), device.c_str(), "test-acoustic", ""};
		auto model = session.engine->context.model(modelDesc);
		auto compute = model.session(options);
		auto run = std::make_shared<svs_compute::Run>(compute.createRun());
		{
			std::lock_guard<std::mutex> lock(session.mutex);
			session.run = run;
			if (session.cancelled.load())
			{
				run->cancel();
			}
		}
		float x[]{0.1f, -0.1f, 0.1f, -0.1f}, y[]{0, 0, 0, 0};
		int64_t shape[]{1, 4};
		const std::vector<svsc_tensor> inputs{{sizeof(svsc_tensor), SVSC_FLOAT32, "x", 2, 0, shape, sizeof(x), x},
											  {sizeof(svsc_tensor), SVSC_FLOAT32, "y", 2, 0, shape, sizeof(y), y}};
		auto result = run->run(inputs);
		if (session.cancelled.load())
		{
			return SVS_CANCELLED;
		}
		auto audio = std::make_unique<Audio>();
		const auto frames = uint64_t(std::ceil(session.duration * session.rate));
		audio->samples.resize(size_t(frames) * 2);
		const auto& view = result.value();
		if (view.tensor_count != 1 || view.tensors[0].byte_count != sizeof(x))
		{
			return SVS_FAILED;
		}
		const auto* samples = static_cast<const float*>(view.tensors[0].data);
		for (uint64_t i = 0; i < frames; ++i)
		{
			audio->samples[size_t(i) * 2] = audio->samples[size_t(i) * 2 + 1] = samples[i % 4];
		}
		audio->feedback = Json{{"computeExecution", Json::parse(view.execution_json)}}.dump();
		audio->error = "{}";
		*output = {sizeof(*output),
				   session.rate,
				   2,
				   frames,
				   session.input.value("originSeconds", 0.0),
				   audio->samples.data(),
				   audio->feedback.c_str(),
				   audio->error.c_str(),
				   audio.get()};
		audio.release();
		return SVS_OK;
	}
	catch (const svs_compute::Error& error)
	{
		return error.status == SVSC_CANCELLED ? SVS_CANCELLED : SVS_FAILED;
	}
	catch (...)
	{
		return SVS_FAILED;
	}
}
void SVS_CALL releaseResult(svs_session, svs_result* result)
{
	if (!result)
	{
		return;
	}
	delete static_cast<Audio*>(result->owner);
	*result = {};
	result->size = sizeof(*result);
}
} // namespace

extern "C" SVS_EXPORT svs_status SVS_CALL svs_get_api(uint32_t major, uint32_t minor, uint32_t size, svs_api* output)
{
	if (major != 1 || !output || size < SVS_API_REQUIRED_SIZE)
	{
		return SVS_BAD_ABI;
	}
	svs_api api{};
	api.size = SVS_API_REQUIRED_SIZE;
	api.major = 1;
	api.minor = 0;
	api.create_engine = create;
	api.destroy_engine = destroy;
	api.catalog = catalog;
	api.capabilities = capabilities;
	api.release_string = releaseString;
	api.create_session = createSession;
	api.destroy_session = destroySession;
	api.submit = submit;
	api.render = render;
	api.cancel = cancel;
	api.release_result = releaseResult;
	std::memcpy(output, &api, SVS_API_REQUIRED_SIZE);
	return SVS_OK;
}
