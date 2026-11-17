#include "agent/chat/ChatPortableTools.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ConfigManager.h"

namespace lmms::agent::chat {
namespace {

QString message(const char* source)
{ return QCoreApplication::translate("AgentChat", source); }

QString sha256(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return {}; }
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(&file);
	return QString::fromLatin1(hash.result().toHex());
}

// Returns the manifest entry hash for a tool file, or an empty string when the
// file is not listed (for example a user-selected tool outside tools/).
QString manifestHash(const QJsonObject& manifest, const QString& toolsRoot, const QString& path)
{
	for (const auto& tool : manifest.value("tools").toArray())
	{
		for (const auto& value : tool.toObject().value("files").toArray())
		{
			const auto entry = value.toObject();
			const auto listed = QDir(toolsRoot).absoluteFilePath(entry.value("path").toString());
			if (QFileInfo(listed) == QFileInfo(path)) { return entry.value("sha256").toString(); }
		}
	}
	return {};
}

bool verifyTool(const QString& name, const QString& path, QStringList& lines)
{
	if (!QFileInfo(path).isFile())
	{
		lines.append(message("%1: not found at %2").arg(name, QDir::toNativeSeparators(path)));
		return false;
	}
	// Layout tools/<tool>/<program>: the manifest is tools/manifest.json.
	const auto toolsRoot = QDir::cleanPath(QFileInfo(path).absolutePath() + "/..");
	QJsonObject manifest;
	QFile file(QDir(toolsRoot).absoluteFilePath("manifest.json"));
	if (file.open(QIODevice::ReadOnly)) { manifest = QJsonDocument::fromJson(file.readAll()).object(); }
	const auto expected = manifestHash(manifest, toolsRoot, path);
	if (expected.isEmpty())
	{
		lines.append(message("%1: found, not listed in tools/manifest.json").arg(name));
		return true;
	}
	if (sha256(path) != expected)
	{
		lines.append(message("%1: SHA256 does not match tools/manifest.json").arg(name));
		return false;
	}
	lines.append(message("%1: verified against tools/manifest.json").arg(name));
	return true;
}

} // namespace

ChatDownloadSettings ChatDownloadSettings::load()
{
	auto* config = ConfigManager::inst();
	ChatDownloadSettings settings;
	settings.enabled = config->value("agentchat", "downloads", "1") == "1";
	settings.aria2Path = config->value("agentchat", "aria2Path", defaultAria2Path());
	settings.sevenZipPath = config->value("agentchat", "sevenZipPath", defaultSevenZipPath());
	settings.extractArchives = config->value("agentchat", "extractArchives", "1") == "1";
	return settings;
}

void ChatDownloadSettings::save() const
{
	auto* config = ConfigManager::inst();
	config->setValue("agentchat", "downloads", enabled ? "1" : "0");
	config->setValue("agentchat", "aria2Path", aria2Path);
	config->setValue("agentchat", "sevenZipPath", sevenZipPath);
	config->setValue("agentchat", "extractArchives", extractArchives ? "1" : "0");
}

QString ChatDownloadSettings::defaultAria2Path()
{ return QStringLiteral("tools/aria2/aria2c.exe"); }

QString ChatDownloadSettings::defaultSevenZipPath()
{ return QStringLiteral("tools/7zip/7z.exe"); }

QString ChatPortableTools::resolve(const QString& configured)
{ return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(configured)); }

bool ChatPortableTools::verify(const ChatDownloadSettings& settings, QString& report)
{
	QStringList lines;
	const bool aria2 = verifyTool("aria2", resolve(settings.aria2Path), lines);
	const bool sevenZip = verifyTool("7-Zip", resolve(settings.sevenZipPath), lines);
	report = lines.join(QStringLiteral("；"));
	return aria2 && sevenZip;
}

} // namespace lmms::agent::chat
