#include "vsthost/Vst3Scanner.h"
#include <filesystem>
#include <iostream>

using namespace lmms::vsthost;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "FAIL " << __LINE__ << ": " << #condition << '\n'; return 1; } } while (false)

int wmain(int argc, wchar_t** argv)
{
	if (argc != 3 && argc != 4) { return 2; }
	const std::wstring helper = argv[1];
	const std::filesystem::path root = argv[2];
	const auto result = scanVst3(helper, (root / L"Vst3Factory.vst3").wstring());
	CHECK(result.error == Error::None);
	CHECK(result.classes.size() == 3);
	CHECK(result.classes[0].audioClass() && result.classes[1].audioClass() && !result.classes[2].audioClass());
	CHECK(result.classes[0].name == "LMMS Alpha \xe9\x9f\xb3\xe9\xa2\x91");
	CHECK(result.classes[0].vendor == "LMMS \xe6\xb5\x8b\xe8\xaf\x95");
	CHECK(result.classes[0].version == "1.2.3" && result.classes[1].subcategories == "Instrument|Synth");
	CHECK(result.classes[0].cid != result.classes[1].cid);
	CHECK(GetModuleHandleW(L"Vst3Factory.vst3") == nullptr);
	std::vector<std::uint8_t> encoded;
	CHECK(encodeVst3Catalog(result.classes, encoded));
	std::vector<Vst3Class> decoded;
	CHECK(decodeVst3Catalog(encoded, decoded));
	CHECK(decoded[0].cid == result.classes[0].cid && decoded[1].cid == result.classes[1].cid);
	for (std::size_t size = 0; size < encoded.size(); ++size)
	{ CHECK(!decodeVst3Catalog(std::span(encoded).first(size), decoded)); CHECK(decoded.empty()); }
	auto oversized = encoded; put(oversized, 4, 4097, 4);
	CHECK(!decodeVst3Catalog(oversized, decoded));
	auto trailing = encoded; trailing.push_back(0);
	CHECK(!decodeVst3Catalog(trailing, decoded));
	auto duplicate = result.classes; duplicate[1].cid = duplicate[0].cid;
	CHECK(encodeVst3Catalog(duplicate, encoded));
	CHECK(!decodeVst3Catalog(encoded, decoded));

	const auto unicodeRoot = root / L"\u6d4b\u8bd5 bundle";
	std::filesystem::create_directories(unicodeRoot);
	struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{unicodeRoot};
	const auto bundle = unicodeRoot / L"\u97f3\u9891.vst3";
	const bool x86 = argc == 4 ? std::wstring_view(argv[3]) == L"x86" : sizeof(void*) == 4;
	const auto binaryRoot = bundle / (x86 ? L"Contents/x86-win" : L"Contents/x86_64-win");
	std::filesystem::create_directories(binaryRoot);
	std::filesystem::copy_file(root / L"Vst3Factory.vst3", binaryRoot / bundle.filename(), std::filesystem::copy_options::overwrite_existing);
	const auto bundled = scanVst3(helper, bundle.wstring());
	CHECK(bundled.error == Error::None && bundled.classes.size() == 3);
	CHECK(bundled.classes[0].cid == result.classes[0].cid);
	for (unsigned fault = 1; fault <= 8; ++fault)
	{
		const auto start = GetTickCount64();
		const auto failed = scanVst3(helper, (root / (L"Vst3FactoryFault" + std::to_wstring(fault) + L".vst3")).wstring(), 1000);
		std::cout << "fault=" << fault << " error=" << static_cast<unsigned>(failed.error)
			<< " native=" << failed.fault.nativeCode << " stage=" << static_cast<unsigned>(failed.fault.stage) << '\n';
		CHECK(failed.error != Error::None && failed.classes.empty());
		CHECK(GetTickCount64() - start < 5000);
		if (fault == 5) { CHECK(failed.error == Error::ProcessCrashed); CHECK(failed.fault.nativeCode == 0xe0000063); }
		if (fault == 6) { CHECK(failed.error == Error::Timeout); CHECK(failed.fault.stage == ProcessSupervisor::Stage::Scan); }
		if (fault == 7) { CHECK(failed.error == Error::Timeout); CHECK(failed.fault.stage == ProcessSupervisor::Stage::Shutdown); }
		if (fault == 8) { CHECK(failed.error == Error::ProcessCrashed); CHECK(failed.fault.nativeCode == 0xe0000064); }
		const auto recovered = scanVst3(helper, (root / L"Vst3Factory.vst3").wstring());
		CHECK(recovered.error == Error::None && recovered.classes.size() == 3);
	}
	CHECK(scanVst3(helper, (root / L"absent.vst3").wstring()).error == Error::LoadFailed);
	CHECK(scanVst3(helper, L"").error == Error::InvalidMessage);
	DWORD handlesBefore = 0; CHECK(GetProcessHandleCount(GetCurrentProcess(), &handlesBefore));
	for (const auto fault : {6, 7})
	{
		std::stop_source cancellation;
		auto pending = std::async(std::launch::async, [&]
		{
			return scanVst3(helper, (root / (L"Vst3FactoryFault" + std::to_wstring(fault) + L".vst3")).wstring(),
				30000, cancellation.get_token());
		});
		CHECK(pending.wait_for(std::chrono::milliseconds(200)) == std::future_status::timeout);
		const auto start = GetTickCount64(); cancellation.request_stop();
		CHECK(pending.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
		const auto cancelled = pending.get();
		CHECK(cancelled.cancelled && cancelled.error != Error::None && cancelled.classes.empty());
		CHECK(cancelled.error != Error::Timeout);
		CHECK(cancelled.fault.stage == (fault == 6 ? ProcessSupervisor::Stage::Scan : ProcessSupervisor::Stage::Shutdown));
		CHECK(GetTickCount64() - start < 2000);
		const auto recovered = scanVst3(helper, (root / L"Vst3Factory.vst3").wstring());
		CHECK(recovered.error == Error::None && recovered.classes.size() == 3);
	}
	DWORD handlesAfter = 0; CHECK(GetProcessHandleCount(GetCurrentProcess(), &handlesAfter));
	CHECK(handlesAfter <= handlesBefore + 4);
	std::stop_source alreadyCancelled; alreadyCancelled.request_stop();
	const auto rejected = scanVst3(helper, (root / L"Vst3Factory.vst3").wstring(), 30000, alreadyCancelled.get_token());
	CHECK(rejected.cancelled && rejected.error != Error::None && rejected.classes.empty());
	std::cout << "PASS VST3 factory, raw CIDs, Unicode bundle, malformed catalog, supervised faults and recovery\n";
	return 0;
}
