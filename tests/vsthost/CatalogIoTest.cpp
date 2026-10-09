#include "vsthost/CatalogIo.h"
#include "vsthost/CatalogJobs.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <future>
#include <iostream>
#include <stdexcept>
#include <windows.h>

using namespace lmms::vsthost;
namespace {
void check(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}
QJsonObject pe(const QString& path)
{
	return {{"op", "pe"}, {"path", path}, {"dll", false}};
}
struct StartedEvent
{
	HANDLE handle;
	explicit StartedEvent(const QString& path)
	{
		const auto name = QStringLiteral("Local\\LMMS-CatalogIoFixture-")
			+ QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
		handle = CreateEventW(nullptr, TRUE, FALSE, name.toStdWString().c_str());
		check(handle != nullptr, "CreateEvent");
	}
	~StartedEvent() { CloseHandle(handle); }
	void wait()
	{
		check(WaitForSingleObject(handle, 3000) == WAIT_OBJECT_0, "worker reached blocking filesystem operation");
	}
};
void idle(CatalogJobs& jobs)
{
	QElapsedTimer timer;
	timer.start();
	while (jobs.snapshot().busy && timer.elapsed() < 3000)
	{
		Sleep(5);
	}
	check(!jobs.snapshot().busy, "catalog job terminates within three seconds");
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	try
	{
		check(argc == 5, "arguments: production worker, fixture worker, native helper, factory fixture");
		const auto args = app.arguments();
		QTemporaryDir directory;
		check(directory.isValid(), "temporary directory");
		const auto hang = directory.path() + "/__hang_io__";
		const auto crash = directory.path() + "/__crash_io__";
		const auto bad = directory.path() + "/__bad_io__";
		CatalogIo production(args[1], 2000, {}), fixture(args[2], 500, {});
		auto result = production.request(pe(args[1]));
		check(result.error == Error::None && result.object.value("valid").toBool(), "real PE operation");
		check(production.request(pe("relative.exe")).error == Error::InvalidMessage, "relative path rejected");
		check(production.request({{"op", "unknown"}}).error == Error::InvalidMessage, "unknown operation rejected");
		StartedEvent started(hang);
		QElapsedTimer timer;
		timer.start();
		result = fixture.request(pe(hang));
		started.wait();
		check(result.error == Error::Timeout && timer.elapsed() < 3000, "filesystem timeout kills worker");
		for (const auto& operation : {"fingerprint", "read-document", "publish"})
		{
			ResetEvent(started.handle);
			timer.restart();
			result = fixture.request({{"op", operation}, {"path", hang}, {"binary", hang}});
			started.wait();
			check(result.error == Error::Timeout && timer.elapsed() < 3000,
				"fingerprint/cache operations have bounded deadlines");
		}
		result = fixture.request(pe(crash));
		check(result.error == Error::ProcessCrashed && result.nativeCode == 0xe0000065, "filesystem crash classified");
		check(fixture.request(pe(args[1])).error == Error::None, "fresh worker recovers after fault");
		ResetEvent(started.handle);
		std::stop_source stop;
		CatalogIo cancellable(args[2], 30000, stop.get_token());
		auto task = std::async(std::launch::async, [&] { return cancellable.request(pe(hang)); });
		started.wait();
		timer.restart();
		stop.request_stop();
		check(
			task.wait_for(std::chrono::seconds(2)) == std::future_status::ready, "cancellation wakes blocked request");
		check(task.get().error != Error::None && timer.elapsed() < 2000, "cancel returns before deadline");

		const auto good = directory.path() + "/good";
		check(QDir().mkpath(good) && QFile::copy(args[4], good + "/Factory.vst3"), "factory module copy");
		PluginCatalog::Configuration configuration;
		configuration.vst3Helper64 = args[3];
		configuration.ioHelper = args[2];
		configuration.cacheFile = directory.path() + "/catalog.json";
		configuration.hostVersion = "io-test-1";
		PluginCatalog catalog(configuration);
		const auto report = catalog.scan(
			{{hang, {"vst3"}}, {crash, {"vst3"}}, {bad, {"vst3"}}, {good, {"vst3"}}}, {false, 2000, 500});
		check(report.entries.size() == 2 && report.failures.size() == 3,
			"later root scanned after timeout, crash and invalid reply");
		check(report.failures[0].error == Error::Timeout && report.failures[1].error == Error::ProcessCrashed
				&& report.failures[2].error == Error::InvalidMessage,
			"root failures preserve causes");
		CatalogJobs jobs(configuration);
		jobs.refresh({{good, {"vst3"}}}, {false, 2000, 2000});
		idle(jobs);
		const auto published = jobs.snapshot().report;
		check(published && published->entries.size() == 2, "initial report published");
		QFile cache(configuration.cacheFile);
		check(cache.open(QIODevice::ReadOnly), "read published cache");
		const auto bytes = cache.readAll();
		cache.close();
		ResetEvent(started.handle);
		jobs.refresh({{hang, {"vst3"}}}, {false, 2000, 30000});
		started.wait();
		jobs.cancel();
		idle(jobs);
		check(jobs.snapshot().report == published, "cancelled filesystem job retains published report");
		check(cache.open(QIODevice::ReadOnly) && cache.readAll() == bytes, "cancelled discovery retains cache");
		cache.close();
		jobs.refresh({{good, {"vst3"}}}, {false, 2000, 2000});
		idle(jobs);
		check(jobs.snapshot().report && jobs.snapshot().report->entries.size() == 2, "job recovers after cancellation");
		ResetEvent(started.handle);
		auto owned = std::make_unique<CatalogJobs>(configuration);
		owned->refresh({{hang, {"vst3"}}}, {false, 2000, 30000});
		started.wait();
		timer.restart();
		owned.reset();
		check(timer.elapsed() < 2000, "destruction cancels blocked filesystem worker");
		DWORD before = 0, after = 0;
		GetProcessHandleCount(GetCurrentProcess(), &before);
		for (int i = 0; i < 50; ++i)
		{
			check(fixture.request(pe(args[1])).error == Error::None, "repeated worker lifecycle");
		}
		GetProcessHandleCount(GetCurrentProcess(), &after);
		check(after <= before + 2, "worker handle count remains bounded");
		std::cout
			<< "Catalog filesystem worker: timeout, crash, cancellation, recovery, cache and handle lifecycle PASS\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
