#ifndef LMMS_VSTHOST_VST_CATALOG_SELECTION_H
#define LMMS_VSTHOST_VST_CATALOG_SELECTION_H
#include "Plugin.h"
#include "vsthost/PluginCatalog.h"

namespace lmms::vsthost {
// Discovery metadata only. Keys always name one of the existing host descriptors.
inline bool isVstInstrument(const CatalogEntry& entry)
{
	return entry.identity.format == Format::Vst3
		? entry.subcategories.split('|').contains("Instrument", Qt::CaseInsensitive)
		: entry.category == "Instrument";
}
inline Plugin::Descriptor::SubPluginFeatures::Key vstCatalogKey(
	const Plugin::Descriptor* descriptor, const CatalogEntry& entry)
{
	Plugin::Descriptor::SubPluginFeatures::Key::AttributeMap attributes;
	attributes["file"] = entry.locator.modulePath;
	attributes["binarypath"] = entry.locator.binaryPath;
	attributes["fingerprint"] = QString::fromLatin1(entry.locator.fingerprint.toHex());
	attributes["format"] = entry.identity.format == Format::Vst3 ? "vst3" : "vst2";
	if (entry.identity.format == Format::Vst3)
	{
		attributes["classid"]
			= QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(entry.identity.cid.data()), 16).toHex());
	}
	attributes["architecture"] = entry.identity.architecture == Architecture::X86 ? "32" : "64";
	attributes["identity"] = entry.identity.key();
	attributes["version"] = entry.locator.version;
	attributes["vendor"] = entry.vendor;
	if (entry.shell)
	{
		attributes["shellid"] = QString::number(entry.identity.vst2Id);
	}
	return {descriptor, entry.name, attributes};
}
}
#endif
