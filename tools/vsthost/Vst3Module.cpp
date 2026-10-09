#include "Vst3Module.h"
#include "pluginterfaces/base/funknownimpl.h"
#include <stdexcept>
#include <unordered_set>

namespace lmms::vsthost {
bool Vst3Module::open(const std::filesystem::path& path, std::string& error)
{
	if (m_module)
	{
		error = "VST3 module already loaded";
		return false;
	}
	auto binary = path;
	std::error_code filesystemError;
	if (std::filesystem::is_directory(path, filesystemError))
	{
#if defined(_WIN64)
		binary /= L"Contents/x86_64-win";
#else
		binary /= L"Contents/x86-win";
#endif
		binary /= path.filename();
	}
	// Resolve bundles with the native Unicode path before passing a UTF-8 binary
	// path to the SDK loader; narrow filesystem paths on Windows use the ANSI page.
	const auto encoded = binary.u8string();
	m_module = VST3::Hosting::Module::create({reinterpret_cast<const char*>(encoded.data()), encoded.size()}, error);
	return m_module != nullptr;
}

const VST3::Hosting::PluginFactory& Vst3Module::factory() const
{
	if (!m_module)
	{
		throw std::logic_error("VST3 module is not loaded");
	}
	return m_module->getFactory();
}

std::vector<VST3::Hosting::ClassInfo> Vst3Module::classes() const
{
	const auto& source = factory();
	const auto count = source.get()->countClasses();
	if (count < 0 || count > 4096)
	{
		throw std::runtime_error("Invalid VST3 factory class count");
	}
	// Enumerate the bounded count ourselves: SDK classInfos() calls back() even
	// when every metadata query for an index failed, and re-reads an unbounded count.
	std::vector<VST3::Hosting::ClassInfo> classes;
	classes.reserve(count);
	auto factory3 = Steinberg::U::cast<Steinberg::IPluginFactory3>(source.get());
	auto factory2 = Steinberg::U::cast<Steinberg::IPluginFactory2>(source.get());
	for (Steinberg::int32 index = 0; index < count; ++index)
	{
		Steinberg::PClassInfoW unicode{};
		Steinberg::PClassInfo2 extended{};
		Steinberg::PClassInfo basic{};
		if (factory3 && factory3->getClassInfoUnicode(index, &unicode) == Steinberg::kResultOk)
		{
			classes.emplace_back(unicode);
		}
		else if (factory2 && factory2->getClassInfo2(index, &extended) == Steinberg::kResultOk)
		{
			classes.emplace_back(extended);
		}
		else if (source.get()->getClassInfo(index, &basic) == Steinberg::kResultOk)
		{
			classes.emplace_back(basic);
		}
		else
		{
			throw std::runtime_error("Incomplete VST3 factory enumeration");
		}
	}
	std::unordered_set<std::string> identities;
	for (auto& info : classes)
	{
		if (info.vendor().empty())
		{
			info.get().vendor = source.info().vendor();
		}
		if (info.name().empty() || info.category().empty() || info.ID().toString() == std::string(32, '0'))
		{
			throw std::runtime_error("Invalid VST3 factory class metadata");
		}
		if (!identities.insert(info.ID().toString()).second)
		{
			throw std::runtime_error("Duplicate VST3 factory class identity");
		}
	}
	return classes;
}
} // namespace lmms::vsthost
