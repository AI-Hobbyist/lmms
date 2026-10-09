#ifndef LMMS_VSTHOST_VST3_SELECTION_H
#define LMMS_VSTHOST_VST3_SELECTION_H
#include <QCoreApplication>

#include "vsthost/CatalogIo.h"
#include "vsthost/PluginCatalog.h"
#include "vsthost/Vst3Scanner.h"

namespace lmms::vsthost {
// Control-owner preflight. All filesystem operations and optional factory
// enumeration run in disposable, bounded workers, before creating an instance.
inline bool validateVst3Selection(
	const CatalogIo& io, const QString& helper, const QString& path, const CatalogEntry& entry, QString& error)
{
	auto fail = [&](const QString& reason) {
		error = reason;
		return false;
	};
	if (entry.identity.format != Format::Vst3
		|| (entry.identity.architecture != Architecture::X86 && entry.identity.architecture != Architecture::X64)
		|| scanPathKey(path) != scanPathKey(entry.locator.modulePath))
	{
		return fail(QCoreApplication::translate(
			"VstHostUI", "Module path or architecture differs from the selected VST3 entry"));
	}
	QByteArray roots;
	QString rootError;
	if (!encodeScanRoots({{path, {"vst3"}, false, true}}, roots, rootError))
	{
		return fail(rootError);
	}
	const auto discovered = io.request({{"op", "discover"}, {"roots", QJsonDocument::fromJson(roots).object()}});
	if (discovered.error != Error::None || !discovered.object.value("modules").isArray())
	{
		return fail(QCoreApplication::translate("VstHostUI", "Cannot verify the selected VST3 module"));
	}
	QString module, binary;
	for (const auto& value : discovered.object.value("modules").toArray())
	{
		const auto row = value.toObject();
		if (row.value("format").toInt() != static_cast<int>(Format::Vst3)
			|| row.value("architecture").toInt() != static_cast<int>(entry.identity.architecture))
		{
			continue;
		}
		if (!binary.isEmpty())
		{
			return fail(QCoreApplication::translate("VstHostUI", "The selected VST3 module is ambiguous"));
		}
		module = row.value("path").toString();
		binary = row.value("binary").toString();
	}
	if (binary.isEmpty())
	{
		return fail(QCoreApplication::translate("VstHostUI", "No VST3 binary matches the selected architecture"));
	}
	if (!entry.locator.binaryPath.isEmpty() && scanPathKey(binary) != scanPathKey(entry.locator.binaryPath))
	{
		return fail(QCoreApplication::translate("VstHostUI", "The selected VST3 binary path has changed"));
	}
	if (!entry.locator.fingerprint.isEmpty())
	{
		const auto hashed = io.request({{"op", "fingerprint"}, {"path", module}, {"binary", binary}});
		const auto encoded = hashed.object.value("fingerprint").toString().toLatin1();
		if (hashed.error != Error::None || entry.locator.fingerprint.size() != 32 || encoded.size() != 64
			|| QByteArray::fromHex(encoded) != entry.locator.fingerprint)
		{
			return fail(QCoreApplication::translate(
				"VstHostUI", "The selected VST3 module or bundle resources have changed; rescan before loading"));
		}
	}
	if (!entry.locator.version.isEmpty())
	{
		const auto scanned = scanVst3(helper.toStdWString(), path.toStdWString(), 15000);
		if (scanned.error != Error::None)
		{
			return fail(QCoreApplication::translate("VstHostUI", "Cannot verify the selected VST3 class version"));
		}
		const auto found = std::find_if(scanned.classes.begin(), scanned.classes.end(),
			[&](const auto& info) { return info.cid == entry.identity.cid && info.audioClass(); });
		if (found == scanned.classes.end() || QString::fromUtf8(found->version) != entry.locator.version)
		{
			return fail(QCoreApplication::translate(
				"VstHostUI", "The selected VST3 class version has changed; rescan before loading"));
		}
	}
	error.clear();
	return true;
}
} // namespace lmms::vsthost
#endif
