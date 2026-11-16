#ifndef LMMS_AGENT_CHAT_CONNECTION_H
#define LMMS_AGENT_CHAT_CONNECTION_H

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace lmms::agent::chat {

struct ChatConnection
{
	QString id;
	QString name;
	QString provider;
	QString protocol = "openai-chat";
	QString baseUrl;
	QString model;
	QString deployment;
	QString apiVersion;
	QString keyHeader = "Authorization";
	QString keyPrefix = "Bearer ";
	QString reasoning = "auto";
	double temperature = 0.7;
	QJsonObject capabilities;
	QJsonObject mapping;

	static ChatConnection create();
	static ChatConnection fromJson(const QJsonObject& object);
	QJsonObject toJson() const;
	bool validate(QString& error) const;
	static QStringList protocols();
};

// Public configuration and native credentials have deliberately separate APIs:
// no credential value is ever part of a connection's JSON representation.
class ChatConnectionStore
{
public:
	explicit ChatConnectionStore(QString directory);
	QVector<ChatConnection> connections(QString& error) const;
	bool saveConnection(const ChatConnection& connection, QString& error);
	bool removeConnection(const QString& id, QString& error);
	bool writeKey(const QString& id, const QString& key, QString& error) const;
	QString readKey(const QString& id, QString& error) const;
	bool removeKey(const QString& id, QString& error) const;

private:
	QString credentialTarget(const QString& id) const;
	bool writeConnections(const QVector<ChatConnection>& connections, QString& error) const;
	QString m_directory;
};

} // namespace lmms::agent::chat

#endif
