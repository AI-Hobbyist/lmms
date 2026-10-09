#include <windows.h>

#include <d3d12.h>
#include <dml_provider_factory.h>
#include <dxgi1_4.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>
#include <sstream>
#include <string>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;

namespace {
void check(HRESULT value, const char* operation)
{
	if (FAILED(value))
	{
		throw std::runtime_error(std::string(operation) + " HRESULT=" + std::to_string(uint32_t(value)));
	}
}

std::string luid(LUID value)
{
	std::ostringstream text;
	text << "dxgi:" << std::hex << std::setfill('0') << std::setw(8) << uint32_t(value.HighPart) << ':' << std::setw(8)
		 << value.LowPart;
	return text.str();
}

std::string utf8(const wchar_t* value)
{
	const auto count = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
	std::string text(size_t(count), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value, -1, text.data(), count, nullptr, nullptr);
	text.pop_back();
	return text;
}

// Minimal ONNX Add fixture encoded using the protobuf wire format; no model download.
void varint(std::string& bytes, uint64_t value)
{
	while (value >= 128)
	{
		bytes.push_back(char((value & 127) | 128));
		value >>= 7;
	}
	bytes.push_back(char(value));
}

std::string integer(unsigned field, uint64_t value)
{
	std::string bytes;
	varint(bytes, field * 8);
	varint(bytes, value);
	return bytes;
}

std::string message(unsigned field, const std::string& value)
{
	std::string bytes;
	varint(bytes, field * 8 + 2);
	varint(bytes, value.size());
	return bytes + value;
}

std::string addModel()
{
	const auto shape = message(1, integer(1, 1)) + message(1, integer(1, 4));
	const auto tensorType = integer(1, 1) + message(2, shape);
	const auto value = [&](const char* name) { return message(1, name) + message(2, message(1, tensorType)); };
	const auto node = message(1, "x") + message(1, "y") + message(2, "z") + message(4, "Add");
	const auto graph = message(1, node) + message(2, "svsc-probe") + message(11, value("x")) + message(11, value("y"))
		+ message(12, value("z"));
	return integer(1, 8) + message(7, graph) + message(8, integer(2, 17));
}

Json probe(IDXGIAdapter1* adapter, const fs::path& model, const fs::path& output)
{
	ComPtr<ID3D12Device> device;
	check(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice");
	ComPtr<IDMLDevice> dml;
	check(DMLCreateDevice(device.Get(), DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&dml)), "DMLCreateDevice");
	D3D12_COMMAND_QUEUE_DESC desc{};
	desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
	ComPtr<ID3D12CommandQueue> queue;
	check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
	const OrtDmlApi* api = nullptr;
	Ort::ThrowOnError(
		Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&api)));
	Ort::SessionOptions options;
	options.DisableMemPattern();
	options.SetExecutionMode(ORT_SEQUENTIAL);
	options.SetIntraOpNumThreads(2);
	options.SetInterOpNumThreads(1);
	options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
	Ort::ThrowOnError(api->SessionOptionsAppendExecutionProvider_DML1(options, dml.Get(), queue.Get()));
	Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "SVSComputeProbe");
	if (!model.empty())
	{
		Ort::Session session(environment, model.c_str(), options);
		return {{"session", "PASS"}, {"inputs", session.GetInputCount()}, {"outputs", session.GetOutputCount()},
			{"execution", "PENDING_B3"},
			{"note", "Session construction only; does not prove model DML node execution"}};
	}
	const auto profilePrefix = output.parent_path() / "B0-device-profile";
	options.EnableProfiling(profilePrefix.c_str());
	const auto bytes = addModel();
	Ort::Session session(environment, bytes.data(), bytes.size(), options);
	float x[]{1, 2, 3, 4}, y[]{4, 3, 2, 1};
	int64_t dims[]{1, 4};
	const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
	std::vector<Ort::Value> values;
	values.push_back(Ort::Value::CreateTensor<float>(memory, x, 4, dims, 2));
	values.push_back(Ort::Value::CreateTensor<float>(memory, y, 4, dims, 2));
	const char* inputs[]{"x", "y"};
	const char* outputs[]{"z"};
	auto result = session.Run(Ort::RunOptions{}, inputs, values.data(), 2, outputs, 1);
	for (size_t i = 0; i < 4; ++i)
	{
		if (result[0].GetTensorData<float>()[i] != 5) { throw std::runtime_error("ORT Add probe numerical mismatch"); }
	}
	Ort::AllocatorWithDefaultOptions allocator;
	const auto profilePath = session.EndProfilingAllocated(allocator);
	std::ifstream file(fs::u8path(profilePath.get()));
	const auto profile = Json::parse(file);
	unsigned dmlNodes = 0, cpuNodes = 0;
	for (const auto& event : profile)
	{
		const auto provider = event.value("args", Json::object()).value("provider", std::string{});
		if (provider == "DmlExecutionProvider") { ++dmlNodes; }
		else if (provider == "CPUExecutionProvider") { ++cpuNodes; }
	}
	if (!dmlNodes) { throw std::runtime_error("Probe executed no DML nodes"); }
	return {{"available", true}, {"d3d12", "PASS"}, {"directml", "PASS"}, {"ortAdd", "PASS"}, {"dmlNodes", dmlNodes},
		{"cpuNodes", cpuNodes}, {"profile", profilePath.get()}};
}
} // namespace

int main(int argc, char** argv)
{
	if (argc != 3 && argc != 5)
	{
		std::cerr << "Usage: SVSComputeProbe report.json luid [model.onnx session]\n";
		return 2;
	}
	const auto output = fs::u8path(argv[1]);
	Json report{{"runtime", OrtGetApiBase()->GetVersionString()}, {"devices", Json::array()}};
	int status = 0;
	try
	{
		ComPtr<IDXGIFactory1> factory;
		check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
		bool matched = false;
		for (unsigned i = 0;; ++i)
		{
			ComPtr<IDXGIAdapter1> adapter;
			const auto enumeration = factory->EnumAdapters1(i, &adapter);
			if (enumeration == DXGI_ERROR_NOT_FOUND) { break; }
			check(enumeration, "EnumAdapters1");
			DXGI_ADAPTER_DESC1 description{};
			check(adapter->GetDesc1(&description), "GetDesc1");
			if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { continue; }
			const auto id = luid(description.AdapterLuid);
			if (std::string(argv[2]) != "all" && id != argv[2]) { continue; }
			matched = true;
			Json item{{"luid", id}, {"name", utf8(description.Description)}, {"vendor", description.VendorId},
				{"device", description.DeviceId}, {"dedicatedBytes", description.DedicatedVideoMemory},
				{"available", false}};
			try
			{
				item.update(probe(adapter.Get(), argc == 5 ? fs::u8path(argv[3]) : fs::path{}, output));
			}
			catch (const std::exception& error)
			{
				item["reason"] = error.what();
				if (argc == 5) { status = 1; }
			}
			std::cout << item.dump() << std::endl;
			report["devices"].push_back(std::move(item));
		}
		if (!matched && std::string(argv[2]) != "all") { throw std::runtime_error("Requested LUID is absent"); }
	}
	catch (const std::exception& error)
	{
		report["error"] = error.what();
		std::cerr << error.what() << std::endl;
		status = 1;
	}
	std::ofstream file(output, std::ios::binary);
	file << report.dump(2) << '\n';
	return file ? status : 2;
}
