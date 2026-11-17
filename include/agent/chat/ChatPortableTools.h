#ifndef LMMS_AGENT_CHAT_PORTABLE_TOOLS_H
#define LMMS_AGENT_CHAT_PORTABLE_TOOLS_H

#include <QString>

namespace lmms::agent::chat {

// Asset download configuration (Penpot 07). Tool paths may be relative to the
// directory that contains lmms.exe, where tools/ and tools/manifest.json live.
struct ChatDownloadSettings
{
	bool enabled = true;
	QString aria2Path = defaultAria2Path();
	QString sevenZipPath = defaultSevenZipPath();
	bool extractArchives = true;

	static ChatDownloadSettings load();
	void save() const;

	static QString defaultAria2Path();
	static QString defaultSevenZipPath();
};

namespace ChatPortableTools {

// Resolves a configured tool path against the lmms.exe directory.
QString resolve(const QString& configured);

// Checks that both tools exist and, when tools/manifest.json lists them, that
// their SHA256 values match. The report is a short human-readable summary.
bool verify(const ChatDownloadSettings& settings, QString& report);

} // namespace ChatPortableTools

} // namespace lmms::agent::chat

#endif // LMMS_AGENT_CHAT_PORTABLE_TOOLS_H
