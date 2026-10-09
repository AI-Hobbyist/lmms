#pragma once

#include <memory>

#include "svc.h"

namespace svc {
struct StreamDeleter
{
	void operator()(svc_stream* stream) const { svc_stream_destroy(stream); }
};
using Stream = std::unique_ptr<svc_stream, StreamDeleter>;

class Job
{
public:
	Job(const svc_engine& engine, const svc_request& request)
		: m_engine(engine)
		, m_job(engine.start(nullptr, &request))
	{
	}
	~Job()
	{
		if (m_job)
		{
			m_engine.cancel(m_job);
			m_engine.destroy(m_job);
		}
	}
	Job(const Job&) = delete;
	Job& operator=(const Job&) = delete;
	svc_status pump() { return m_job ? m_engine.pump(m_job) : SVC_INVALID; }
	void cancel()
	{
		if (m_job) { m_engine.cancel(m_job); }
	}
	explicit operator bool() const { return m_job != nullptr; }

private:
	const svc_engine& m_engine;
	void* m_job;
};
} // namespace svc
