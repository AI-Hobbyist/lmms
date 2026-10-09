#include <condition_variable>
#include <map>
#include <memory>
#include <regex>
#include <set>

#include "ComputeResidency.h"
#include "ComputeTransport.h"

namespace svsc {
enum class Kind
{
	Context,
	Model,
	Session,
	Run,
	Result
};
struct Object
{
	Kind kind;
	std::shared_ptr<Object> parent;
	std::atomic<bool> closed{false};
	explicit Object(Kind value, std::shared_ptr<Object> owner = {})
		: kind(value)
		, parent(std::move(owner))
	{
	}
	virtual ~Object() = default;
};
struct Context : Object
{
	fs::path directory;
	std::mutex mutex;
	std::atomic<size_t> allocatedBytes{0};
	std::shared_ptr<Worker> cpu, dml;
	explicit Context(fs::path path)
		: Object(Kind::Context)
		, directory(std::move(path))
	{
		Residency::instance().contextOpened();
	}
	~Context() override
	{
		cpu.reset();
		dml.reset();
		Residency::instance().contextClosed();
	}
	std::shared_ptr<Worker> worker(const std::string& backend, const std::string& id)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (closed.load())
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Context is closed");
		}
		auto& value = backend == "cpu" ? cpu : dml;
		if (value && (value->lost.load() || value->device != id))
		{
			if (!value->lost.load() && value.use_count() > 1)
			{
				throw Error(SVSC_LIMIT_EXCEEDED, "Previous DML device still has live sessions");
			}
			value.reset();
		}
		if (!value)
		{
			value = std::make_shared<Worker>(directory, backend, id);
			Residency::instance().add(value);
		}
		return value;
	}
};
struct Allocation
{
	std::shared_ptr<Context> context;
	size_t bytes;
	Allocation(std::shared_ptr<Context> owner, size_t count)
		: context(std::move(owner))
		, bytes(count)
	{
		constexpr size_t limit = size_t(1024) * 1024 * 1024;
		auto current = context->allocatedBytes.load();
		for (;;)
		{
			if (bytes > limit - current)
			{
				throw Error(SVSC_LIMIT_EXCEEDED, "Context shared-buffer/result budget exceeded");
			}
			if (context->allocatedBytes.compare_exchange_weak(current, current + bytes))
			{
				break;
			}
		}
	}
	~Allocation() { context->allocatedBytes.fetch_sub(bytes); }
};
struct Model : Object
{
	std::shared_ptr<Context> context;
	Json descriptor;
	Model(std::shared_ptr<Context> owner, Json desc)
		: Object(Kind::Model, owner)
		, context(std::move(owner))
		, descriptor(std::move(desc))
	{
	}
};
struct Session : Object
{
	std::shared_ptr<Model> model;
	std::shared_ptr<Worker> worker;
	std::string requestedBackend, requestedDevice, stage, reason;
	uint64_t remote = 0;
	bool fallback = false;
	Session(std::shared_ptr<Model> owner)
		: Object(Kind::Session, owner)
		, model(std::move(owner))
	{
	}
	~Session()
	{
		try
		{
			if (worker && !worker->lost.load() && remote)
			{
				worker->rpc({{"op", "release"}, {"session", remote}}, 5);
			}
		}
		catch (...)
		{
		}
	}
};
struct Run : Object
{
	std::shared_ptr<Session> session;
	std::mutex mutex;
	std::condition_variable completed;
	std::shared_ptr<SharedBuffer> buffer;
	std::atomic<bool> cancelled{false};
	bool active = false, used = false;
	explicit Run(std::shared_ptr<Session> owner)
		: Object(Kind::Run, owner)
		, session(std::move(owner))
	{
	}
};
struct Result : Object
{
	std::shared_ptr<Allocation> allocation;
	std::vector<svsc_tensor> tensors;
	std::vector<std::vector<int64_t>> shapes;
	std::vector<std::string> names;
	std::vector<std::vector<uint8_t>> bytes;
	std::string execution;
	svsc_result view{};
	explicit Result(std::shared_ptr<Run> owner)
		: Object(Kind::Result, std::move(owner))
	{
	}
};

std::mutex registryMutex;
std::map<svsc_handle, std::shared_ptr<Object>> objects;
std::atomic<svsc_handle> nextHandle{1};
thread_local std::string lastError;

template <class T> std::shared_ptr<T> get(svsc_handle handle, Kind kind)
{
	std::lock_guard<std::mutex> lock(registryMutex);
	const auto found = objects.find(handle);
	if (found == objects.end() || found->second->kind != kind || found->second->closed.load())
	{
		throw Error(SVSC_INVALID_ARGUMENT, "Invalid or stale compute handle");
	}
	for (auto parent = found->second->parent; parent; parent = parent->parent)
	{
		if (parent->closed.load())
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Compute parent is closed");
		}
	}
	return std::static_pointer_cast<T>(found->second);
}
svsc_handle add(std::shared_ptr<Object> value)
{
	std::lock_guard<std::mutex> lock(registryMutex);
	const auto handle = nextHandle.fetch_add(1);
	objects.emplace(handle, std::move(value));
	return handle;
}
template <class F> svsc_status call(F&& function)
{
	lastError.clear();
	try
	{
		function();
		return SVSC_OK;
	}
	catch (const Error& error)
	{
		lastError = error.what();
		return error.status;
	}
	catch (const std::bad_alloc&)
	{
		lastError = "Compute allocation budget exhausted";
		return SVSC_LIMIT_EXCEEDED;
	}
	catch (const std::exception& error)
	{
		lastError = error.what();
		return SVSC_INVALID_ARGUMENT;
	}
	catch (...)
	{
		lastError = "Unknown native compute failure";
		return SVSC_INTERNAL_ERROR;
	}
}
void outputString(const Json& json, char** output)
{
	if (!output)
	{
		throw Error(SVSC_INVALID_ARGUMENT, "Null string output");
	}
	const auto text = json.dump();
	auto value = std::make_unique<char[]>(text.size() + 1);
	std::memcpy(value.get(), text.c_str(), text.size() + 1);
	*output = value.release();
}
void backend(const char* type, const char* id)
{
	if (!type || !id || (std::string(type) != "cpu" && std::string(type) != "directml"))
	{
		throw Error(SVSC_INVALID_ARGUMENT, "Unknown compute backend");
	}
	if ((std::string(type) == "cpu" && std::string(id) != "cpu")
		|| (std::string(type) == "directml" && !std::regex_match(id, std::regex("dxgi:[0-9a-f]{8}:[0-9a-f]{8}"))))
	{
		throw Error(SVSC_INVALID_ARGUMENT, "Invalid compute device identity");
	}
}
void initialize(Session& session, const std::string& type, const std::string& id)
{
	RenderLease lease;
	const auto worker = session.model->context->worker(type, id);
	const auto response = worker->rpc({{"op", "session"}, {"model", session.model->descriptor}}, 120);
	session.worker = worker;
	session.remote = response.at("session").get<uint64_t>();
}
void cancelRun(const std::shared_ptr<Run>& run)
{
	run->cancelled.store(true);
	std::lock_guard<std::mutex> lock(run->mutex);
	if (run->buffer)
	{
		run->buffer->cancel();
	}
}
void destroy(svsc_handle handle)
{
	std::vector<std::shared_ptr<Object>> removed;
	{
		std::lock_guard<std::mutex> lock(registryMutex);
		const auto found = objects.find(handle);
		if (found == objects.end())
		{
			return;
		}
		const auto target = found->second;
		for (auto iterator = objects.begin(); iterator != objects.end();)
		{
			bool belongs = false;
			for (auto ancestor = iterator->second; ancestor; ancestor = ancestor->parent)
			{
				if (ancestor == target)
				{
					belongs = true;
					break;
				}
			}
			if (belongs)
			{
				iterator->second->closed.store(true);
				removed.push_back(iterator->second);
				iterator = objects.erase(iterator);
			}
			else
			{
				++iterator;
			}
		}
	}
	for (const auto& object : removed)
	{
		if (object->kind == Kind::Run)
		{
			cancelRun(std::static_pointer_cast<Run>(object));
		}
	}
	for (const auto& object : removed)
	{
		if (object->kind != Kind::Run)
		{
			continue;
		}
		const auto run = std::static_pointer_cast<Run>(object);
		std::unique_lock<std::mutex> lock(run->mutex);
		if (!run->completed.wait_for(lock, std::chrono::seconds(5), [&] { return !run->active; }))
		{
			run->session->worker->terminate();
			run->completed.wait_for(lock, std::chrono::seconds(1), [&] { return !run->active; });
		}
	}
}

svsc_status SVSC_CALL createContext(const char* directory, svsc_handle* result)
{
	if (result)
	{
		*result = 0;
	}
	return call([&] {
		if (!directory || !*directory || !result)
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Runtime directory and output are required");
		}
		if (!fs::is_directory(fs::u8path(directory)))
		{
			throw Error(SVSC_UNAVAILABLE, "Compute runtime directory is missing");
		}
		const auto path = fs::canonical(fs::u8path(directory));
#ifdef _WIN32
		if (!fs::is_regular_file(path / "SVSComputeWorker.exe") || !fs::is_regular_file(path / "onnxruntime.dll"))
#else
		if (!fs::is_regular_file(path / "SVSComputeWorker"))
#endif
		{
			throw Error(SVSC_UNAVAILABLE, "Compute runtime is incomplete");
		}
		*result = add(std::make_shared<Context>(path));
	});
}
svsc_status SVSC_CALL queryDevices(svsc_handle context, char** result)
{
	if (result)
	{
		*result = nullptr;
	}
	return call([&] {
		const auto owner = get<Context>(context, Kind::Context);
		const auto cpu = owner->worker("cpu", "cpu");
		Json devices = Json::array({{{"backend", "cpu"}, {"device", "cpu"}, {"name", "CPU"}, {"available", true}}});
		const auto inventory = cpu->rpc({{"op", "inventory"}});
		for (auto item : inventory.at("devices"))
		{
			try
			{
				const auto gpu = owner->worker("directml", item.at("device").get<std::string>());
				item.update(gpu->rpc({{"op", "probe"}}, 120));
			}
			catch (const Error& error)
			{
				item["available"] = false;
				item["reason"] = error.what();
			}
			devices.push_back(std::move(item));
		}
		outputString({{"runtimeVersion", RuntimeVersion}, {"devices", devices}}, result);
	});
}
svsc_status SVSC_CALL probe(svsc_handle context, const char* type, const char* id, char** result)
{
	if (result)
	{
		*result = nullptr;
	}
	return call([&] {
		backend(type, id);
		const auto owner = get<Context>(context, Kind::Context);
		outputString(owner->worker(type, id)->rpc({{"op", "probe"}}, 120), result);
	});
}
svsc_status SVSC_CALL createModel(svsc_handle context, const svsc_model_desc* desc, svsc_handle* result)
{
	if (result)
	{
		*result = 0;
	}
	return call([&] {
		if (!desc || desc->size < sizeof(*desc) || !result || !desc->authorized_root || !desc->relative_path
			|| !desc->fingerprint)
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Invalid model descriptor");
		}
		const auto owner = get<Context>(context, Kind::Context);
		const auto root = fs::canonical(fs::u8path(desc->authorized_root));
		const auto path = authorizedPath(root, fs::u8path(desc->relative_path));
		Json model{
			{"root", root.u8string()},			{"relative", fs::relative(path, root).generic_u8string()},
			{"fingerprint", desc->fingerprint}, {"seed", desc->seed},
			{"bytes", fs::file_size(path)},		{"modified", fs::last_write_time(path).time_since_epoch().count()}};
		*result = add(std::make_shared<Model>(owner, std::move(model)));
	});
}
svsc_status SVSC_CALL querySignature(svsc_handle handle, char** result)
{
	if (result)
	{
		*result = nullptr;
	}
	return call([&] {
		const auto model = get<Model>(handle, Kind::Model);
		const auto cpu = model->context->worker("cpu", "cpu");
		const auto reply = cpu->rpc({{"op", "session"}, {"model", model->descriptor}}, 120);
		cpu->rpc({{"op", "release"}, {"session", reply.at("session")}}, 5);
		outputString(reply.at("signature"), result);
	});
}
svsc_status SVSC_CALL createSession(svsc_handle handle, const svsc_session_desc* desc, svsc_handle* result)
{
	if (result)
	{
		*result = 0;
	}
	return call([&] {
		if (!desc || desc->size < sizeof(*desc) || !result || !desc->stage || desc->allow_cpu_fallback > 1)
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Invalid session descriptor");
		}
		backend(desc->backend, desc->device);
		auto session = std::make_shared<Session>(get<Model>(handle, Kind::Model));
		session->requestedBackend = desc->backend;
		session->requestedDevice = desc->device;
		session->stage = desc->stage;
		session->fallback = desc->allow_cpu_fallback != 0;
		const bool cpuOnly = desc->cpu_only_reason && *desc->cpu_only_reason;
		if (cpuOnly)
		{
			session->reason = desc->cpu_only_reason;
		}
		try
		{
			initialize(*session, cpuOnly ? "cpu" : desc->backend, cpuOnly ? "cpu" : desc->device);
		}
		catch (const Error& error)
		{
			if (cpuOnly || session->requestedBackend != "directml" || !session->fallback
				|| (error.status != SVSC_BACKEND_FAILURE && error.status != SVSC_WORKER_LOST
					&& error.status != SVSC_UNAVAILABLE))
			{
				throw;
			}
			session->reason = error.what();
			initialize(*session, "cpu", "cpu");
		}
		*result = add(std::move(session));
	});
}
svsc_status SVSC_CALL createRun(svsc_handle session, svsc_handle* result)
{
	if (result)
	{
		*result = 0;
	}
	return call([&] {
		if (!result)
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Null run output");
		}
		*result = add(std::make_shared<Run>(get<Session>(session, Kind::Session)));
	});
}
svsc_status SVSC_CALL run(svsc_handle handle, const svsc_tensor* inputs, uint32_t count, svsc_handle* output)
{
	if (output)
	{
		*output = 0;
	}
	return call([&] {
		RenderLease lease;
		if (!output || !inputs || !count || count > 256)
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Invalid run tensor count");
		}
		const auto value = get<Run>(handle, Kind::Run);
		{
			std::lock_guard<std::mutex> lock(value->mutex);
			if (value->used)
			{
				throw Error(SVSC_INVALID_ARGUMENT, "Run is single-use");
			}
			value->used = true;
			value->active = true;
		}
		struct Completion
		{
			std::shared_ptr<Run> value;
			~Completion()
			{
				std::lock_guard<std::mutex> lock(value->mutex);
				value->buffer.reset();
				value->active = false;
				value->completed.notify_all();
			}
		} completion{value};
		if (value->cancelled.load())
		{
			throw Error(SVSC_CANCELLED, "Run cancelled");
		}
		auto sharedAllocation = std::make_shared<Allocation>(value->session->model->context, BufferLimit);
		auto buffer = std::shared_ptr<SharedBuffer>(new SharedBuffer(),
													[sharedAllocation](SharedBuffer* memory) { delete memory; });
		{
			std::lock_guard<std::mutex> lock(value->mutex);
			value->buffer = buffer;
			if (value->cancelled.load())
			{
				buffer->cancel();
			}
		}
		Json tensors = Json::array();
		size_t offset = 0;
		std::set<std::string> names;
		for (uint32_t i = 0; i < count; ++i)
		{
			const auto& input = inputs[i];
			if (input.size < sizeof(input) || !input.name || !*input.name || input.rank > SVSC_MAX_RANK
				|| (input.rank && !input.dims) || input.reserved || !names.insert(input.name).second)
			{
				throw Error(SVSC_INVALID_ARGUMENT, "Invalid/duplicate tensor descriptor");
			}
			std::vector<int64_t> dims;
			if (input.rank)
			{
				dims.assign(input.dims, input.dims + input.rank);
			}
			const auto bytes = tensorBytes(input.dtype, dims);
			offset = (offset + 7) & ~size_t(7);
			if (input.byte_count != bytes || offset > BufferLimit || bytes > BufferLimit - offset)
			{
				throw Error(SVSC_INVALID_ARGUMENT, "Tensor byte count mismatch or aggregate budget exceeded");
			}
			validateValues(input.dtype, input.data, bytes);
			if (bytes)
			{
				std::memcpy(buffer->payload() + offset, input.data, bytes);
			}
			tensors.push_back(
				{{"name", input.name}, {"dtype", input.dtype}, {"dims", dims}, {"bytes", bytes}, {"offset", offset}});
			offset += bytes;
		}
		auto& session = *value->session;
		if (session.worker->lost.load())
		{
			throw Error(SVSC_WORKER_LOST, "Session belongs to a lost worker epoch");
		}
		Json response;
		try
		{
			response = session.worker->rpc(
				{{"op", "run"}, {"session", session.remote}, {"buffer", buffer->name}, {"inputs", tensors}});
		}
		catch (const Error& error)
		{
			if (value->cancelled.load())
			{
				throw Error(SVSC_CANCELLED, "Run cancelled");
			}
			if (session.worker->backend != "directml" || !session.fallback
				|| (error.status != SVSC_BACKEND_FAILURE && error.status != SVSC_WORKER_LOST))
			{
				throw;
			}
			Session cpu(session.model);
			initialize(cpu, "cpu", "cpu");
			response = cpu.worker->rpc(
				{{"op", "run"}, {"session", cpu.remote}, {"buffer", buffer->name}, {"inputs", tensors}});
			response["execution"]["fallbackReason"] = error.what();
		}
		if (value->cancelled.load() || value->closed.load())
		{
			throw Error(SVSC_CANCELLED, "Cancelled result discarded");
		}
		auto result = std::make_shared<Result>(value);
		const auto outputs = response.at("outputs");
		if (!outputs.is_array() || outputs.size() > 256)
		{
			throw Error(SVSC_WORKER_LOST, "Invalid worker output count");
		}
		size_t resultBytes = 0;
		for (const auto& tensor : outputs)
		{
			const auto bytes
				= tensorBytes(tensor.at("dtype").get<uint32_t>(), tensor.at("dims").get<std::vector<int64_t>>());
			if (bytes > BufferLimit - resultBytes)
			{
				throw Error(SVSC_WORKER_LOST, "Worker output aggregate budget exceeded");
			}
			resultBytes += bytes;
		}
		result->allocation = std::make_shared<Allocation>(session.model->context, resultBytes);
		for (const auto& tensor : outputs)
		{
			const auto dtype = tensor.at("dtype").get<uint32_t>();
			const auto dims = tensor.at("dims").get<std::vector<int64_t>>();
			const auto bytes = tensorBytes(dtype, dims);
			const auto start = tensor.at("offset").get<uint64_t>();
			if (tensor.at("bytes") != bytes || start % 8 || start > BufferLimit || bytes > BufferLimit - start)
			{
				throw Error(SVSC_WORKER_LOST, "Invalid worker output dimensions/offset/bytes");
			}
			validateValues(dtype, buffer->payload() + start, bytes);
			result->shapes.push_back(dims);
			result->names.push_back(tensor.at("name").get<std::string>());
			result->bytes.emplace_back(bytes);
			if (bytes)
			{
				std::memcpy(result->bytes.back().data(), buffer->payload() + start, bytes);
			}
			result->tensors.push_back(
				{sizeof(svsc_tensor), dtype, nullptr, uint32_t(dims.size()), 0, nullptr, bytes, nullptr});
		}
		for (size_t i = 0; i < result->tensors.size(); ++i)
		{
			result->tensors[i].name = result->names[i].c_str();
			result->tensors[i].dims = result->shapes[i].data();
			result->tensors[i].data = result->bytes[i].data();
		}
		response["execution"]["requestedBackend"] = session.requestedBackend;
		response["execution"]["requestedDevice"] = session.requestedDevice;
		response["execution"]["stage"] = session.stage;
		if (!session.reason.empty())
		{
			response["execution"]["fallbackReason"] = session.reason;
		}
		result->execution = response.at("execution").dump();
		result->view = {sizeof(svsc_result), uint32_t(result->tensors.size()), result->tensors.data(),
						result->execution.c_str()};
		*output = add(std::move(result));
	});
}
svsc_status SVSC_CALL cancel(svsc_handle handle)
{
	return call([&] { cancelRun(get<Run>(handle, Kind::Run)); });
}
svsc_status SVSC_CALL getResult(svsc_handle handle, const svsc_result** output)
{
	if (output)
	{
		*output = nullptr;
	}
	return call([&] {
		if (!output)
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Null result view output");
		}
		*output = &get<Result>(handle, Kind::Result)->view;
	});
}
void SVSC_CALL release(svsc_handle handle)
{
	try
	{
		destroy(handle);
	}
	catch (...)
	{
	}
}
void SVSC_CALL releaseString(char* value)
{
	delete[] value;
}
const char* SVSC_CALL error()
{
	return lastError.c_str();
}

svsc_status SVSC_CALL setMemoryPolicy(const char* policy, uint32_t seconds)
{
	return call([&] {
		if (!policy)
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Missing memory policy");
		}
		Residency::instance().configure(policy, seconds);
	});
}
svsc_status SVSC_CALL beginRender()
{
	return call([] { Residency::instance().begin(); });
}
svsc_status SVSC_CALL endRender()
{
	return call([] { Residency::instance().end(); });
}
svsc_status SVSC_CALL memoryStatus(svsc_handle handle, char** output)
{
	return call([&] {
		const auto context = get<Context>(handle, Kind::Context);
		auto report = Residency::instance().status();
		std::shared_ptr<Worker> worker, cpu;
		{
			std::lock_guard<std::mutex> lock(context->mutex);
			worker = context->dml;
			cpu = context->cpu;
		}
		report["gpu"] = worker && !worker->lost.load() ? worker->rpc({{"op", "memory"}}) : Json::object();
		report["cpu"] = cpu && !cpu->lost.load() ? cpu->rpc({{"op", "memory"}}) : Json::object();
		outputString(report, output);
	});
}

const svsc_api api{sizeof(svsc_api),
				   SVSC_ABI_VERSION,
				   SVSC_FEATURE_CPU | SVSC_FEATURE_ISOLATED_WORKER | SVSC_FEATURE_CANCEL | SVSC_FEATURE_MEMORY_POLICY
#ifdef SVSC_HAS_DML
					   | SVSC_FEATURE_DIRECTML
#endif
				   ,
				   RuntimeVersion,
				   createContext,
				   queryDevices,
				   probe,
				   createModel,
				   querySignature,
				   createSession,
				   createRun,
				   run,
				   cancel,
				   getResult,
				   release,
				   release,
				   release,
				   release,
				   release,
				   releaseString,
				   error,
				   setMemoryPolicy,
				   beginRender,
				   endRender,
				   memoryStatus};
} // namespace svsc

extern "C" SVSC_EXPORT svsc_status SVSC_CALL svsc_get_api(uint32_t version, uint32_t size, const svsc_api** output)
{
	if (output)
	{
		*output = nullptr;
	}
	if (!output || (version >> 16) != 1 || (version & 0xffff) > 0 || size > sizeof(svsc_api)
		|| size < SVSC_API_REQUIRED_SIZE)
	{
		return SVSC_VERSION_MISMATCH;
	}
	*output = &svsc::api;
	return SVSC_OK;
}
