#ifndef SVS_COMPUTE_HPP
#define SVS_COMPUTE_HPP
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "svs_compute.h"
#ifdef _WIN32
#pragma push_macro("NOMINMAX")
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#pragma pop_macro("NOMINMAX")
#else
#include <dlfcn.h>
#endif

namespace svs_compute {
struct Error : std::runtime_error
{
	svsc_status status;
	Error(svsc_status value, const std::string& text)
		: std::runtime_error(text)
		, status(value)
	{
	}
};
namespace detail {
inline std::string utf8(const std::filesystem::path& path)
{
	const auto bytes = path.u8string();
	return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
struct LibraryState
{
	const svsc_api* api = nullptr;
#ifdef _WIN32
	HMODULE module = nullptr;
#else
	void* module = nullptr;
#endif
	~LibraryState()
	{
#ifdef _WIN32
		if (module)
		{
			FreeLibrary(module);
		}
#else
		if (module)
		{
			dlclose(module);
		}
#endif
	}
	void check(svsc_status status) const
	{
		if (status != SVSC_OK)
		{
			throw Error(status, api && api->last_error ? api->last_error() : "Compute ABI negotiation failed");
		}
	}
	void requireMemoryPolicy() const
	{
		if (api->size < sizeof(svsc_api) || !(api->features & SVSC_FEATURE_MEMORY_POLICY))
		{
			throw Error(SVSC_UNAVAILABLE, "Compute runtime does not support memory policy");
		}
	}
};
struct State
{
	std::shared_ptr<LibraryState> library;
	std::shared_ptr<State> parent;
	svsc_handle handle = 0;
	void(SVSC_CALL* release)(svsc_handle) = nullptr;
	State(std::shared_ptr<LibraryState> owner, svsc_handle value, void(SVSC_CALL* destroy)(svsc_handle),
		  std::shared_ptr<State> ancestor = {})
		: library(std::move(owner))
		, parent(std::move(ancestor))
		, handle(value)
		, release(destroy)
	{
	}
	~State()
	{
		if (handle)
		{
			release(handle);
		}
	}
};
inline std::string string(std::shared_ptr<State> owner, svsc_status status, char* value)
{
	std::unique_ptr<char, void(SVSC_CALL*)(char*)> text(value, owner->library->api->release_string);
	owner->library->check(status);
	if (!text)
	{
		throw Error(SVSC_INTERNAL_ERROR, "Compute returned a null string");
	}
	return text.get();
}
} // namespace detail

class RenderLease
{
	std::shared_ptr<detail::LibraryState> m_library;

public:
	explicit RenderLease(std::shared_ptr<detail::LibraryState> library)
		: m_library(std::move(library))
	{
		m_library->requireMemoryPolicy();
		m_library->check(m_library->api->begin_render());
	}
	RenderLease(const RenderLease&) = delete;
	RenderLease& operator=(const RenderLease&) = delete;
	~RenderLease() { m_library->api->end_render(); }
};

class Result
{
	std::shared_ptr<detail::State> m_state;
	friend class Run;
	explicit Result(std::shared_ptr<detail::State> state)
		: m_state(std::move(state))
	{
	}

public:
	const svsc_result& value() const
	{
		const svsc_result* result = nullptr;
		m_state->library->check(m_state->library->api->get_result(m_state->handle, &result));
		if (!result || result->size < sizeof(*result))
		{
			throw Error(SVSC_INTERNAL_ERROR, "Invalid compute result view");
		}
		return *result;
	}
};
class Run
{
	std::shared_ptr<detail::State> m_state;
	friend class Session;
	explicit Run(std::shared_ptr<detail::State> state)
		: m_state(std::move(state))
	{
	}

public:
	Result run(const std::vector<svsc_tensor>& inputs) const
	{
		svsc_handle result = 0;
		const auto library = m_state->library;
		library->check(library->api->run(m_state->handle, inputs.data(), uint32_t(inputs.size()), &result));
		return Result(std::make_shared<detail::State>(library, result, library->api->release_result, m_state));
	}
	void cancel() const { m_state->library->check(m_state->library->api->cancel(m_state->handle)); }
};
class Session
{
	std::shared_ptr<detail::State> m_state;
	friend class Model;
	explicit Session(std::shared_ptr<detail::State> state)
		: m_state(std::move(state))
	{
	}

public:
	Run createRun() const
	{
		svsc_handle result = 0;
		const auto library = m_state->library;
		library->check(library->api->create_run(m_state->handle, &result));
		return Run(std::make_shared<detail::State>(library, result, library->api->destroy_run, m_state));
	}
};
class Model
{
	std::shared_ptr<detail::State> m_state;
	friend class Context;
	explicit Model(std::shared_ptr<detail::State> state)
		: m_state(std::move(state))
	{
	}

public:
	std::string signature() const
	{
		char* text = nullptr;
		const auto status = m_state->library->api->query_model_signature(m_state->handle, &text);
		return detail::string(m_state, status, text);
	}
	Session session(const svsc_session_desc& desc) const
	{
		svsc_handle result = 0;
		const auto library = m_state->library;
		library->check(library->api->create_session(m_state->handle, &desc, &result));
		return Session(std::make_shared<detail::State>(library, result, library->api->destroy_session, m_state));
	}
};
class Context
{
	std::shared_ptr<detail::State> m_state;
	friend class Library;
	explicit Context(std::shared_ptr<detail::State> state)
		: m_state(std::move(state))
	{
	}

public:
	std::shared_ptr<RenderLease> retainModels() const { return std::make_shared<RenderLease>(m_state->library); }
	std::string memoryStatus() const
	{
		m_state->library->requireMemoryPolicy();
		char* text = nullptr;
		const auto status = m_state->library->api->memory_status(m_state->handle, &text);
		return detail::string(m_state, status, text);
	}
	std::string devices() const
	{
		char* text = nullptr;
		const auto status = m_state->library->api->query_backend_devices(m_state->handle, &text);
		return detail::string(m_state, status, text);
	}
	std::string probe(const char* backend, const char* device) const
	{
		char* text = nullptr;
		const auto status = m_state->library->api->probe(m_state->handle, backend, device, &text);
		return detail::string(m_state, status, text);
	}
	Model model(const svsc_model_desc& desc) const
	{
		svsc_handle result = 0;
		const auto library = m_state->library;
		library->check(library->api->create_model(m_state->handle, &desc, &result));
		return Model(std::make_shared<detail::State>(library, result, library->api->destroy_model, m_state));
	}
};
class Library
{
	std::shared_ptr<detail::LibraryState> m_state = std::make_shared<detail::LibraryState>();

public:
	explicit Library(const std::filesystem::path& path)
	{
		using Entry = svsc_status(SVSC_CALL*)(uint32_t, uint32_t, const svsc_api**);
#ifdef _WIN32
		m_state->module = LoadLibraryExW(std::filesystem::absolute(path).c_str(), nullptr,
										 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		const auto entry
			= m_state->module ? reinterpret_cast<Entry>(GetProcAddress(m_state->module, "svsc_get_api")) : nullptr;
#else
		m_state->module = dlopen(std::filesystem::absolute(path).c_str(), RTLD_NOW | RTLD_LOCAL);
		const auto entry = m_state->module ? reinterpret_cast<Entry>(dlsym(m_state->module, "svsc_get_api")) : nullptr;
#endif
		if (!entry)
		{
			throw Error(SVSC_UNAVAILABLE, "Cannot load SVSCompute runtime: " + detail::utf8(path));
		}
		m_state->check(entry(SVSC_ABI_VERSION, SVSC_API_REQUIRED_SIZE, &m_state->api));
		if (!m_state->api || m_state->api->size < SVSC_API_REQUIRED_SIZE
			|| m_state->api->abi_version != SVSC_ABI_VERSION)
		{
			throw Error(SVSC_VERSION_MISMATCH, "Incompatible SVSCompute ABI");
		}
	}
	Context context(const std::filesystem::path& directory) const
	{
		svsc_handle handle = 0;
		m_state->check(m_state->api->create_context(detail::utf8(directory).c_str(), &handle));
		return Context(std::make_shared<detail::State>(m_state, handle, m_state->api->destroy_context));
	}
	const svsc_api& api() const { return *m_state->api; }
	void setMemoryPolicy(const char* policy, uint32_t idleSeconds = 60) const
	{
		m_state->requireMemoryPolicy();
		m_state->check(m_state->api->set_memory_policy(policy, idleSeconds));
	}
	std::shared_ptr<RenderLease> retainModels() const { return std::make_shared<RenderLease>(m_state); }
};
} // namespace svs_compute
#endif
