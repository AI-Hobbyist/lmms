#ifndef LMMS_AGENT_DISCOVERY_SUPPORT_H
#define LMMS_AGENT_DISCOVERY_SUPPORT_H

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <cmath>
#include <limits>

namespace lmms::agent::discovery {

constexpr int MaxPageSize = 500;
constexpr int DefaultPageSize = 100;
constexpr int MaxKeywordLength = 128;
constexpr int MaxScanDepth = 8;
constexpr int MaxScanEntries = 20000;
constexpr int MaxVisitedFiles = 200000;

struct Page
{
	int page = 0;
	int pageSize = DefaultPageSize;
};

inline QJsonObject objectSchema(QJsonObject properties = {}, QJsonArray required = {})
{
	return {{"type", "object"}, {"properties", properties}, {"required", required},
		{"additionalProperties", false}};
}

inline QJsonObject stringSchema()
{
	return {{"type", "string"}};
}

inline QJsonObject integerSchema()
{
	return {{"type", "integer"}};
}

inline QJsonObject booleanSchema()
{
	return {{"type", "boolean"}};
}

inline QJsonObject stringArraySchema()
{
	return {{"type", "array"}, {"items", stringSchema()}, {"maxItems", 128}};
}

inline QJsonObject pageProperties()
{
	return {{"page", QJsonObject{{"type", "integer"}, {"minimum", 0}}},
		{"pageSize", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", MaxPageSize}}}};
}

inline bool readPage(const QJsonObject& arguments, Page& page, QString& error)
{
	if (arguments.contains("page"))
	{
		const auto value = arguments.value("page");
		if (!value.isDouble())
		{
			error = "'page' must be an integer.";
			return false;
		}
		const double raw = value.toDouble();
		if (!std::isfinite(raw) || raw < 0 || raw > std::numeric_limits<int>::max() || std::floor(raw) != raw)
		{
			error = "'page' must be a non-negative integer.";
			return false;
		}
		page.page = static_cast<int>(raw);
	}
	if (arguments.contains("pageSize"))
	{
		const auto value = arguments.value("pageSize");
		if (!value.isDouble())
		{
			error = "'pageSize' must be an integer.";
			return false;
		}
		const double raw = value.toDouble();
		if (!std::isfinite(raw) || raw < 1 || raw > MaxPageSize || std::floor(raw) != raw)
		{
			error = QStringLiteral("'pageSize' must be between 1 and %1.").arg(MaxPageSize);
			return false;
		}
		page.pageSize = static_cast<int>(raw);
	}
	return true;
}

inline bool readKeyword(const QJsonObject& arguments, QString& keyword, QString& error)
{
	keyword.clear();
	if (!arguments.contains("keyword"))
	{
		return true;
	}
	const auto value = arguments.value("keyword");
	if (!value.isString())
	{
		error = "'keyword' must be a string.";
		return false;
	}
	keyword = value.toString().trimmed();
	if (keyword.size() > MaxKeywordLength)
	{
		error = QStringLiteral("'keyword' must not exceed %1 characters.").arg(MaxKeywordLength);
		return false;
	}
	return true;
}

inline bool isSafeExtension(const QString& extension)
{
	static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9]{1,16}$"));
	return pattern.match(extension).hasMatch();
}

inline bool readExtensions(
	const QJsonObject& arguments, const QStringList& defaults, QStringList& extensions, QString& error)
{
	extensions = defaults;
	if (!arguments.contains("extensions"))
	{
		return true;
	}
	const auto value = arguments.value("extensions");
	if (!value.isArray())
	{
		error = "'extensions' must be an array of strings.";
		return false;
	}
	QStringList requested;
	for (const auto& item : value.toArray())
	{
		if (!item.isString())
		{
			error = "'extensions' must contain only strings.";
			return false;
		}
		const auto extension = item.toString().toLower();
		if (!isSafeExtension(extension))
		{
			error = "Extension filters must contain only letters and digits (1-16 characters).";
			return false;
		}
		if (!requested.contains(extension))
		{
			requested.append(extension);
		}
	}
	if (requested.isEmpty())
	{
		error = "'extensions' must not be empty when provided.";
		return false;
	}
	extensions = requested;
	return true;
}

inline bool matchesKeyword(const QString& keyword, const QStringList& values)
{
	if (keyword.isEmpty())
	{
		return true;
	}
	for (const auto& value : values)
	{
		if (value.contains(keyword, Qt::CaseInsensitive))
		{
			return true;
		}
	}
	return false;
}

inline QString isoTime(const QDateTime& value)
{
	return value.toUTC().toString(Qt::ISODate);
}

inline QJsonObject pageFields(int total, const Page& page, int returned)
{
	return {{"page", page.page}, {"pageSize", page.pageSize}, {"total", total}, {"returned", returned}};
}

inline QJsonObject fileEntry(const QFileInfo& info, const QString& type)
{
	QJsonObject entry{{"name", info.fileName()}, {"path", info.absoluteFilePath()}, {"type", type},
		{"symlink", info.isSymLink()}, {"size", static_cast<double>(info.size())},
		{"modified", isoTime(info.lastModified())}, {"readable", info.isReadable()}};
	if (type == "file" && !info.suffix().isEmpty())
	{
		entry.insert("extension", info.suffix().toLower());
	}
	return entry;
}

} // namespace lmms::agent::discovery

#endif // LMMS_AGENT_DISCOVERY_SUPPORT_H
