#ifndef LMMS_VSTHOST_SCAN_ROOTS_H
#define LMMS_VSTHOST_SCAN_ROOTS_H

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <vector>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace lmms::vsthost
{
struct ScanRoot
{
	QString path;
	QStringList formats{"vst2", "vst3"};
	bool recursive = true;
	bool enabled = true;
	bool operator==(const ScanRoot&) const = default;
};

inline QString scanPathKey(const QString& path)
{
	auto key = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
	// Keep only mappings that Windows ordinal file-name comparison accepts.
	// Linguistic expansions and supplementary-letter mappings can differ.
	const auto source = reinterpret_cast<LPCWSTR>(key.utf16());
	const auto length = static_cast<int>(key.size());
	const auto size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE,
		source, length, nullptr, 0, nullptr, nullptr, 0);
	if (size > 0)
	{
		QString mapped(size, Qt::Uninitialized);
		if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, source, length,
			reinterpret_cast<LPWSTR>(mapped.data()), size, nullptr, nullptr, 0) == size &&
			CompareStringOrdinal(source, length, reinterpret_cast<LPCWSTR>(mapped.utf16()), size, TRUE) == CSTR_EQUAL)
		{ return mapped; }
	}
	// Ordinal comparison operates on UTF-16 code units. A one-unit destination
	// rejects expansions; verification retains unsupported mappings verbatim.
	for (qsizetype i = 0; i < key.size(); ++i)
	{
		const WCHAR original = key.at(i).unicode(); WCHAR mapped = original;
		if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, &original, 1,
			&mapped, 1, nullptr, nullptr, 0) == 1 &&
			CompareStringOrdinal(&original, 1, &mapped, 1, TRUE) == CSTR_EQUAL) { key[i] = QChar(mapped); }
	}
#endif
	return key;
}

// Normalize syntax without accessing a possibly unavailable network directory.
// Repeated paths keep their first entry's options and position.
inline bool normalizeScanRoots(const std::vector<ScanRoot>& source, std::vector<ScanRoot>& roots, QString& error)
{
	roots.clear(); error.clear();
	auto fail = [&](const QString& message) { roots.clear(); error = message; return false; };
	if (source.size() > 256) { return fail("Too many VST scan roots (maximum 256)"); }
	QSet<QString> seen;
	for (auto root : source)
	{
		root.path = QDir::cleanPath(QDir::fromNativeSeparators(root.path));
		if (root.path.isEmpty() || root.path.size() > 32767 || root.path.contains(QChar(0)) ||
			!QDir::isAbsolutePath(root.path)) { return fail("VST scan roots must be absolute paths without NUL characters"); }
		if (root.formats.isEmpty() || root.formats.size() > 2) { return fail("Invalid VST scan format selection"); }
		QSet<QString> formats;
		for (const auto& format : root.formats)
		{
			if ((format != "vst2" && format != "vst3") || formats.contains(format))
			{ return fail("VST scan formats must be unique vst2/vst3 entries"); }
			formats.insert(format);
		}
		const auto key = scanPathKey(root.path);
		if (!seen.contains(key)) { seen.insert(key); roots.push_back(std::move(root)); }
	}
	return true;
}

inline bool decodeScanRoots(const QByteArray& json, std::vector<ScanRoot>& roots, QString& error)
{
	roots.clear(); error.clear();
	if (json.size() > 1024 * 1024) { error = "VST scan roots exceed configuration size limit"; return false; }
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(json, &parse);
	if (parse.error != QJsonParseError::NoError || !document.isObject())
	{ error = "Invalid VST scan roots JSON"; return false; }
	const auto object = document.object();
	if (!object.value("version").isDouble() || object.value("version").toDouble() != 1 ||
		!object.value("roots").isArray() || object.value("roots").toArray().size() > 256)
	{ error = "Unsupported VST scan roots schema"; return false; }
	std::vector<ScanRoot> parsed;
	for (const auto& item : object.value("roots").toArray())
	{
		if (!item.isObject()) { error = "Invalid VST scan root entry"; return false; }
		const auto entry = item.toObject();
		if (!entry.value("path").isString() || !entry.value("formats").isArray() ||
			!entry.value("recursive").isBool() || !entry.value("enabled").isBool())
		{ error = "Invalid VST scan root fields"; return false; }
		ScanRoot root{entry.value("path").toString(), {}, entry.value("recursive").toBool(), entry.value("enabled").toBool()};
		for (const auto& format : entry.value("formats").toArray())
		{
			if (!format.isString()) { error = "Invalid VST scan root format"; return false; }
			root.formats.append(format.toString());
		}
		parsed.push_back(std::move(root));
	}
	return normalizeScanRoots(parsed, roots, error);
}

inline bool encodeScanRoots(const std::vector<ScanRoot>& roots, QByteArray& json, QString& error)
{
	json.clear(); std::vector<ScanRoot> normalized;
	if (!normalizeScanRoots(roots, normalized, error)) { return false; }
	QJsonArray entries;
	for (const auto& root : normalized)
	{
		QJsonArray formats; for (const auto& format : root.formats) { formats.append(format); }
		entries.append(QJsonObject{{"path", root.path}, {"formats", formats},
			{"recursive", root.recursive}, {"enabled", root.enabled}});
	}
	json = QJsonDocument(QJsonObject{{"version", 1}, {"roots", entries}}).toJson(QJsonDocument::Compact);
	if (json.size() > 1024 * 1024) { json.clear(); error = "VST scan roots exceed configuration size limit"; return false; }
	return true;
}

inline std::vector<ScanRoot> migrateScanRoots(const QString& legacyRoot, const QStringList& standardRoots)
{
	std::vector<ScanRoot> initial;
	if (!legacyRoot.isEmpty()) { initial.push_back({QDir(legacyRoot).absolutePath()}); }
	for (const auto& path : standardRoots)
	{
		if (!path.isEmpty() && QDir(path).exists()) { initial.push_back({QDir(path).absolutePath(), {"vst3"}}); }
	}
	std::vector<ScanRoot> normalized; QString error;
	if (!normalizeScanRoots(initial, normalized, error)) { return {}; }
	return normalized;
}
} // namespace lmms::vsthost
#endif
