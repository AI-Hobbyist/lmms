#ifndef LMMS_AGENT_CHAT_SEARCH_SETTINGS_H
#define LMMS_AGENT_CHAT_SEARCH_SETTINGS_H

#include <QObject>
#include <QString>

namespace lmms::agent::chat {

// Emits changed() after ChatSearchSettings::save(), so running searches can be
// canceled as soon as the user turns web search off.
class ChatSearchSettingsNotifier : public QObject
{
	Q_OBJECT
public:
	static ChatSearchSettingsNotifier* instance();

signals:
	void changed();
};

enum class ChatSearchBackend
{
	SearXNG,
	Brave,
	Model
};

// Web search configuration. Public values live in the LMMS config file;
// the SearXNG authentication and the Brave API key are native credentials
// stored through ChatConnectionStore under the fixed identifiers below.
struct ChatSearchSettings
{
	// Web search is opt-in: disabled unless the user enables it.
	bool enabled = false;
	ChatSearchBackend backend = ChatSearchBackend::SearXNG;
	// No public instance is prefilled; the user supplies their own instance.
	QString searxngUrl;
	QString braveUrl = defaultBraveUrl();

	static ChatSearchSettings load();
	void save() const;

	static QString defaultBraveUrl();
	static QString backendName(ChatSearchBackend backend);
	static ChatSearchBackend backendFromName(const QString& name);

	// Credential identifiers for ChatConnectionStore::writeKey/readKey.
	static QString searxngAuthId();
	static QString braveKeyId();
};

} // namespace lmms::agent::chat

#endif // LMMS_AGENT_CHAT_SEARCH_SETTINGS_H
