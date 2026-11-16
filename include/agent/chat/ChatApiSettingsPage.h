#ifndef LMMS_AGENT_CHAT_API_SETTINGS_PAGE_H
#define LMMS_AGENT_CHAT_API_SETTINGS_PAGE_H

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include "agent/chat/ChatConnection.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

namespace lmms::agent::chat {

class ChatApiSettingsPage : public QWidget
{
	Q_OBJECT
public:
	explicit ChatApiSettingsPage(ChatConnectionStore& store, QWidget* parent = nullptr, const QString& selectedId = {});
	bool save(QString& error);
	QString selectedConnectionId() const;
	ChatConnection connectionDraft();
	void setGenerationDraft(const ChatConnection& connection);

private:
	void buildConnections();
	void buildPresets();
	void addConnection(const QString& provider, const QString& name, const QString& url, const QString& protocol);
	void refreshConnections();
	void captureFields();
	void selectConnection(const QString& id);
	void refreshCurrentFields();
	void editAdvanced();
	void connectionMenu(const QPoint& position);
	void fetchModels();
	void requestModels(const QUrl& url, const ChatConnection& connection, const QString& key, QStringList models,
					   QSet<QString> cursors);
	ChatConnection* currentConnection();

	ChatConnectionStore& m_store;
	QVector<ChatConnection> m_connections;
	QHash<QString, QString> m_keys;
	QSet<QString> m_removed;
	QString m_currentId;
	QString m_loadError;
	QWidget* m_editor = nullptr;
	QWidget* m_presets = nullptr;
	QListWidget* m_list = nullptr;
	QLineEdit* m_search = nullptr;
	QLineEdit* m_name = nullptr;
	QLineEdit* m_url = nullptr;
	QLineEdit* m_key = nullptr;
	QComboBox* m_protocol = nullptr;
	QComboBox* m_model = nullptr;
	QLabel* m_title = nullptr;
	QLabel* m_status = nullptr;
	QLabel* m_preview = nullptr;
	QNetworkAccessManager* m_network = nullptr;
	QPointer<QNetworkReply> m_reply;
};

} // namespace lmms::agent::chat

#endif
