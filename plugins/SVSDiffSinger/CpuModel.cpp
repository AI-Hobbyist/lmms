#include "CpuModel.h"
#include "ModelSeed.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <thread>
namespace diffsinger {
namespace {
size_t width(ONNXTensorElementDataType type)
{
	if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
	{
		return 4;
	}
	if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64)
	{
		return 8;
	}
	if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL)
	{
		return 1;
	}
	throw std::runtime_error("Unsupported ONNX tensor type: " + std::to_string(type));
}
size_t count(const std::vector<int64_t>& shape)
{
	size_t total = 1;
	if (shape.size() > 8)
	{
		throw std::runtime_error("Tensor rank exceeds bound");
	}
	for (const auto dimension : shape)
	{
		if (dimension < 0 || uint64_t(dimension) > 128 * 1024 * 1024
			|| total > 128 * 1024 * 1024 / std::max(int64_t(1), dimension))
		{
			throw std::runtime_error("Tensor dimensions exceed bound");
		}
		total *= size_t(dimension);
	}
	return total;
}
void finite(const Tensor& tensor)
{
	if (tensor.type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
	{
		for (const auto value : tensor.values<float>())
		{
			if (!std::isfinite(value))
			{
				throw std::runtime_error("Non-finite tensor value");
			}
		}
	}
}
}
void CpuModel::resetSession()
{
	m_session = Ort::Session(nullptr);
	Ort::SessionOptions options;
	options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
	options.SetIntraOpNumThreads(std::max(1u, std::min(4u, std::thread::hardware_concurrency())));
	options.SetInterOpNumThreads(1);
	options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
	if (m_seededModel.empty())
	{
		m_session = Ort::Session(m_environment, m_path.c_str(), options);
	}
	else
	{
		options.AddConfigEntry(
			"session.model_external_initializers_file_folder_path", m_path.parent_path().u8string().c_str());
		m_session = Ort::Session(m_environment, m_seededModel.data(), m_seededModel.size(), options);
	}
}
CpuModel::CpuModel(Ort::Env& environment, const fs::path& path, std::string stage, uint32_t seed)
	: m_stage(std::move(stage))
	, m_path(path)
	, m_environment(environment)
{
	try
	{
		m_seededModel = ModelSeed(path, seed).read(path);
		resetSession();
		Ort::AllocatorWithDefaultOptions allocator;
		for (size_t i = 0; i < m_session.GetInputCount(); ++i)
		{
			const auto name = m_session.GetInputNameAllocated(i, allocator);
			const auto type = m_session.GetInputTypeInfo(i);
			const auto info = type.GetTensorTypeAndShapeInfo();
			m_inputs.push_back({name.get(), info.GetElementType(), info.GetShape()});
		}
		for (size_t i = 0; i < m_session.GetOutputCount(); ++i)
		{
			const auto name = m_session.GetOutputNameAllocated(i, allocator);
			const auto type = m_session.GetOutputTypeInfo(i);
			const auto info = type.GetTensorTypeAndShapeInfo();
			m_outputs.push_back({name.get(), info.GetElementType(), info.GetShape()});
		}
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
		// Reset only stochastic sessions so equal seeded requests do not depend
		// on how many previous Run calls advanced an operator's RNG.
		if (!m_seededModel.empty())
		{
			resetSession();
		}
		if (inputs.size() != m_inputs.size())
		{
			throw std::runtime_error("Input count does not match model signature");
		}
		const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
		std::vector<Ort::Value> values;
		std::vector<const char*> names, outputs;
		for (const auto& port : m_inputs)
		{
			const auto found = inputs.find(port.name);
			if (found == inputs.end())
			{
				throw std::runtime_error("Missing input " + port.name);
			}
			const auto& tensor = found->second;
			if (port.type != tensor.type || port.dimensions.size() != tensor.dimensions.size()
				|| count(tensor.dimensions) * width(tensor.type) != tensor.bytes.size())
			{
				throw std::runtime_error("Invalid type/rank/byte count for input " + port.name);
			}
			for (size_t i = 0; i < port.dimensions.size(); ++i)
			{
				if (port.dimensions[i] >= 0 && port.dimensions[i] != tensor.dimensions[i])
				{
					throw std::runtime_error("Invalid shape for input " + port.name);
				}
			}
			finite(tensor);
			names.push_back(port.name.c_str());
			values.push_back(Ort::Value::CreateTensor(memory, const_cast<uint8_t*>(tensor.bytes.data()),
				tensor.bytes.size(), tensor.dimensions.data(), tensor.dimensions.size(), tensor.type));
		}
		for (const auto& port : m_outputs)
		{
			outputs.push_back(port.name.c_str());
		}
		Ort::RunOptions options;
		auto result
			= m_session.Run(options, names.data(), values.data(), values.size(), outputs.data(), outputs.size());
		if (cancelled.load())
		{
			throw std::runtime_error("Cancelled");
		}
		Tensors tensors;
		for (size_t i = 0; i < result.size(); ++i)
		{
			if (!result[i].IsTensor())
			{
				throw std::runtime_error("Non-tensor model output");
			}
			const auto info = result[i].GetTensorTypeAndShapeInfo();
			Tensor tensor{info.GetElementType(), info.GetShape(), {}};
			tensor.bytes.resize(count(tensor.dimensions) * width(tensor.type));
			if (!tensor.bytes.empty())
			{
				std::memcpy(tensor.bytes.data(), result[i].GetTensorRawData(), tensor.bytes.size());
			}
			finite(tensor);
			tensors.emplace(m_outputs[i].name, std::move(tensor));
		}
		return tensors;
	}
	catch (const std::exception& error)
	{
		throw std::runtime_error(m_stage + " / " + m_path.u8string() + ": " + error.what());
	}
}
}
