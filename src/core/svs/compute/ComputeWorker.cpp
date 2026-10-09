#include <iostream>
#include <map>
#include <memory>
#include <set>

#include "ComputeDevice.h"
#include "ComputeFixture.h"
#include "ComputeModelSeed.h"
#include "ComputeTransport.h"
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace svsc {
Json ports(Ort::Session& session, bool input)
{
	Json result = Json::array();
	Ort::AllocatorWithDefaultOptions allocator;
	const auto count = input ? session.GetInputCount() : session.GetOutputCount();
	for (size_t i = 0; i < count; ++i)
	{
		const auto name
			= input ? session.GetInputNameAllocated(i, allocator) : session.GetOutputNameAllocated(i, allocator);
		const auto type = input ? session.GetInputTypeInfo(i) : session.GetOutputTypeInfo(i);
		if (type.GetONNXType() != ONNX_TYPE_TENSOR)
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Non-tensor model interface");
		}
		const auto info = type.GetTensorTypeAndShapeInfo();
		width(uint32_t(info.GetElementType()));
		const auto shape = info.GetShape();
		if (shape.size() > SVSC_MAX_RANK)
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Model tensor rank exceeds bound");
		}
		std::vector<const char*> symbols(shape.size());
		info.GetSymbolicDimensions(symbols.data(), symbols.size());
		Json names = Json::array();
		for (auto symbol : symbols)
		{
			names.push_back(symbol ? symbol : "");
		}
		result.push_back(
			{{"name", name.get()}, {"dtype", uint32_t(info.GetElementType())}, {"dims", shape}, {"symbols", names}});
	}
	return result;
}

struct Session
{
	fs::path path;
	std::string seedBytes, key;
	uint64_t handle = 0, references = 0, used = 0;
	Ort::Session session{nullptr};
	Json signature, providers = Json::object();
	bool profileActive = false;
	void reset(Ort::Env& env, Device& device, const std::string& backend, const std::string& id)
	{
		if (session && profileActive)
		{
			captureProfile();
		}
		session = Ort::Session(nullptr);
		Ort::SessionOptions options;
		device.configure(options, backend, id);
		const auto prefix = fs::temp_directory_path() / token();
		options.EnableProfiling(prefix.c_str());
		profileActive = true;
		if (seedBytes.empty())
		{
			session = Ort::Session(env, path.c_str(), options);
		}
		else
		{
			options.AddConfigEntry("session.model_external_initializers_file_folder_path",
								   path.parent_path().u8string().c_str());
			session = Ort::Session(env, seedBytes.data(), seedBytes.size(), options);
		}
	}
	void captureProfile()
	{
		if (!profileActive)
		{
			return;
		}
		Ort::AllocatorWithDefaultOptions allocator;
		const auto file = session.EndProfilingAllocated(allocator);
		profileActive = false;
		std::ifstream input(fs::u8path(file.get()));
		const auto events = Json::parse(input);
		Json nodes = Json::array();
		unsigned cpu = 0, dml = 0;
		for (const auto& event : events)
		{
			const auto args = event.value("args", Json::object());
			const auto provider = args.value("provider", std::string{});
			if (provider.empty())
			{
				continue;
			}
			if (provider == "DmlExecutionProvider")
			{
				++dml;
			}
			if (provider == "CPUExecutionProvider")
			{
				++cpu;
			}
			if (nodes.size() < 128)
			{
				nodes.push_back({{"name", event.value("name", "")},
								 {"provider", provider},
								 {"durationUs", event.value("dur", 0.0)}});
			}
		}
		providers = {{"dmlNodes", dml}, {"cpuNodes", cpu}, {"nodes", nodes}, {"scope", "session-profile"}};
		input.close();
		std::error_code error;
		fs::remove(fs::u8path(file.get()), error);
	}
	~Session()
	{
		try
		{
			if (session)
			{
				captureProfile();
			}
		}
		catch (...)
		{
		}
	}
};

class Server
{
	Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "SVSComputeWorker"};
	Device device;
	std::string backend, id, epoch;
	uint64_t lastRequest = 0, nextHandle = 1, clock = 0;
	std::map<uint64_t, std::unique_ptr<Session>> sessions;
	Json inventory()
	{
		Json devices = Json::array();
#ifdef SVSC_HAS_DML
		Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
		{
			throw Error(SVSC_UNAVAILABLE, "DXGI unavailable");
		}
		for (unsigned i = 0;; ++i)
		{
			Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
			const auto status = factory->EnumAdapters1(i, &adapter);
			if (status == DXGI_ERROR_NOT_FOUND)
			{
				break;
			}
			if (FAILED(status))
			{
				throw Error(SVSC_UNAVAILABLE, "DXGI enumeration failed");
			}
			DXGI_ADAPTER_DESC1 desc{};
			if (FAILED(adapter->GetDesc1(&desc)))
			{
				continue;
			}
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				continue;
			}
			std::ostringstream key;
			key << "dxgi:" << std::hex << std::setfill('0') << std::setw(8) << uint32_t(desc.AdapterLuid.HighPart)
				<< ':' << std::setw(8) << desc.AdapterLuid.LowPart;
			const auto length = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
			std::string name(size_t(length), '\0');
			WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name.data(), length, nullptr, nullptr);
			name.pop_back();
			devices.push_back({{"backend", "directml"},
							   {"device", key.str()},
							   {"name", name},
							   {"vendorId", desc.VendorId},
							   {"deviceId", desc.DeviceId},
							   {"dedicatedBytes", desc.DedicatedVideoMemory}});
		}
#endif
		return {{"devices", devices}};
	}
	Json probe()
	{
		Session model;
		model.seedBytes = fixtureModel();
		const auto directory = fs::temp_directory_path();
		model.path = directory / "svsc-probe.onnx";
		model.reset(environment, device, backend, id);
		float x[]{1, 2, 3, 4}, y[]{4, 3, 2, 1};
		int64_t shape[]{1, 4};
		const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
		std::vector<Ort::Value> inputs;
		inputs.push_back(Ort::Value::CreateTensor<float>(memory, x, 4, shape, 2));
		inputs.push_back(Ort::Value::CreateTensor<float>(memory, y, 4, shape, 2));
		const char* names[]{"x", "y"};
		const char* outputs[]{"z"};
		const auto result = model.session.Run(Ort::RunOptions{}, names, inputs.data(), 2, outputs, 1);
		for (unsigned i = 0; i < 4; ++i)
		{
			if (result[0].GetTensorData<float>()[i] != 5)
			{
				throw Error(SVSC_BACKEND_FAILURE, "Add probe numerical failure");
			}
		}
		model.captureProfile();
		if (backend == "directml" && model.providers.value("dmlNodes", 0u) == 0)
		{
			throw Error(SVSC_BACKEND_FAILURE, "Add probe executed no DML nodes");
		}
		return {{"available", true},
				{"effectiveBackend", backend},
				{"effectiveDevice", id},
				{"providerEvidence", model.providers}};
	}

	Session& find(uint64_t handle)
	{
		const auto found = sessions.find(handle);
		if (found == sessions.end() || !found->second->references)
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Unknown or released worker session handle");
		}
		found->second->used = ++clock;
		return *found->second;
	}
	Json create(const Json& request)
	{
		const auto model = request.at("model");
		const auto path = authorizedPath(fs::u8path(model.at("root").get<std::string>()),
										 fs::u8path(model.at("relative").get<std::string>()));
		const auto key = model.dump();
		for (auto& entry : sessions)
		{
			if (entry.second->key == key)
			{
				++entry.second->references;
				entry.second->used = ++clock;
				return {{"session", entry.first}, {"signature", entry.second->signature}};
			}
		}
		if (sessions.size() >= 32)
		{
			auto victim = sessions.end();
			for (auto iterator = sessions.begin(); iterator != sessions.end(); ++iterator)
			{
				if (!iterator->second->references
					&& (victim == sessions.end() || iterator->second->used < victim->second->used))
				{
					victim = iterator;
				}
			}
			if (victim == sessions.end())
			{
				throw Error(SVSC_LIMIT_EXCEEDED, "Worker session budget exhausted");
			}
			sessions.erase(victim);
		}
		auto value = std::make_unique<Session>();
		value->path = path;
		value->key = key;
		value->handle = nextHandle++;
		value->references = 1;
		value->used = ++clock;
		value->seedBytes = ModelSeed(path, model.at("seed").get<uint32_t>()).read(path);
		value->reset(environment, device, backend, id);
		value->signature = {{"inputs", ports(value->session, true)}, {"outputs", ports(value->session, false)}};
		Json result{{"session", value->handle}, {"signature", value->signature}};
		sessions.emplace(value->handle, std::move(value));
		return result;
	}
	Json run(const Json& request)
	{
		auto& model = find(request.at("session").get<uint64_t>());
		const auto bufferName = request.at("buffer").get<std::string>();
		if (bufferName.rfind("svsc-", 0) != 0
			|| bufferName.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos)
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Invalid shared buffer identity");
		}
		SharedBuffer buffer(bufferName);
		if (buffer.cancelled())
		{
			throw Error(SVSC_CANCELLED, "Cancelled before inference");
		}
		const auto inputs = request.at("inputs");
		if (!inputs.is_array() || inputs.size() > 256 || inputs.size() != model.signature["inputs"].size())
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Input tensor count mismatch");
		}
		if (!model.seedBytes.empty())
		{
			model.reset(environment, device, backend, id);
		}
		const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
		std::vector<Ort::Value> values;
		std::vector<std::string> names, outputNames;
		std::map<std::string, int64_t> symbols;
		std::set<std::string> unique;
		for (const auto& port : model.signature["inputs"])
		{
			const auto name = port.at("name").get<std::string>();
			const Json* tensor = nullptr;
			for (const auto& candidate : inputs)
			{
				if (candidate.at("name") == name)
				{
					if (tensor)
					{
						throw Error(SVSC_INVALID_ARGUMENT, "Duplicate tensor name");
					}
					tensor = &candidate;
				}
			}
			if (!tensor)
			{
				throw Error(SVSC_INVALID_ARGUMENT, "Missing tensor " + name);
			}
			const auto type = tensor->at("dtype").get<uint32_t>();
			const auto dims = tensor->at("dims").get<std::vector<int64_t>>();
			const auto bytes = tensorBytes(type, dims);
			const auto offset = tensor->at("offset").get<uint64_t>();
			if (type != port.at("dtype") || dims.size() != port.at("dims").size() || tensor->at("bytes") != bytes
				|| offset % 8 || offset > BufferLimit || bytes > BufferLimit - offset)
			{
				throw Error(SVSC_INVALID_ARGUMENT, "Invalid tensor dtype/rank/bytes/offset: " + name);
			}
			for (size_t i = 0; i < dims.size(); ++i)
			{
				const auto fixed = port["dims"][i].get<int64_t>();
				const auto symbol = port["symbols"][i].get<std::string>();
				if (fixed >= 0 && fixed != dims[i])
				{
					throw Error(SVSC_INVALID_ARGUMENT, "Static tensor dimension mismatch: " + name);
				}
				if (!symbol.empty())
				{
					const auto inserted = symbols.emplace(symbol, dims[i]);
					if (!inserted.second && inserted.first->second != dims[i])
					{
						throw Error(SVSC_INVALID_ARGUMENT, "Symbolic dimension mismatch: " + symbol);
					}
				}
			}
			validateValues(type, buffer.payload() + offset, bytes);
			names.push_back(name);
			values.push_back(Ort::Value::CreateTensor(memory, buffer.payload() + offset, bytes, dims.data(),
													  dims.size(), ONNXTensorElementDataType(type)));
		}
		for (const auto& port : model.signature["outputs"])
		{
			outputNames.push_back(port.at("name").get<std::string>());
		}
		std::vector<const char*> inputPointers, outputPointers;
		for (const auto& name : names)
		{
			inputPointers.push_back(name.c_str());
		}
		for (const auto& name : outputNames)
		{
			outputPointers.push_back(name.c_str());
		}
		Ort::RunOptions options;
		std::atomic<bool> done{false};
		std::thread cancellation([&] {
			while (!done.load())
			{
				if (buffer.cancelled())
				{
					try
					{
						options.SetTerminate();
					}
					catch (...)
					{
					}
					return;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		});
		const auto started = std::chrono::steady_clock::now();
		std::vector<Ort::Value> outputs;
		try
		{
			outputs = model.session.Run(options, inputPointers.data(), values.data(), values.size(),
										outputPointers.data(), outputPointers.size());
		}
		catch (...)
		{
			done.store(true);
			cancellation.join();
			if (buffer.cancelled())
			{
				throw Error(SVSC_CANCELLED, "Inference cancelled");
			}
			throw;
		}
		done.store(true);
		cancellation.join();
		if (buffer.cancelled())
		{
			throw Error(SVSC_CANCELLED, "Inference cancelled");
		}
		const auto milliseconds
			= std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
		Json tensors = Json::array();
		std::vector<std::vector<uint8_t>> copies;
		size_t offset = 0;
		for (size_t i = 0; i < outputs.size(); ++i)
		{
			const auto info = outputs[i].GetTensorTypeAndShapeInfo();
			const auto type = uint32_t(info.GetElementType());
			const auto dims = info.GetShape();
			const auto bytes = tensorBytes(type, dims);
			offset = (offset + 7) & ~size_t(7);
			if (offset > BufferLimit || bytes > BufferLimit - offset)
			{
				throw Error(SVSC_LIMIT_EXCEEDED, "Output tensors exceed shared budget");
			}
			validateValues(type, outputs[i].GetTensorRawData(), bytes);
			copies.emplace_back(bytes);
			if (bytes)
			{
				std::memcpy(copies.back().data(), outputs[i].GetTensorRawData(), bytes);
			}
			tensors.push_back(
				{{"name", outputNames[i]}, {"dtype", type}, {"dims", dims}, {"bytes", bytes}, {"offset", offset}});
			offset += bytes;
		}
		for (size_t i = 0; i < copies.size(); ++i)
		{
			if (!copies[i].empty())
			{
				std::memcpy(buffer.payload() + tensors[i]["offset"].get<size_t>(), copies[i].data(), copies[i].size());
			}
		}
		model.captureProfile();
		return {{"outputs", tensors},
				{"execution",
				 {{"effectiveBackend", backend},
				  {"effectiveDevice", id},
				  {"workerEpoch", epoch},
				  {"runtimeVersion", RuntimeVersion},
				  {"runMilliseconds", milliseconds},
				  {"providerEvidence", model.providers}}}};
	}

public:
	Server(std::string type, std::string deviceId, std::string workerEpoch)
		: backend(std::move(type))
		, id(std::move(deviceId))
		, epoch(std::move(workerEpoch))
	{
	}
	Json respond(const Json& request)
	{
		Json response{{"version", ProtocolVersion},
					  {"epoch", epoch},
					  {"request", request.value("request", uint64_t(0))},
					  {"status", SVSC_OK}};
		response["workerProcessId"] =
#ifdef _WIN32
			GetCurrentProcessId();
#else
			getpid();
#endif
		try
		{
			if (request.value("version", 0u) != ProtocolVersion || request.value("epoch", std::string{}) != epoch
				|| request.value("request", uint64_t(0)) <= lastRequest)
			{
				throw Error(SVSC_INVALID_ARGUMENT, "Invalid worker request version/epoch/sequence");
			}
			lastRequest = request.at("request").get<uint64_t>();
			const auto operation = request.at("op").get<std::string>();
			if (operation == "hello")
			{
				if (std::string(OrtGetApiBase()->GetVersionString()) != "1.23.0")
				{
					throw Error(SVSC_VERSION_MISMATCH, "ORT version mismatch");
				}
				response["runtime"] = RuntimeVersion;
				response["processId"] =
#ifdef _WIN32
					GetCurrentProcessId();
#else
					getpid();
#endif
			}
			else if (operation == "session")
			{
				response.update(create(request));
			}
			else if (operation == "inventory")
			{
				response.update(inventory());
			}
			else if (operation == "probe")
			{
				response.update(probe());
			}
			else if (operation == "run")
			{
				response.update(run(request));
			}
			else if (operation == "release")
			{
				--find(request.at("session").get<uint64_t>()).references;
			}
			else
			{
				throw Error(SVSC_INVALID_ARGUMENT, "Unknown worker operation");
			}
		}
		catch (const Error& error)
		{
			response["status"] = error.status;
			response["error"] = error.what();
		}
		catch (const Ort::Exception& error)
		{
			const std::string text = error.what();
			const bool backendError = backend == "directml"
				&& (error.GetOrtErrorCode() == ORT_NOT_IMPLEMENTED || text.find("DML") != std::string::npos
					|| text.find("DirectML") != std::string::npos || text.find("0x8007000E") != std::string::npos
					|| text.find("DXGI_ERROR") != std::string::npos);
			response["status"] = backendError ? SVSC_BACKEND_FAILURE : SVSC_INVALID_ARGUMENT;
			response["error"] = text;
		}
		catch (const std::exception& error)
		{
			response["status"] = SVSC_INVALID_ARGUMENT;
			response["error"] = error.what();
		}
		return response;
	}
};
} // namespace svsc

int main(int argc, char** argv)
{
	if (argc != 4)
	{
		return 2;
	}
#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif
	try
	{
		svsc::Server server(argv[1], argv[2], argv[3]);
		while (true)
		{
			uint32_t size = 0;
			if (!std::cin.read(reinterpret_cast<char*>(&size), sizeof(size)))
			{
				return 0;
			}
			if (!size || size > svsc::ControlLimit)
			{
				return 3;
			}
			std::string bytes(size, '\0');
			if (!std::cin.read(bytes.data(), size))
			{
				return 3;
			}
			const auto response = server.respond(svsc::Json::parse(bytes)).dump();
			if (response.size() > svsc::ControlLimit)
			{
				return 3;
			}
			const auto responseSize = uint32_t(response.size());
			std::cout.write(reinterpret_cast<const char*>(&responseSize), sizeof(responseSize));
			std::cout.write(response.data(), response.size());
			std::cout.flush();
		}
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
