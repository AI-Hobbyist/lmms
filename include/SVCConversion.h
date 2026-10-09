#pragma once

#include <QObject>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace lmms {
class SVCClip;
class SVCTrack;
namespace svc {
class ConversionService : public QObject
{
	Q_OBJECT
public:
	static ConversionService& instance();
	void render(SVCTrack* track);
	void cancel(SVCTrack* track);
	void shutdown();
	~ConversionService() override;

private:
	struct Task;
	ConversionService() = default;
	void work();
	void run(const std::shared_ptr<Task>& task);
	bool post(const std::shared_ptr<Task>& task, std::function<void(SVCClip*)> action);
	std::atomic<bool> m_stopping{false};
	std::mutex m_mutex;
	std::condition_variable m_wake;
	std::deque<std::shared_ptr<Task>> m_queue;
	std::thread m_worker;
};
} // namespace svc
} // namespace lmms
