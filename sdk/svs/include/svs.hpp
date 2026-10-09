/* Caller-side C++17 conveniences. Only the C types in svs.h cross module boundaries. */
#ifndef SVS_CPP_HPP
#define SVS_CPP_HPP
#include "svs.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace svs_sdk {
inline svs_api negotiate(svs_get_api_fn entry)
{
	svs_api api{};
	if (!entry || entry(SVS_ABI_MAJOR, SVS_ABI_MINOR, sizeof(api), &api) != SVS_OK || api.major != SVS_ABI_MAJOR
		|| api.size < SVS_API_REQUIRED_SIZE || !api.create_engine || !api.destroy_engine || !api.catalog
		|| !api.capabilities || !api.release_string || !api.create_session || !api.destroy_session || !api.submit
		|| !api.render || !api.cancel || !api.release_result)
		throw std::runtime_error("Incompatible SVS ABI");
	if (!SVS_HAS_FIELD(api, svs_api, pronunciation))
		api.pronunciation = nullptr;
	if (!SVS_HAS_FIELD(api, svs_api, open_resource))
		api.open_resource = nullptr;
	if (!SVS_HAS_FIELD(api, svs_api, read_resource))
		api.read_resource = nullptr;
	if (!SVS_HAS_FIELD(api, svs_api, close_resource))
		api.close_resource = nullptr;
	if (!SVS_HAS_FIELD(api, svs_api, query_ranges))
		api.query_ranges = nullptr;
	if (!SVS_HAS_FIELD(api, svs_api, query_engine_settings))
		api.query_engine_settings = nullptr;
	if (!SVS_HAS_FIELD(api, svs_api, query_catalog))
		api.query_catalog = nullptr;
	return api;
}
namespace detail {
struct EngineState
{
	svs_api api{};
	svs_engine handle = nullptr;
	~EngineState()
	{
		if (handle)
			api.destroy_engine(handle);
	}
};
struct SessionState
{
	std::shared_ptr<EngineState> engine;
	svs_session handle = nullptr;
	unsigned results = 0;
	~SessionState()
	{
		if (handle)
		{
			engine->api.cancel(handle);
			engine->api.destroy_session(handle);
		}
	}
};
}
class String
{
	std::shared_ptr<detail::EngineState> m_engine;
	const char* m_text = nullptr;

public:
	String(std::shared_ptr<detail::EngineState> engine, const char* text)
		: m_engine(std::move(engine))
		, m_text(text)
	{
	}
	String(const String&) = delete;
	String& operator=(const String&) = delete;
	String(String&& other) noexcept
		: m_engine(std::move(other.m_engine))
		, m_text(std::exchange(other.m_text, nullptr))
	{
	}
	~String()
	{
		if (m_text)
			m_engine->api.release_string(m_engine->handle, m_text);
	}
	const char* c_str() const noexcept { return m_text ? m_text : ""; }
	std::string copy() const { return c_str(); }
};
class Result
{
	std::shared_ptr<detail::SessionState> m_session;
	svs_result m_result{};
	svs_status m_status = SVS_FAILED;
	friend class Session;
	explicit Result(std::shared_ptr<detail::SessionState> session)
		: m_session(std::move(session))
	{
		m_result.size = sizeof(m_result);
		++m_session->results;
		m_status = m_session->engine->api.render(m_session->handle, &m_result);
	}

public:
	Result(const Result&) = delete;
	Result& operator=(const Result&) = delete;
	Result(Result&& other) noexcept
		: m_session(std::move(other.m_session))
		, m_result(other.m_result)
		, m_status(other.m_status)
	{
	}
	~Result()
	{
		if (m_session)
		{
			m_session->engine->api.release_result(m_session->handle, &m_result);
			--m_session->results;
		}
	}
	svs_status status() const noexcept { return m_status; }
	const svs_result& value() const noexcept { return m_result; }
};
class Resource
{
	std::shared_ptr<detail::EngineState> m_engine;
	svs_resource m_handle = nullptr;
	svs_resource_info m_info{};
	friend class Engine;
	Resource(std::shared_ptr<detail::EngineState> engine, const char* id)
		: m_engine(std::move(engine))
	{
		const auto& api = m_engine->api;
		m_info.size = sizeof(m_info);
		if (!api.open_resource || !api.read_resource || !api.close_resource)
			throw std::runtime_error("SVS resource API unavailable");
		const auto status = api.open_resource(m_engine->handle, id, &m_handle, &m_info);
		if (status != SVS_OK || !m_handle || m_info.size < sizeof(m_info))
		{
			if (m_handle)
				api.close_resource(m_engine->handle, m_handle);
			m_handle = nullptr;
			throw std::runtime_error("SVS resource unavailable");
		}
	}

public:
	Resource(const Resource&) = delete;
	Resource& operator=(const Resource&) = delete;
	Resource(Resource&& other) noexcept
		: m_engine(std::move(other.m_engine))
		, m_handle(std::exchange(other.m_handle, nullptr))
		, m_info(other.m_info)
	{
	}
	~Resource()
	{
		if (m_handle)
			m_engine->api.close_resource(m_engine->handle, m_handle);
	}
	const svs_resource_info& info() const noexcept { return m_info; }
	std::vector<uint8_t> read(uint64_t maximum = 64u * 1024 * 1024) const
	{
		if (m_info.byte_count > maximum || m_info.byte_count > SIZE_MAX)
			throw std::runtime_error("SVS resource exceeds size limit");
		std::vector<uint8_t> bytes(size_t(m_info.byte_count));
		uint64_t offset = 0;
		while (offset < m_info.byte_count)
		{
			uint64_t count = 0;
			const auto remaining = m_info.byte_count - offset;
			if (m_engine->api.read_resource(
					m_engine->handle, m_handle, offset, bytes.data() + offset, remaining, &count)
					!= SVS_OK
				|| !count || count > remaining)
				throw std::runtime_error("Incomplete SVS resource");
			offset += count;
		}
		return bytes;
	}
};
class Session
{
	std::shared_ptr<detail::SessionState> m_state;
	friend class Engine;
	Session(std::shared_ptr<detail::EngineState> engine, const char* voice)
		: m_state(std::make_shared<detail::SessionState>())
	{
		m_state->engine = std::move(engine);
		if (m_state->engine->api.create_session(m_state->engine->handle, voice, &m_state->handle) != SVS_OK
			|| !m_state->handle)
			throw std::runtime_error("SVS voice session unavailable");
	}

public:
	Session(const Session&) = delete;
	Session& operator=(const Session&) = delete;
	Session(Session&&) = default;
	Session& operator=(Session&&) = default;
	svs_status submit(const svs_snapshot& snapshot)
	{
		if (m_state->results)
			return SVS_INVALID_INPUT;
		return m_state->engine->api.submit(m_state->handle, &snapshot);
	}
	Result render()
	{
		if (m_state->results)
			throw std::runtime_error("Release the previous SVS result before rendering");
		return Result(m_state);
	}
	void cancel() const { m_state->engine->api.cancel(m_state->handle); }
	String ranges() const
	{
		const char* text = nullptr;
		const auto& api = m_state->engine->api;
		if (!api.query_ranges || api.query_ranges(m_state->handle, &text) != SVS_OK || !text)
		{
			if (text)
				api.release_string(m_state->engine->handle, text);
			throw std::runtime_error("SVS range query unavailable");
		}
		return String(m_state->engine, text);
	}
};
class Engine
{
	std::shared_ptr<detail::EngineState> m_state;
	template <class Query> String query(Query callback) const
	{
		const char* text = nullptr;
		if (callback(&text) != SVS_OK || !text)
		{
			if (text)
				m_state->api.release_string(m_state->handle, text);
			throw std::runtime_error("SVS declaration query failed");
		}
		return String(m_state, text);
	}

public:
	explicit Engine(svs_get_api_fn entry, const svs_host* host = nullptr)
		: m_state(std::make_shared<detail::EngineState>())
	{
		m_state->api = negotiate(entry);
		svs_host empty{};
		empty.size = sizeof(empty);
		if (m_state->api.create_engine(host ? host : &empty, &m_state->handle) != SVS_OK || !m_state->handle)
			throw std::runtime_error("SVS engine initialization failed");
	}
	Engine(const Engine&) = delete;
	Engine& operator=(const Engine&) = delete;
	Engine(Engine&&) = default;
	Engine& operator=(Engine&&) = default;
	String catalog() const
	{
		return query([&](const char** out) { return m_state->api.catalog(m_state->handle, out); });
	}
	bool hasCatalogQuery() const { return m_state->api.query_catalog != nullptr; }
	String catalog(const char* context) const
	{
		if (!hasCatalogQuery())
			return catalog();
		return query([&](const char** out) { return m_state->api.query_catalog(m_state->handle, context, out); });
	}
	bool hasEngineSettings() const { return m_state->api.query_engine_settings != nullptr; }
	String engineSettings(const char* context = "{}") const
	{
		if (!hasEngineSettings())
			throw std::runtime_error("SVS engine settings API unavailable");
		return query(
			[&](const char** out) { return m_state->api.query_engine_settings(m_state->handle, context, out); });
	}
	String capabilities(const char* voice, const char* context = "{}") const
	{
		return query([&](const char** out) { return m_state->api.capabilities(m_state->handle, voice, context, out); });
	}
	String pronunciation(const char* voice, const char* request) const
	{
		if (!m_state->api.pronunciation)
			throw std::runtime_error("SVS pronunciation API unavailable");
		return query(
			[&](const char** out) { return m_state->api.pronunciation(m_state->handle, voice, request, out); });
	}
	Resource resource(const char* id) const { return Resource(m_state, id); }
	Session session(const char* voice) const { return Session(m_state, voice); }
};
} // namespace svs_sdk
#endif
