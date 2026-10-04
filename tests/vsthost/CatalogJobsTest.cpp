#include "vsthost/CatalogJobs.h"
#include <QtTest>
#include <QSemaphore>
#include <chrono>
#include <atomic>
#include <stdexcept>
#include <thread>

using namespace lmms::vsthost;
namespace
{
CatalogReport report(const QString& name)
{
	CatalogReport value; CatalogEntry entry; entry.name = name; value.entries.push_back(entry); return value;
}
}
class CatalogJobsTest : public QObject
{
	Q_OBJECT
private slots:
	void latestPendingRequestWins()
	{
		QSemaphore entered, release;
		std::atomic<unsigned> middleCalls = 0;
		CatalogJobs jobs({}, [&](const auto& roots, auto, std::stop_token, auto progress)
		{
			const auto name = roots.front().path;
			if (name == "first") { entered.release(); release.tryAcquire(1, 2000); progress("stale", 99, 100); }
			if (name == "middle") { ++middleCalls; }
			return report(name);
		});
		const auto first = jobs.refresh({{"first"}}, {}); QVERIFY(entered.tryAcquire(1, 2000));
		QElapsedTimer timer; timer.start();
		jobs.refresh({{"middle"}}, {}); const auto last = jobs.refresh({{"last"}}, {});
		QVERIFY(timer.elapsed() < 100); QVERIFY(last > first);
		release.release(); QTRY_COMPARE(jobs.snapshot().published, last);
		const auto snapshot = jobs.snapshot(); QVERIFY(!snapshot.busy && !snapshot.cancelled);
		QVERIFY(snapshot.report && snapshot.report->entries.front().name == "last");
		QCOMPARE(middleCalls.load(), 0u); QVERIFY(snapshot.path.isEmpty());
	}
	void cancellationKeepsPublishedSnapshot()
	{
		QSemaphore entered;
		CatalogJobs jobs({}, [&](const auto& roots, auto, std::stop_token stop, auto progress)
		{
			if (roots.front().path == "blocked")
			{
				progress("native scan", 1, 2); entered.release();
				while (!stop.stop_requested()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
			}
			return report(roots.front().path); // Even an executor ignoring cancelled result flags cannot publish stale work.
		});
		const auto ready = jobs.refresh({{"ready"}}, {}); QTRY_COMPARE(jobs.snapshot().published, ready);
		const auto retained = jobs.snapshot().report;
		jobs.refresh({{"blocked"}}, {}); QVERIFY(entered.tryAcquire(1, 2000));
		QCOMPARE(jobs.snapshot().path, QString("native scan"));
		jobs.cancel(); QTRY_VERIFY(!jobs.snapshot().busy);
		QVERIFY(jobs.snapshot().cancelled); QCOMPARE(jobs.snapshot().published, ready);
		QVERIFY(jobs.snapshot().report == retained && retained->entries.front().name == "ready");
		const auto recovered = jobs.refresh({{"recovered"}}, {});
		QTRY_COMPARE(jobs.snapshot().published, recovered); QVERIFY(!jobs.snapshot().cancelled);
	}
	void exceptionAndDestructorCancellation()
	{
		QSemaphore entered;
		QElapsedTimer timer;
		{
			CatalogJobs jobs({}, [&](const auto& roots, auto, std::stop_token stop, auto) -> CatalogReport
			{
				if (roots.front().path == "throw") { throw std::runtime_error("fixture"); }
				entered.release();
				while (!stop.stop_requested()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
				return {};
			});
			const auto failed = jobs.refresh({{"throw"}}, {}); QTRY_COMPARE(jobs.snapshot().published, failed);
			const auto state = jobs.snapshot(); QVERIFY(state.report && state.report->failures.size() == 1);
			QVERIFY(state.report->failures.front().error == Error::InitializationFailed);
			jobs.refresh({{"blocked"}}, {}); QVERIFY(entered.tryAcquire(1, 2000)); timer.start();
		}
		QVERIFY(timer.elapsed() < 500);
	}
};
QTEST_GUILESS_MAIN(CatalogJobsTest)
#include "CatalogJobsTest.moc"
