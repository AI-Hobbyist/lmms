#include "agent/chat/ChatController.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

#include "agent/chat/ChatContent.h"
#include "agent/chat/ChatProviderClient.h"

namespace lmms::agent::chat {
namespace {

QString newId()
{ return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString message(const char* source)
{ return QCoreApplication::translate("AgentChat", source); }

} // namespace

ChatController::ChatController(ChatStore& store, QObject* parent)
	: QObject(parent)
	, m_store(store)
{
	m_client = new ChatProviderClient(this);
	connect(m_client, &ChatProviderClient::uploadProgress, this,
			[this](qint64 sent, qint64 total) { emit attachmentStatus(m_sessionId, "uploading", sent, total); });
	connect(m_client, &ChatProviderClient::updated, this, [this](const QString& text, const QString& summary) {
		m_message.insert("text", text);
		m_message.insert("summary", summary);
		emit messageUpdated(m_sessionId, m_message);
	});
	connect(m_client, &ChatProviderClient::finished, this, [this] {
		const auto response = m_client->response();
		m_message.insert("text", response.text);
		m_message.insert("summary", response.summary);
		m_message.insert("finishReason", response.finishReason);
		m_message.insert("usage", response.usage);
		m_message.insert("error", response.error);
		m_message.insert("tool_calls", response.toolCalls);
		if (response.complete && !response.nativeContent.isEmpty())
		{
			m_message.insert("native", QJsonObject{{"origin", m_nativeOrigin}, {"content", response.nativeContent}});
		}
		finish(response.complete ? "complete" : "error");
	});
}

ChatController::~ChatController()
{
	// Owners must keep the injected store alive until this controller is gone.
	stop();
}

bool ChatController::active() const
{ return m_preparing || m_client->active(); }

QString ChatController::activeSessionId() const
{ return active() ? m_sessionId : QString{}; }

QJsonObject ChatController::currentMessage() const
{ return m_message; }

bool ChatController::send(const QString& sessionId, const ChatConnection& connection, const QString& key,
						  const QString& systemPrompt, const QString& text, QString& error,
						  const QVector<ChatAttachment>& attachments)
{ return startTurn(sessionId, connection, key, systemPrompt, text, error, attachments, false); }

bool ChatController::retry(const QString& sessionId, const ChatConnection& connection, const QString& key,
						   const QString& systemPrompt, QString& error)
{ return startTurn(sessionId, connection, key, systemPrompt, {}, error, {}, true); }

bool ChatController::startTurn(const QString& sessionId, const ChatConnection& connection, const QString& key,
							   const QString& systemPrompt, const QString& text, QString& error,
							   const QVector<ChatAttachment>& attachments, bool retry)
{
	error.clear();
	if (active())
	{
		error = message("A model request is already running.");
		return false;
	}
	auto session = m_store.session(sessionId);
	if (session.isEmpty() || (!retry && text.trimmed().isEmpty() && attachments.isEmpty()))
	{
		error = message("Select a valid session and enter a message.");
		return false;
	}
	qint64 attachmentBytes = 0;
	QJsonArray references;
	for (const auto& attachment : attachments)
	{
		if (!supportsChatAttachment(connection, attachment, error))
		{
			return false;
		}
		attachmentBytes += attachment.size;
		references.append(attachment.toJson());
	}
	if (attachmentBytes > 64 * 1024 * 1024)
	{
		error = message("Selected attachments exceed the 64 MiB request limit.");
		return false;
	}
	QJsonArray history;
	auto stored = session.value("messages").toArray();
	QJsonObject retryUser;
	QString retryOf;
	if (retry)
	{
		const auto last = stored.isEmpty() ? QJsonObject{} : stored.last().toObject();
		if (last.value("role") == "assistant" && last.value("status") == "error")
		{
			retryOf = last.value("id").toString();
			for (int index = stored.size() - 2; index >= 0; --index)
			{
				const auto item = stored[index].toObject();
				if (item.value("role") == "user")
				{
					retryUser = item;
					break;
				}
				if (item.value("role") != "assistant" || item.value("status") != "error")
				{
					break;
				}
			}
		}
		if (retryUser.isEmpty())
		{
			error = message("There is no failed request to retry in this session.");
			return false;
		}
		if (!retryUser.value("attachments").toArray().isEmpty() && !retryUser.value("content").isArray())
		{
			error = message("Attachment content is unavailable. Select the files again to send a new request.");
			return false;
		}
	}
	for (const auto& value : stored)
	{
		const auto item = value.toObject();
		const auto role = item.value("role").toString();
		if (role == "tool")
		{
			history.append(
				QJsonObject{{"role", "tool"},
							{"tool_call_id", item.value("tool_call_id")},
							{"name", item.value("name")},
							{"is_error", item.value("is_error").toBool()},
							{"content", item.contains("content") ? item.value("content") : item.value("text")}});
			continue;
		}
		if (role == "user" || (role == "assistant" && item.value("status").toString("complete") == "complete"))
		{
			QJsonValue content = item.value("content");
			if (!content.isArray())
			{
				auto text = item.value("text").toString();
				for (const auto& reference : item.value("attachments").toArray())
				{
					// Legacy/imported metadata never authorizes reading a local path.
					text += "\n"
						+ message("Previous attachment: %1 (content unavailable; select it again to send).")
							  .arg(reference.toObject().value("name").toString());
				}
				content = text;
			}
			QJsonObject message{{"role", role}, {"content", content}};
			if (role == "assistant" && !item.value("tool_calls").toArray().isEmpty())
			{
				message.insert("tool_calls", item.value("tool_calls"));
			}
			const auto native = item.value("native").toObject();
			if (role == "assistant" && native.value("origin").toObject() == chatNativeOrigin(connection))
			{
				message.insert("native", native);
			}
			history.append(message);
		}
	}
	if (!retry)
	{
		history.append(QJsonObject{{"role", "user"}, {"content", text}});
	}
	if (!validateChatToolHistory(history, error))
	{
		return false;
	}
	m_sessionId = sessionId;
	m_nativeOrigin = chatNativeOrigin(connection);
	m_turnId = newId();
	QJsonObject user{{"id", newId()}, {"turnId", m_turnId}, {"role", "user"}, {"text", text}, {"status", "complete"}};
	if (!references.isEmpty())
	{
		user.insert("attachments", references);
	}
	m_message = {{"id", newId()}, {"turnId", m_turnId}, {"role", "assistant"}, {"text", ""}, {"status", "streaming"}};
	if (retry)
	{
		m_message.insert("retryOf", retryOf);
	}
	if (stored.isEmpty())
	{
		session.insert("title", text.trimmed().isEmpty() ? attachments.first().name : text.simplified().left(30));
	}
	if (!retry)
	{
		stored.append(user);
		session.insert("draft", "");
		session.insert("draftAttachments", QJsonArray{});
	}
	stored.append(m_message);
	session.insert("messages", stored);
	if (!m_store.saveSession(session, error))
	{
		return false;
	}
	if (!attachments.isEmpty())
	{
		m_preparing = true;
		m_preparationCanceled = std::make_shared<std::atomic_bool>(false);
		const auto canceled = m_preparationCanceled;
		const auto turn = m_turnId;
		const auto userId = user.value("id").toString();
		const QPointer<ChatController> controller(this);
		QThreadPool::globalInstance()->start([controller, canceled, turn, userId, connection, key, systemPrompt,
											  history, attachments]() mutable {
			QJsonArray parts;
			parts.append(QJsonObject{{"type", "text"}, {"text", history.last().toObject().value("content")}});
			QString error;
			for (const auto& attachment : attachments)
			{
				if (canceled->load())
				{
					return;
				}
				QJsonObject part;
				if (!prepareChatAttachment(connection, attachment, part, error))
				{
					break;
				}
				parts.append(part);
			}
			history[history.size() - 1] = QJsonObject{{"role", "user"}, {"content", parts}};
			QMetaObject::invokeMethod(
				QCoreApplication::instance(),
				[controller, canceled, turn, userId, connection, key, systemPrompt, history, parts, error]() mutable {
					if (!controller || canceled->load() || controller->m_turnId != turn || !controller->m_preparing)
					{
						return;
					}
					controller->m_preparing = false;
					if (!error.isEmpty() || !controller->savePreparedContent(userId, parts, error)
						|| !controller->m_client->start(connection, key, systemPrompt, history, error))
					{
						controller->m_message.insert("error", error);
						controller->finish("error");
					}
				},
				Qt::QueuedConnection);
		});
		emit sessionChanged(m_sessionId);
		emit stateChanged();
		emit attachmentStatus(m_sessionId, "preparing", 0, -1);
		return true;
	}
	if (!m_client->start(connection, key, systemPrompt, history, error))
	{
		m_message.insert("error", error);
		finish("error");
		return false;
	}
	emit sessionChanged(m_sessionId);
	emit stateChanged();
	return true;
}

bool ChatController::savePreparedContent(const QString& userId, const QJsonArray& content, QString& error)
{
	auto session = m_store.session(m_sessionId);
	auto messages = session.value("messages").toArray();
	int userIndex = -1;
	bool assistantExists = false;
	for (int index = 0; index < messages.size(); ++index)
	{
		const auto item = messages[index].toObject();
		if (item.value("turnId").toString() != m_turnId)
		{
			continue;
		}
		if (item.value("id").toString() == userId)
		{
			userIndex = index;
		}
		assistantExists = assistantExists || item.value("id") == m_message.value("id");
	}
	if (userIndex < 0 || !assistantExists)
	{
		error = message("The request's original session or message no longer exists.");
		return false;
	}
	// Keep the exact bytes prepared for this turn, not a path to mutable originals.
	auto user = messages[userIndex].toObject();
	user.insert("content", content);
	messages[userIndex] = user;
	session.insert("messages", messages);
	return m_store.saveSession(session, error);
}

bool ChatController::saveResult(QString& error)
{
	// Read the current record so a new draft, rename or archive action performed
	// while streaming is preserved. Never resurrect a deleted session/message.
	auto session = m_store.session(m_sessionId);
	auto messages = session.value("messages").toArray();
	for (int index = 0; index < messages.size(); ++index)
	{
		const auto item = messages[index].toObject();
		if (item.value("id") == m_message.value("id") && item.value("turnId").toString() == m_turnId)
		{
			messages[index] = m_message;
			session.insert("messages", messages);
			return m_store.saveSession(session, error);
		}
	}
	error = message("The request's original session or message no longer exists.");
	return false;
}

void ChatController::finish(const QString& status)
{
	m_message.insert("status", status);
	QString error;
	if (!saveResult(error))
	{
		m_message.insert("error", error);
	}
	const auto id = m_sessionId;
	const auto resultError = m_message.value("error").toString();
	emit sessionChanged(id);
	emit stateChanged();
	if (!resultError.isEmpty())
	{
		emit requestFailed(id, resultError);
	}
}

void ChatController::stop()
{
	if (!active())
	{
		return;
	}
	if (m_preparing)
	{
		m_preparationCanceled->store(true);
		m_preparing = false;
		finish("canceled");
		return;
	}
	const auto response = m_client->response();
	m_client->cancel();
	m_message.insert("text", response.text);
	m_message.insert("summary", response.summary);
	finish("canceled");
}

} // namespace lmms::agent::chat
