#ifndef LMMS_SVS_SYNTHESIS_SCHEDULER_H
#define LMMS_SVS_SYNTHESIS_SCHEDULER_H
#include "SVSModel.h"
#include "SVSSegmentedSynthesis.h"
#include <QObject>
#include <QThreadPool>
#include <QSet>
namespace lmms::svs {
// All queue/state callbacks have owner-thread affinity. Workers own snapshots
// and plugin references; cancelled running jobs retain their slot until exit.
class SynthesisScheduler : public QObject
{
public:
	using StateCallback = std::function<void(const QString&)>;
	using ResultCallback = std::function<void(std::shared_ptr<const Audio>, const QString&)>;
	using PartialCallback = std::function<void(std::shared_ptr<const Audio>, QVector<SynthesisSegment>)>;
	static SynthesisScheduler& instance();
	SynthesisScheduler(int budget = 2, QObject* parent = nullptr);
	~SynthesisScheduler() override;
	std::shared_ptr<RenderControl> submit(std::shared_ptr<Plugin>, Input, int priority, StateCallback, ResultCallback,
		PartialCallback = {}, QVector<SynthesisSegment> retained = {});
	void cancel(const std::shared_ptr<RenderControl>&);
	void cancelAll();
	void shutdown();
	QThreadPool& declarationPool() { return m_declarationPool; }
	int activeCount() const { return m_active; }
	int queuedCount() const { return int(m_queue.size()); }
	int peakActiveCount() const { return m_peak; }
	int budget() const { return m_budget; }

private:
	struct Job
	{
		std::shared_ptr<Plugin> plugin;
		Input input;
		int priority;
		std::shared_ptr<RenderControl> control;
		StateCallback state;
		ResultCallback result;
		PartialCallback partial;
		QVector<SynthesisSegment> retained;
	};
	void dispatch();
	int m_budget, m_active = 0, m_peak = 0;
	QThreadPool m_pool;
	QThreadPool m_declarationPool;
	bool m_stopping = false;
	QVector<std::shared_ptr<Job>> m_queue, m_running;
	QSet<Plugin*> m_busy;
};
}
#endif
