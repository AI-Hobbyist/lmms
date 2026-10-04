#include "vsthost/Vst3HostProxy.h"
#include "vsthost/Vst3Scanner.h"
#include "public.sdk/source/vst/hosting/module.h"
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace lmms::vsthost;

namespace
{
std::string utf8(const std::filesystem::path& path)
{
	const auto bytes = path.u8string();
	return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
std::string hex(const std::array<std::uint8_t, 16>& cid)
{
	std::ostringstream result;
	for (const auto byte : cid) { result << std::hex << std::setfill('0') << std::setw(2) << unsigned(byte); }
	return result.str();
}
void print(const Vst3Class& info)
{
	std::cout << "CLASS\t" << hex(info.cid) << '\t' << info.category << '\t' << info.name
		<< '\t' << info.version << '\t' << info.subcategories << '\n';
}
int inspect(const std::filesystem::path& path)
{
	// Independent SDK inspection runs in this test executable, never in LMMS.
	std::string error;
	const auto module = VST3::Hosting::Module::create(utf8(path), error);
	if (!module) { std::cerr << error << '\n'; return 1; }
	for (const auto& item : module->getFactory().classInfos())
	{
		Vst3Class info;
		std::memcpy(info.cid.data(), item.ID().data(), 16);
		info.category = item.category(); info.name = item.name();
		info.version = item.version(); info.subcategories = item.subCategoriesString();
		print(info);
	}
	return 0;
}
int probe(const std::wstring& helper, const std::filesystem::path& module, const std::wstring& selected)
{
	const auto scanned = scanVst3(helper, module.wstring());
	if (scanned.error != Error::None) { return 1; }
	const std::string wanted(selected.begin(), selected.end());
	const auto found = std::find_if(scanned.classes.begin(), scanned.classes.end(), [&](const auto& item) {
		return item.audioClass() && hex(item.cid) == wanted;
	});
	if (found == scanned.classes.end()) { return 2; }
	Vst3HostProxy proxy, peer;
	const Vst3Create create{found->cid, 48000, 512, true, utf8(module)};
	if (!proxy.open({helper, {}, 15000}, create) || !peer.open({helper, {}, 15000}, create))
	{
		std::cout << "PROBE_FAILED\t" << wanted << '\t' << found->name << "\terror="
			<< unsigned(proxy.error()) << ',' << unsigned(peer.error()) << '\n';
		return 3;
	}
	if (proxy.pid() == peer.pid()) { return 1; }
	const auto state = proxy.state();
	if (state.error != Error::None || peer.restoreState(state.payload) != Error::None) { return 1; }
	const auto preset = proxy.preset();
	if (preset.error != Error::None || peer.restorePreset(preset.payload) != Error::None) { return 1; }
	const auto metadata = proxy.metadata();
	const auto editable = std::find_if(metadata.parameters.begin(), metadata.parameters.end(), [](const auto& item) {
		return (item.flags & 1) && !(item.flags & (2 | 65536)) && item.steps == 0;
	});
	if (editable != metadata.parameters.end() && proxy.setParameter(editable->id, 0.25).error != Error::None) { return 1; }
	const auto midi = std::find_if(metadata.buses.begin(), metadata.buses.end(), [](const auto& bus) {
		return bus.media == 1 && bus.direction == 0 && bus.active;
	});
	if (midi != metadata.buses.end() && !proxy.postEvent({1, 0, midi->index, 0, 60, 0.5})) { return 1; }
	const AudioQueue::Layout layout{512, metadata.inputs, metadata.outputs};
	std::vector<float> input(512 * metadata.inputs, 0), output(512 * metadata.outputs, 0);
	for (unsigned period = 0; period < 8; ++period)
	{
		if (period == 7 && midi != metadata.buses.end() && !proxy.postEvent({2, 0, midi->index, 0, 60, 0})) { return 1; }
		if (!proxy.process(layout, input, output, Vst3Transport{}, true) ||
			!std::all_of(output.begin(), output.end(), [](float sample) { return std::isfinite(sample); })) { return 1; }
	}
	if (proxy.poll().error != Error::None) { return 1; }
	std::cout << "PROBE_PASS\t" << wanted << '\t' << found->name << "\tparameters=" << metadata.parameters.size()
		<< "\tinputs=" << metadata.inputs << "\toutputs=" << metadata.outputs
		<< "\tdistinct_helpers=" << proxy.pid() << ',' << peer.pid() << "\tstate_bytes=" << state.payload.size()
		<< "\tparameter_write=" << (editable != metadata.parameters.end()) << "\tmidi=" << (midi != metadata.buses.end()) << '\n';
	if (proxy.close() != Error::None || peer.close() != Error::None) { return 1; }
	return 0;
}
}

int wmain(int argc, wchar_t** argv)
{
	if (argc == 3 && std::wstring_view(argv[1]) == L"--inspect") { return inspect(argv[2]); }
	if (argc == 4 && std::wstring_view(argv[1]) == L"--scan")
	{
		const auto scanned = scanVst3(argv[2], argv[3]);
		if (scanned.error != Error::None) { std::cout << "SCAN_FAILED\terror=" << unsigned(scanned.error) << '\n'; return 1; }
		for (const auto& item : scanned.classes) { print(item); }
		if (GetModuleHandleW(std::filesystem::path(argv[3]).filename().c_str())) { return 1; }
		return scanned.classes.empty() ? 1 : 0;
	}
	if (argc == 5 && std::wstring_view(argv[1]) == L"--probe") { return probe(argv[2], argv[3], argv[4]); }
	std::cerr << "Test utility: --inspect module | --scan helper module | --probe helper module raw-cid\n";
	return 2;
}
