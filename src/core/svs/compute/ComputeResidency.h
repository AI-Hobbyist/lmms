#ifndef SVSC_COMPUTE_RESIDENCY_H
#define SVSC_COMPUTE_RESIDENCY_H

#include <condition_variable>
#include <thread>

#include "ComputeTransport.h"

namespace svsc {
// One policy and render lease count for every AI context loaded through this client.
class Residency
{
public:
	static Residency& instance()
	{
		static Residency value;
		return value;
	}
	void contextOpened()
	{
		std::lock_guard<std::mutex> lifecycle(m_lifecycle);
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_contexts++ == 0)
		{
			m_stopping = false;
			m_thread = std::thread([this] { monitor(); });
		}
	}
	void contextClosed()
	{
		std::lock_guard<std::mutex> lifecycle(m_lifecycle);
		bool stop = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			stop = --m_contexts == 0;
			if (stop)
			{
				m_stopping = true;
				m_wake.notify_all();
			}
		}
		if (stop && m_thread.joinable())
		{
			m_thread.join();
		}
	}
	void add(const std::shared_ptr<Worker>& worker)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_workers.erase(
			std::remove_if(m_workers.begin(), m_workers.end(), [](const auto& item) { return item.expired(); }),
			m_workers.end());
		m_workers.push_back(worker);
		m_idleSince = std::chrono::steady_clock::now();
		m_armed = true;
	}
	void configure(const std::string& policy, uint32_t seconds)
	{
		if ((policy != "immediate" && policy != "idle" && policy != "resident") || seconds < 1 || seconds > 86400)
		{
			throw Error(SVSC_INVALID_ARGUMENT,
						"Memory policy must be immediate/idle/resident; idle seconds must be 1–86400");
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_policy == policy && m_seconds == seconds)
		{
			return;
		}
		m_policy = policy;
		m_seconds = seconds;
		m_armed = true;
		if (!m_active && m_policy == "immediate")
		{
			trim();
		}
		m_wake.notify_all();
	}
	void begin()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_active;
		m_armed = true;
	}
	void end()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_active)
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Unbalanced compute render lease");
		}
		if (--m_active == 0)
		{
			m_idleSince = std::chrono::steady_clock::now();
			if (m_policy == "immediate")
			{
				trim();
			}
		}
		m_wake.notify_all();
	}
	Json status()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return {
			{"policy", m_policy}, {"idleSeconds", m_seconds}, {"activeRenders", m_active}, {"releaseError", m_error}};
	}
	~Residency()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stopping = true;
			m_wake.notify_all();
		}
		if (m_thread.joinable())
		{
			m_thread.join();
		}
	}

private:
	Residency() = default;
	void monitor()
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		while (!m_stopping)
		{
			m_wake.wait_for(lock, std::chrono::milliseconds(250));
			if (!m_stopping && m_armed && !m_active && m_policy == "idle"
				&& std::chrono::steady_clock::now() - m_idleSince >= std::chrono::seconds(m_seconds))
			{
				trim();
			}
		}
	}
	void trim()
	{
		m_error.clear();
		for (const auto& item : m_workers)
		{
			if (auto worker = item.lock(); worker && !worker->lost.load())
			{
				try
				{
					worker->rpc({{"op", "trim"}}, 30);
				}
				catch (const std::exception& error)
				{
					m_error = error.what();
				}
			}
		}
		m_armed = false;
	}
	std::mutex m_mutex;
	std::mutex m_lifecycle;
	std::condition_variable m_wake;
	std::vector<std::weak_ptr<Worker>> m_workers;
	std::string m_policy = "idle", m_error;
	uint32_t m_seconds = 60, m_active = 0;
	uint32_t m_contexts = 0;
	bool m_armed = false, m_stopping = false;
	std::chrono::steady_clock::time_point m_idleSince = std::chrono::steady_clock::now();
	std::thread m_thread;
};
struct RenderLease
{
	RenderLease() { Residency::instance().begin(); }
	~RenderLease() { Residency::instance().end(); }
};
} // namespace svsc
#endif
