#include "vsthost/PluginCatalog.h"
#include "vsthost/HostSession.h"
#include "vsthost/Vst3Commands.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <windows.h>
#include <winioctl.h>
#include <future>
#include <iostream>
#include <algorithm>
#include <cstring>

using namespace lmms::vsthost;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "FAIL " << __LINE__ << ": " << #condition << '\n'; return 1; } } while (false)
namespace
{
bool copy(const QString& source, const QString& target)
{
	return QDir().mkpath(QFileInfo(target).absolutePath()) && QFile::copy(source, target);
}
QByteArray read(const QString& path)
{
	QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool patchVersion(const QString& path)
{
	auto bytes = read(path);
	const QString oldVersion("1.2.3"), newVersion("1.2.4");
	const QByteArray oldWide(reinterpret_cast<const char*>(oldVersion.utf16()), oldVersion.size() * 2);
	const QByteArray newWide(reinterpret_cast<const char*>(newVersion.utf16()), newVersion.size() * 2);
	if (!bytes.contains(oldWide) && !bytes.contains("1.2.3")) { return false; }
	bytes.replace("1.2.3", "1.2.4"); bytes.replace(oldWide, newWide); return write(path, bytes);
}
bool junction(const QString& path, const QString& target)
{
	if (!QDir().mkpath(path)) { return false; }
	const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_WRITE, 0, nullptr,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (handle == INVALID_HANDLE_VALUE) { return false; }
	const auto print = QDir::toNativeSeparators(target), substitute = "\\??\\" + print;
	std::vector<std::uint8_t> bytes(16 + (substitute.size() + print.size() + 2) * 2);
	put(bytes, 0, IO_REPARSE_TAG_MOUNT_POINT, 4); put(bytes, 4, bytes.size() - 8, 2);
	put(bytes, 8, 0, 2); put(bytes, 10, substitute.size() * 2, 2);
	put(bytes, 12, (substitute.size() + 1) * 2, 2); put(bytes, 14, print.size() * 2, 2);
	std::memcpy(bytes.data() + 16, substitute.utf16(), substitute.size() * 2);
	std::memcpy(bytes.data() + 16 + (substitute.size() + 1) * 2, print.utf16(), print.size() * 2);
	DWORD returned = 0;
	const bool ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, bytes.data(), static_cast<DWORD>(bytes.size()),
		nullptr, 0, &returned, nullptr); CloseHandle(handle); return ok;
}
struct JunctionCleanup
{
	QString path;
	~JunctionCleanup() { RemoveDirectoryW(reinterpret_cast<LPCWSTR>(path.utf16())); }
};
}

int wmain(int argc, wchar_t** argv)
{
	if (argc != 7) { return 2; }
	int qtArgc = 1; char name[] = "PluginCatalogTest"; char* qtArgv[]{name, nullptr}; QCoreApplication app(qtArgc, qtArgv);
	const auto helper64 = QString::fromWCharArray(argv[1]), helper32 = QString::fromWCharArray(argv[2]);
	const auto fixture64 = QString::fromWCharArray(argv[3]), fixture32 = QString::fromWCharArray(argv[4]);
	const auto plugins = QString::fromWCharArray(argv[5]), legacy = QString::fromWCharArray(argv[6]);
	QTemporaryDir directory(QDir::tempPath() + QString::fromUtf16(u"/LMMS catalog 音楽-XXXXXX")); CHECK(directory.isValid());
	const auto first = directory.path() + "/first", second = directory.path() + "/second";
	CHECK(copy(fixture64 + "/Vst3Factory.vst3", first + "/one.vst3"));
	CHECK(copy(fixture32 + "/Vst3Factory.vst3", first + "/sub/other.vst3"));
	CHECK(copy(fixture64 + "/Vst3Factory.vst3", second + "/two.vst3")); CHECK(patchVersion(second + "/two.vst3"));
	CHECK(copy(fixture64 + "/Vst3Native.vst3", first + "/Dual.vst3/Contents/x86_64-win/Dual.vst3"));
	CHECK(copy(fixture32 + "/Vst3Native.vst3", first + "/Dual.vst3/Contents/x86-win/Dual.vst3"));
	CHECK(copy(legacy + "/x64/fixtures/Release/Vst2Shell.dll", first + "/shell.dll"));
	CHECK(copy(legacy + "/x64/fixtures/Release/Vst2Baseline.dll", first + "/baseline.dll"));
	CHECK(copy(legacy + "/x86/fixtures/Release/Vst2Shell.dll", second + "/shell.dll"));
	CHECK(copy(legacy + "/x64/fixtures/Release/Vst2Shell.dll", second + "/collision.dll"));
	CHECK(QDir().mkpath(first + "/discard.BAK")); CHECK(write(first + "/discard.BAK/bad.vst3", "bad"));
	CHECK(write(first + "/ignored.vst3.BAK", "bad"));
	CHECK(QDir().mkpath(directory.path() + "/disabled")); CHECK(write(directory.path() + "/disabled/bad.dll", "bad"));
	const auto loop = first + "/sub/loop"; CHECK(junction(loop, first)); JunctionCleanup cleanup{loop};
	const auto alias = first + "/Alias.vst3"; CHECK(junction(alias, first + "/Dual.vst3")); JunctionCleanup aliasCleanup{alias};
	PluginCatalog::Configuration config{plugins + "/32/RemoteVstPlugin32.exe", plugins + "/RemoteVstPlugin64.exe",
		helper32, helper64, directory.path() + "/cache/catalog.json", "test-host-1"};
	PluginCatalog catalog(config);
	const std::vector<ScanRoot> roots{{directory.path() + "/missing"}, {first}, {first + "/sub"}, {second},
		{directory.path() + "/disabled", {"vst2", "vst3"}, true, false}, {first.toUpper()},
		{first + "/Dual.vst3/Contents/x86-win"}};
	const auto fresh = catalog.scan(roots, {false, 2000});
	for (const auto& failure : fresh.failures) { std::cerr << "failure " << failure.path.toStdString() << " " << failure.operation.toStdString() << " " << static_cast<unsigned>(failure.error) << '\n'; }
	CHECK(!fresh.cancelled && fresh.cacheError.isEmpty()); CHECK(fresh.modules == 9 && fresh.entries.size() == 17);
	CHECK(fresh.failures.size() == 1 && fresh.failures[0].path.endsWith("/missing")); CHECK(fresh.cachedModules == 0);
	CHECK(std::none_of(fresh.entries.begin(), fresh.entries.end(), [](const auto& entry) { return entry.name.contains("Controller"); }));
	CHECK(std::any_of(fresh.entries.begin(), fresh.entries.end(), [](const auto& entry) { return entry.locator.version == "1.2.4"; }));
	const auto alpha = std::find_if(fresh.entries.begin(), fresh.entries.end(), [](const auto& entry)
	{ return entry.identity.format == Format::Vst3 && entry.identity.architecture == Architecture::X64 && entry.name.contains("Alpha"); });
	CHECK(alpha != fresh.entries.end()); CHECK(fresh.candidates(alpha->identity).size() == 3);
	CHECK(std::count_if(fresh.entries.begin(), fresh.entries.end(), [](const auto& entry)
	{ return entry.identity.architecture == Architecture::X86; }) == 6);
	const auto shell = std::find_if(fresh.entries.begin(), fresh.entries.end(), [](const auto& entry)
	{ return entry.identity.format == Format::Vst2 && entry.identity.architecture == Architecture::X64 && entry.identity.vst2Id == 0xf1020304; });
	CHECK(shell != fresh.entries.end()); CHECK(fresh.candidates(shell->identity).size() == 1);
	CHECK(GetModuleHandleW(L"one.vst3") == nullptr && GetModuleHandleW(L"shell.dll") == nullptr);
	for (const auto& entry : fresh.entries)
	{
		if (entry.identity.format == Format::Vst2) { CHECK(entry.vendor == "LMMS tests" && entry.locator.version == "1"); }
	}
	const auto cached = catalog.scan(roots, {false, 2000}); CHECK(cached.cachedModules == 9 && cached.entries == fresh.entries);
	CHECK(!read(config.cacheFile).isEmpty() && read(config.cacheFile + ".failures.json").contains("records"));
	QFile changed(first + "/one.vst3"); CHECK(changed.open(QIODevice::Append)); CHECK(changed.write("X", 1) == 1); changed.close();
	const auto invalidated = catalog.scan(roots, {false, 2000}); CHECK(invalidated.cachedModules == 8 && invalidated.entries.size() == 17);
	CHECK(invalidated.entries[0].locator.fingerprint != QByteArray{});
	auto versionConfig = config; versionConfig.hostVersion = "test-host-2";
	const auto versionChanged = PluginCatalog(versionConfig).scan(roots, {false, 2000});
	CHECK(versionChanged.cachedModules == 0 && versionChanged.entries.size() == 17 && !versionChanged.cacheError.isEmpty());
	CHECK(catalog.scan(roots, {true, 2000}).entries.size() == 17);
	const auto originalCache = read(config.cacheFile); auto json = QJsonDocument::fromJson(originalCache).object();
	auto records = json["records"].toArray(); auto row = records[0].toObject(); auto entries = row["entries"].toArray();
	auto entry = entries[0].toObject(); entry["cid"] = "invalid"; entries[0] = entry; row["entries"] = entries; records[0] = row;
	json["records"] = records; CHECK(write(config.cacheFile, QJsonDocument(json).toJson(QJsonDocument::Compact)));
	const auto rejectedCache = catalog.scan(roots, {false, 2000});
	CHECK(rejectedCache.cachedModules == 0 && rejectedCache.entries.size() == 17 && !rejectedCache.cacheError.isEmpty());
	auto uncachedConfig = config; uncachedConfig.cacheFile.clear();
	const auto shallow = PluginCatalog(uncachedConfig).scan({{first, {"vst2", "vst3"}, false, true}}, {false, 2000});
	CHECK(shallow.modules == 5 && shallow.entries.size() == 9 && shallow.failures.empty());
	const auto sharp = directory.path() + QString::fromUtf16(u"/Straße");
	const auto ascii = directory.path() + "/STRASSE";
	CHECK(copy(fixture64 + "/Vst3Factory.vst3", sharp + "/one.vst3"));
	CHECK(copy(fixture64 + "/Vst3Factory.vst3", ascii + "/one.vst3"));
	const auto distinct = PluginCatalog(uncachedConfig).scan({{sharp, {"vst3"}}, {ascii, {"vst3"}}}, {false, 2000});
	CHECK(distinct.modules == 2 && distinct.entries.size() == 4 && distinct.failures.empty());
	CHECK(distinct.candidates(distinct.entries.front().identity).size() == 2);
	uncachedConfig.vst3Helper32 = helper64;
	const auto wrongHelper = PluginCatalog(uncachedConfig).scan({{first, {"vst3"}}}, {false, 2000});
	CHECK(!wrongHelper.entries.empty()); CHECK(std::count_if(wrongHelper.failures.begin(), wrongHelper.failures.end(),
		[](const auto& failure) { return failure.operation == "helper" && failure.error == Error::UnsupportedArchitecture; }) == 2);
	uncachedConfig.vst3Helper32.clear();
	const auto missingHelper = PluginCatalog(uncachedConfig).scan({{first + "/sub/other.vst3", {"vst3"}}}, {false, 2000});
	CHECK(missingHelper.entries.empty() && missingHelper.failures.size() == 1 && missingHelper.failures[0].error == Error::MissingHelper);
	const auto liveEntry = std::find_if(fresh.entries.begin(), fresh.entries.end(), [](const auto& entry)
	{ return entry.identity.architecture == Architecture::X64 && entry.name.contains("Alpha") && entry.locator.modulePath.endsWith("/Dual.vst3"); });
	CHECK(liveEntry != fresh.entries.end());
	HostSession live;
	CHECK(live.open({helper64.toStdWString(), {}, 2000}).get().error == Error::None);
	std::vector<std::uint8_t> create;
	CHECK(encodeVst3Create({liveEntry->identity.cid, 48000, 512, true,
		liveEntry->locator.modulePath.toUtf8().toStdString()}, create));
	CHECK(live.request(MessageType::Create, create, 2000).get().error == Error::None);
	const auto livePid = live.pid();
	const auto liveGeneration = live.generation();
	auto liveAudio = [&]
	{
		const auto rendered = live.renderOffline({64, 2, 2}, std::vector<float>(128, 1.0f),
			[] { return std::span<const std::uint8_t>{}; }, 2000).get();
		if (rendered.error != Error::None || rendered.payload.size() != 512 ||
			live.pid() != livePid || live.generation() != liveGeneration) { return false; }
		for (unsigned i = 0; i < 128; ++i)
		{ float sample = 0; std::memcpy(&sample, rendered.payload.data() + i * 4, 4); if (sample != 0.25f) { return false; } }
		return true;
	};
	CHECK(liveAudio());
	CHECK(copy(fixture64 + "/Vst3FactoryFault5.vst3", directory.path() + "/faults/crash.vst3"));
	CHECK(copy(fixture64 + "/Vst3Factory.vst3", directory.path() + "/faults/good.vst3"));
	CHECK(write(directory.path() + "/faults/bad.dll", "not a PE"));
	auto faultConfig = config; faultConfig.cacheFile = directory.path() + "/fault-cache.json";
	PluginCatalog faults(faultConfig); const std::vector<ScanRoot> faultRoots{{directory.path() + "/faults"}};
	const auto failed = faults.scan(faultRoots, {false, 1000}); CHECK(failed.entries.size() == 2 && failed.failures.size() == 2);
	CHECK(liveAudio());
	CHECK(std::any_of(failed.failures.begin(), failed.failures.end(), [](const auto& failure)
	{ return failure.error == Error::ProcessCrashed && failure.nativeCode == 0xe0000063; }));
	const auto isolated = faults.scan(faultRoots, {false, 1000});
	CHECK(isolated.cachedModules == 1 && isolated.entries.size() == 2 && isolated.failures.size() == 2);
	CHECK(std::count_if(isolated.failures.begin(), isolated.failures.end(), [](const auto& failure) { return failure.quarantined; }) == 1);
	const auto retried = faults.scan(faultRoots, {true, 1000});
	CHECK(retried.cachedModules == 0 && std::none_of(retried.failures.begin(), retried.failures.end(), [](const auto& failure) { return failure.quarantined; }));
	CHECK(copy(fixture64 + "/Vst3FactoryFault6.vst3", directory.path() + "/hang/hang.vst3"));
	const auto beforeCancellation = read(faultConfig.cacheFile);
	std::stop_source cancellation;
	auto pending = std::async(std::launch::async, [&]
	{ return faults.scan({{directory.path() + "/hang"}}, {true, 30000}, cancellation.get_token()); });
	CHECK(pending.wait_for(std::chrono::milliseconds(200)) == std::future_status::timeout);
	CHECK(liveAudio());
	cancellation.request_stop(); CHECK(pending.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	const auto cancelled = pending.get(); CHECK(cancelled.cancelled && cancelled.entries.empty());
	CHECK(read(faultConfig.cacheFile) == beforeCancellation);
	CHECK(liveAudio());
	auto parallelA = std::async(std::launch::async, [&] { return catalog.scan(roots, {false, 2000}); });
	auto parallelB = std::async(std::launch::async, [&] { return catalog.scan(roots, {false, 2000}); });
	CHECK(parallelA.get().entries.size() == 17 && parallelB.get().entries.size() == 17);
	CHECK(liveAudio());
	CHECK(catalog.scan(roots, {false, 2000}).cachedModules == 9);
	CHECK(catalog.scan({}, {false, 2000}).entries.empty());
	CHECK(PluginCatalog(config).scan({{first, {"vst3"}, true, false}}, {false, 2000}).modules == 0);
	CHECK(live.close().get().error == Error::None);
	std::cout << "PASS catalog roots, junction cycle, atomic bundles, dual ABI, exact identities/version candidates, cache, quarantine, cancellation and parallel scans\n";
	return 0;
}
