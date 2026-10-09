#ifndef LMMS_VSTHOST_VST3_MODULE_H
#define LMMS_VSTHOST_VST3_MODULE_H

#include "public.sdk/source/vst/hosting/module.h"
#include <filesystem>

namespace lmms::vsthost {
// Native helper-only ownership. This module and every object created from its
// factory stay in the supervised process; the DAW receives serialized metadata.
class Vst3Module
{
public:
	bool open(const std::filesystem::path& path, std::string& error);
	std::vector<VST3::Hosting::ClassInfo> classes() const;
	const VST3::Hosting::PluginFactory& factory() const;

private:
	VST3::Hosting::Module::Ptr m_module;
};
} // namespace lmms::vsthost
#endif
