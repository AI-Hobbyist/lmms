#ifndef LMMS_NATIVE_VST_SELECTION_H
#define LMMS_NATIVE_VST_SELECTION_H

#include <QRegularExpression>
#include <cstring>
#include <memory>

#include "Engine.h"
#include "GuiApplication.h"
#include "VstPlugin.h"
#include "vsthost/CatalogJobs.h"
#include "vsthost/CatalogLocatorCodec.h"
#include "vsthost/VstCatalogSelection.h"

namespace lmms::vsthost {

using NativeVstKey = Plugin::Descriptor::SubPluginFeatures::Key;

class NativeVstFeatures final : public Plugin::Descriptor::SubPluginFeatures
{
public:
	explicit NativeVstFeatures(Plugin::Type type)
		: SubPluginFeatures(type)
		, m_instrument(type == Plugin::Type::Instrument)
	{
	}

	void listSubPluginKeys(const Plugin::Descriptor* descriptor, KeyList& keys) const override
	{
		const auto* catalog = Engine::vstCatalog();
		const auto report = catalog ? catalog->snapshot().report : nullptr;
		if (!report) { return; }
		for (const auto& entry : report->entries)
		{
			if (isVstInstrument(entry) == m_instrument)
			{
				auto key = vstCatalogKey(descriptor, entry);
				key.attributes["pluginname"] = entry.name;
				keys.push_back(key);
			}
		}
	}

private:
	bool m_instrument;
};

inline std::unique_ptr<VstPlugin> openNativeVst(const NativeVstKey& key)
{
	const auto& attributes = key.attributes;
	const auto path = attributes.value("file");
	const auto format = attributes.value("format");
	const auto architecture = attributes.value("architecture");
	if (path.isEmpty() || (format != "vst2" && format != "vst3") || (architecture != "32" && architecture != "64"))
	{
		return {};
	}

	bool validShellId = true;
	const auto shellId = attributes.contains("shellid") ? attributes.value("shellid").toUInt(&validShellId) : 0;
	if (!validShellId || (attributes.contains("shellid") && (!shellId || format == "vst3"))) { return {}; }

	CatalogEntry entry;
	if (format == "vst3")
	{
		static const QRegularExpression cidPattern("^[0-9A-Fa-f]{32}$");
		const auto cid = attributes.value("classid");
		QByteArray fingerprint;
		const auto encodedFingerprint = attributes.value("fingerprint");
		if (!cidPattern.match(cid).hasMatch()
			|| (!encodedFingerprint.isEmpty() && !decodeCatalogFingerprint(encodedFingerprint, fingerprint)))
		{
			return {};
		}
		const auto bytes = QByteArray::fromHex(cid.toLatin1());
		std::memcpy(entry.identity.cid.data(), bytes.constData(), entry.identity.cid.size());
		entry.identity.architecture = architecture == "32" ? Architecture::X86 : Architecture::X64;
		entry.locator = {path, attributes.value("binarypath"), attributes.value("version"), fingerprint};
		entry.name = attributes.value("pluginname", key.name);
		entry.vendor = attributes.value("vendor");
	}

	auto plugin = std::make_unique<VstPlugin>(
		path, shellId, gui::getGUI() ? "none" : "headless", format == "vst3" ? &entry : nullptr);
	if (plugin->failed()) { return {}; }
	return plugin;
}

} // namespace lmms::vsthost

#endif
