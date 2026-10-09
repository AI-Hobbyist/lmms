#include "CpuModel.h"

#include <cstdlib>
#include <mutex>
#include <thread>
#include <utility>

namespace diffsinger {
namespace {
thread_local InferenceObserver inferenceObserver;
struct ComputeRuntime
{
	std::mutex mutex;
	size_t engines = 0;
	std::optional<svs_compute::Library> library;
	std::optional<svs_compute::Context> context;
};
ComputeRuntime& computeRuntime()
{
	static ComputeRuntime runtime;
	return runtime;
}
svs_compute::Context& computeContext()
{
	static const auto directory = [] {
		const auto configured = std::getenv("SVS_COMPUTE_RUNTIME_DIR");
		return configured && *configured ? fs::u8path(configured)
										 : packageDirectory().parent_path().parent_path() / "svs" / "compute";
	}();
	auto& runtime = computeRuntime();
	std::lock_guard lock(runtime.mutex);
	if (!runtime.context)
	{
#ifdef _WIN32
		runtime.library.emplace(directory.parent_path().parent_path() / "plugins" / "SVSCompute.dll");
#else
		runtime.library.emplace(directory.parent_path().parent_path() / "plugins" / "libSVSCompute.so");
#endif
		runtime.context.emplace(runtime.library->context(directory));
	}
	return *runtime.context;
}
} // namespace
void acquireComputeRuntime()
{
	auto& runtime = computeRuntime();
	std::lock_guard lock(runtime.mutex);
	++runtime.engines;
}
void releaseComputeRuntime()
{
	auto& runtime = computeRuntime();
	std::lock_guard lock(runtime.mutex);
	if (--runtime.engines == 0)
	{
		// Join the residency monitor while the plugin is still loaded, outside the DLL loader lock.
		runtime.context.reset();
		runtime.library.reset();
	}
}
std::shared_ptr<svs_compute::RenderLease> retainComputeModels()
{
	return computeContext().retainModels();
}
InferenceObserver exchangeInferenceObserver(InferenceObserver observer)
{
	return std::exchange(inferenceObserver, std::move(observer));
}
Json voiceComputePolicy(const VoicePackage& voice, Json policy)
{
	if (!policy.is_object())
	{
		throw std::runtime_error("Compute policy must be an object");
	}
	if (!policy.contains("stageOverrides"))
	{
		policy["stageOverrides"] = Json::array();
	}
	for (const auto& stage : voice.stages)
	{
		if (stage.second.values.value("force_on_cpu", false))
		{
			for (const auto& model : stage.second.models)
			{
				const auto name = stage.first == "duration"
					? (model.first == "linguistic" ? "duration.linguistic" : "duration")
					: stage.first + "/" + model.first;
				const Json constraint{{"stage", name},
									  {"effectiveBackend", "cpu"},
									  {"effectiveDevice", "cpu"},
									  {"reason", "Voice configuration force_on_cpu=true"}};
				if (std::find(policy["stageOverrides"].begin(), policy["stageOverrides"].end(), constraint)
					== policy["stageOverrides"].end())
				{
					policy["stageOverrides"].push_back(constraint);
				}
			}
		}
	}
	return policy;
}
CpuModel::CpuModel(Ort::Env& environment, const fs::path& path, std::string stage, uint32_t seed, const Json& policy)
	: m_stage(std::move(stage))
	, m_seed(seed)
	, m_path(path)
{
	(void)environment;
	try
	{
		const auto root = fs::canonical(path.parent_path()).u8string();
		const auto relative = path.filename().u8string();
		const auto fingerprint = std::to_string(fs::file_size(path)) + ":"
			+ std::to_string(fs::last_write_time(path).time_since_epoch().count());
		const svsc_model_desc descriptor{sizeof(descriptor), seed, root.c_str(), relative.c_str(), fingerprint.c_str()};
		m_computeModel.emplace(computeContext().model(descriptor));
		const auto signature = Json::parse(m_computeModel->signature());
		for (const auto& port : signature.at("inputs"))
		{
			m_inputs.push_back({port.at("name").get<std::string>(),
								ONNXTensorElementDataType(port.at("dtype").get<uint32_t>()),
								port.at("dims").get<std::vector<int64_t>>()});
		}
		for (const auto& port : signature.at("outputs"))
		{
			m_outputs.push_back({port.at("name").get<std::string>(),
								 ONNXTensorElementDataType(port.at("dtype").get<uint32_t>()),
								 port.at("dims").get<std::vector<int64_t>>()});
		}
		setComputePolicy(policy);
	}
	catch (const std::exception& error)
	{
		throw std::runtime_error(m_stage + " / " + path.u8string() + ": " + error.what());
	}
}

void CpuModel::setComputePolicy(const Json& policy)
{
	if (!policy.is_object())
	{
		throw std::runtime_error("Compute policy must be an object");
	}
	auto backend = policy.value("effectiveBackend", std::string("cpu"));
	auto device = policy.value("effectiveDevice", std::string("cpu"));
	if (backend != "cpu" && backend != "directml")
	{
		throw std::runtime_error("Unsupported compute backend");
	}
	std::string reason;
	for (const auto& constraint : policy.value("stageOverrides", Json::array()))
	{
		const auto stage = constraint.value("stage", std::string{});
		if (!stage.empty()
			&& (m_stage == stage || m_stage.rfind(stage + "/", 0) == 0 || m_stage.rfind(stage + ".", 0) == 0)
			&& constraint.value("effectiveBackend", "") == "cpu")
		{
			backend = "cpu";
			device = "cpu";
			reason = constraint.value("reason", std::string("Configured CPU-only stage"));
			break;
		}
	}
	const auto requestedBackend = policy.value("effectiveBackend", std::string("cpu"));
	const auto requestedDevice = policy.value("effectiveDevice", std::string("cpu"));
	m_policy = policy;
	m_cpuOnly = !reason.empty();
	if (m_computeSession && requestedBackend == m_requestedBackend && requestedDevice == m_requestedDevice)
	{
		return;
	}
	m_computeSession.reset();
	m_execution = Json::object();
	m_fallbackReason = reason;
	const svsc_session_desc options{sizeof(options), 0, backend.c_str(), device.c_str(), m_stage.c_str(), ""};
	try
	{
		m_computeSession.emplace(m_computeModel->session(options));
	}
	catch (const svs_compute::Error& error)
	{
		if (backend != "directml"
			|| (error.status != SVSC_BACKEND_FAILURE && error.status != SVSC_UNAVAILABLE
				&& error.status != SVSC_WORKER_LOST))
		{
			throw;
		}
		m_fallbackReason = error.what();
		backend = "cpu";
		device = "cpu";
		const svsc_session_desc cpu{sizeof(cpu), 0, "cpu", "cpu", m_stage.c_str(), ""};
		m_computeSession.emplace(m_computeModel->session(cpu));
	}
	m_backend = backend;
	m_device = device;
	m_requestedBackend = requestedBackend;
	m_requestedDevice = requestedDevice;
	m_execution = {{"stage", m_stage},
				   {"requestedBackend", requestedBackend},
				   {"requestedDevice", requestedDevice},
				   {"effectiveBackend", m_backend},
				   {"effectiveDevice", m_device},
				   {"fallbackReason", m_fallbackReason}};
}

std::string CpuModel::computeIdentity() const
{
	return m_backend + "/" + m_device + "/svs-compute-1/ORT1.23.0/DML1.15.4/native.v4";
}
void CpuModel::beginRequest()
{
	if (!m_cpuOnly && m_requestedBackend == "directml" && m_backend == "cpu")
	{
		m_computeSession.reset();
		setComputePolicy(m_policy);
	}
}

bool CpuModel::accepts(const std::string& name) const
{
	return std::any_of(m_inputs.begin(), m_inputs.end(), [&](const auto& port) { return port.name == name; });
}

Tensors CpuModel::run(const Tensors& inputs, const std::atomic<bool>& cancelled)
{
	try
	{
		if (cancelled.load())
		{
			throw std::runtime_error("Cancelled");
		}
		std::vector<svsc_tensor> tensors;
		for (const auto& input : inputs)
		{
			const auto& tensor = input.second;
			tensors.push_back(
				{sizeof(svsc_tensor), uint32_t(tensor.type), input.first.c_str(), uint32_t(tensor.dimensions.size()), 0,
					tensor.dimensions.data(), tensor.bytes.size(), tensor.bytes.data()});
		}
		auto run = m_computeSession->createRun();
		std::atomic<bool> complete{false};
		std::thread cancellation([&] {
			while (!complete.load())
			{
				if (cancelled.load())
				{
					try
					{
						run.cancel();
					}
					catch (...)
					{
					}
					return;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		});
		try
		{
			std::optional<svs_compute::Result> result(run.run(tensors));
			complete.store(true);
			cancellation.join();
			if (cancelled.load())
			{
				throw std::runtime_error("Cancelled");
			}
			Tensors output;
			const auto& view = result->value();
			m_execution = Json::parse(view.execution_json);
			m_execution["requestedBackend"] = m_requestedBackend;
			m_execution["requestedDevice"] = m_requestedDevice;
			if (!m_fallbackReason.empty())
			{
				m_execution["fallbackReason"] = m_fallbackReason;
			}
			for (uint32_t i = 0; i < view.tensor_count; ++i)
			{
				const auto& value = view.tensors[i];
				Tensor tensor{ONNXTensorElementDataType(value.dtype), {}, {}};
				if (value.rank)
				{
					tensor.dimensions.assign(value.dims, value.dims + value.rank);
				}
				tensor.bytes.resize(size_t(value.byte_count));
				if (value.byte_count)
				{
					std::memcpy(tensor.bytes.data(), value.data, size_t(value.byte_count));
				}
				output.emplace(value.name, std::move(tensor));
			}
			// The copied output owns its data; release the compute result before
			// observers or the next synthesis stage can retain temporary storage.
			result.reset();
			if (inferenceObserver)
			{
				inferenceObserver(m_path, m_stage, m_seed, inputs, output, m_execution);
			}
			return output;
		}
		catch (...)
		{
			complete.store(true);
			if (cancellation.joinable())
			{
				cancellation.join();
			}
			throw;
		}
	}
	catch (const svs_compute::Error& error)
	{
		if (cancelled.load())
		{
			throw std::runtime_error("Cancelled");
		}
		if (m_backend != "directml"
			|| (error.status != SVSC_BACKEND_FAILURE && error.status != SVSC_WORKER_LOST
				&& error.status != SVSC_UNAVAILABLE))
		{
			throw std::runtime_error(m_stage + " / " + m_path.u8string() + ": " + error.what());
		}
		// Retry the complete validated stage once. CPU errors and cancellation never recurse.
		m_fallbackReason = error.what();
		m_computeSession.reset();
		const svsc_session_desc cpu{sizeof(cpu), 0, "cpu", "cpu", m_stage.c_str(), ""};
		m_computeSession.emplace(m_computeModel->session(cpu));
		m_backend = "cpu";
		m_device = "cpu";
		return run(inputs, cancelled);
	}
	catch (const std::exception& error)
	{
		throw std::runtime_error(m_stage + " / " + m_path.u8string() + ": " + error.what());
	}
}
} // namespace diffsinger
