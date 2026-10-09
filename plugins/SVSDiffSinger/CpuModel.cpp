#include "CpuModel.h"

#include <cstdlib>
#include <thread>

namespace diffsinger {
namespace {
svs_compute::Context& computeContext()
{
	static const auto directory = [] {
		const auto configured = std::getenv("SVS_COMPUTE_RUNTIME_DIR");
		return configured && *configured ? fs::u8path(configured)
										 : packageDirectory().parent_path().parent_path() / "svs" / "compute";
	}();
#ifdef _WIN32
	static svs_compute::Library library(directory.parent_path().parent_path() / "plugins" / "SVSCompute.dll");
#else
	static svs_compute::Library library(directory.parent_path().parent_path() / "plugins" / "libSVSCompute.so");
#endif
	static auto context = library.context(directory);
	return context;
}
} // namespace
CpuModel::CpuModel(Ort::Env& environment, const fs::path& path, std::string stage, uint32_t seed)
	: m_stage(std::move(stage))
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
		const svsc_session_desc options{sizeof(options), 0, "cpu", "cpu", m_stage.c_str(), ""};
		m_computeSession.emplace(m_computeModel->session(options));
	}
	catch (const std::exception& error)
	{
		throw std::runtime_error(m_stage + " / " + path.u8string() + ": " + error.what());
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
			tensors.push_back({sizeof(svsc_tensor), uint32_t(tensor.type), input.first.c_str(),
							   uint32_t(tensor.dimensions.size()), 0, tensor.dimensions.data(), tensor.bytes.size(),
							   tensor.bytes.data()});
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
			auto result = run.run(tensors);
			complete.store(true);
			cancellation.join();
			if (cancelled.load())
			{
				throw std::runtime_error("Cancelled");
			}
			Tensors output;
			const auto& view = result.value();
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
	catch (const std::exception& error)
	{
		throw std::runtime_error(m_stage + " / " + m_path.u8string() + ": " + error.what());
	}
}
} // namespace diffsinger
