#include "agent/chat/ChatController.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>

#include "ConfigManager.h"
#include "agent/CommandBus.h"
#include "agent/chat/ChatContent.h"
#include "agent/chat/ChatProviderClient.h"
#include "agent/chat/ChatRequest.h"
#include "agent/chat/ChatSearchSettings.h"
#include "agent/chat/ChatToolDispatcher.h"

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
	m_dispatcher = new ChatToolDispatcher(this);
	connect(m_dispatcher, &ChatToolDispatcher::approvalRequested, this, &ChatController::approvalRequested);
	connect(m_dispatcher, &ChatToolDispatcher::invalidated, this,
			[this](const QString& session, const QString& turn, const QString& reason) {
				if (!m_toolTurn || session != m_sessionId || turn != m_turnId)
				{
					return;
				}
				if (m_dispatcher->executing())
				{
					// Preserve the actual result of a command that itself resets the project.
					m_invalidatedReason = reason;
					return;
				}
				abortTurn(reason, "canceled");
			});
	connect(m_dispatcher, &ChatToolDispatcher::toolFinished, this, [this](const QJsonObject& outcome) {
		if (!m_toolTurn || outcome.value("sessionId") != m_sessionId || outcome.value("turnId") != m_turnId
			|| m_pendingIndex >= m_pendingCalls.size()
			|| outcome.value("id") != m_pendingCalls[m_pendingIndex].toObject().value("id"))
		{
			return;
		}
		QString error;
		const QJsonObject execution{{"command", outcome.value("command")},
			{"arguments", outcome.value("arguments")},
			{"approvalRequired", outcome.value("approvalRequired")},
			{"executed", outcome.value("executed")}};
		const auto call = m_pendingCalls[m_pendingIndex].toObject();
		if (!appendToolResult(call, outcome.value("result").toObject(), error, execution))
		{
			abortTurn(error, "error");
			return;
		}
		++m_pendingIndex;
		const QPointer<ChatController> alive(this);
		emit sessionChanged(m_sessionId);
		if (!alive || !m_toolTurn)
		{
			return;
		}
		if (!m_invalidatedReason.isEmpty())
		{
			abortTurn(m_invalidatedReason, "canceled");
			return;
		}
		const auto turn = m_turnId;
		QTimer::singleShot(0, this, [this, turn] {
			if (m_toolTurn && m_turnId == turn)
			{
				dispatchNextTool();
			}
		});
	});
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
		if (!response.sources.isEmpty()) { m_message.insert("sources", response.sources); }
		if (m_toolTurn && response.complete && !response.toolCalls.isEmpty())
		{
			receiveTools(response.toolCalls);
		}
		else
		{
			finish(response.complete ? "complete" : "error");
		}
	});
	connect(ChatSearchSettingsNotifier::instance(), &ChatSearchSettingsNotifier::changed, this, [this] {
		// Turning hosted search off stops the client request; the provider may
		// already have run searches, which cannot be undone from here.
		if (m_hostedSearch == HostedSearch::Enabled && m_client->active() && !hostedSearchEligible())
		{
			abortTurn(message("Web search was turned off; the model request was stopped."), "canceled");
		}
	});
}

ChatController::~ChatController()
{
	// Owners must keep the injected store alive until this controller is gone.
	stop();
}

bool ChatController::active() const
{ return m_toolTurn || m_preparing || m_client->active() || !m_hostedApproval.isEmpty(); }

QString ChatController::activeSessionId() const
{ return active() ? m_sessionId : QString{}; }

QJsonObject ChatController::currentMessage() const
{ return m_message; }

void ChatController::setToolsEnabled(bool enabled)
{ m_toolsEnabled = enabled; }

QJsonObject ChatController::pendingApproval() const
{
	if (!m_hostedApproval.isEmpty()) { return m_hostedApproval; }
	return m_toolTurn ? m_dispatcher->pendingApproval() : QJsonObject{};
}

bool ChatController::decideTool(const QJsonObject& approval, bool approved, QString& error)
{
	if (!m_hostedApproval.isEmpty() && approval.value("token") == m_hostedApproval.value("token"))
	{
		return decideHostedSearch(approval, approved, error);
	}
	if (!m_toolTurn || !saveResult(error))
	{
		error = message("The request's original session or message no longer exists.");
		if (m_toolTurn)
		{
			abortTurn(error, "canceled");
		}
		return false;
	}
	return m_dispatcher->decide(approval.value("token").toString(), approval.value("sessionId").toString(),
								approval.value("turnId").toString(), approval.value("id").toString(),
								approval.value("arguments").toObject(), approved, error);
}

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
				// A failed continuation follows completed assistant/tool pairs.
				// Keep those results in history and retry only the model request.
				if (item.value("role") != "assistant" && item.value("role") != "tool")
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
			if (item.contains("provider_call_id"))
			{
				auto tool = history.last().toObject();
				tool.insert("provider_call_id", item.value("provider_call_id"));
				history[history.size() - 1] = tool;
			}
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
	m_connection = connection;
	m_key = key;
	m_systemPrompt = systemPrompt;
	m_pendingCalls = {};
	m_pendingIndex = 0;
	m_invalidatedReason.clear();
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
	if (m_toolsEnabled)
	{
		if (!m_dispatcher->beginTurn(m_sessionId, m_turnId, error))
		{
			m_message.insert("error", error);
			finish("error");
			return false;
		}
		m_toolTurn = true;
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
						|| !controller->startRequest(history, error))
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
	if (!startRequest(history, error))
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

bool ChatController::hostedSearchEligible() const
{
	const auto settings = ChatSearchSettings::load();
	return settings.enabled && settings.backend == ChatSearchBackend::Model && supportsHostedWebSearch(m_connection);
}

bool ChatController::startRequest(const QJsonArray& history, QString& error)
{
	m_history = history;
	if (m_hostedSearch == HostedSearch::Undecided)
	{
		const bool request = ConfigManager::inst()->value("agentchat", "approval", "request") != "none";
		if (!hostedSearchEligible()) { m_hostedSearch = HostedSearch::Disabled; }
		else if (!request) { m_hostedSearch = HostedSearch::Enabled; }
		else
		{
			// Hosted searches run on the provider's servers and cannot be approved
			// one by one: ask once for this turn before anything is sent.
			const auto model
				= m_connection.name.isEmpty() ? m_connection.model : m_connection.name + " / " + m_connection.model;
			m_hostedApproval = {{"token", QUuid::createUuid().toString(QUuid::WithoutBraces)},
				{"sessionId", m_sessionId}, {"turnId", m_turnId}, {"id", "hosted-web-search"},
				{"command", "model.webSearch"},
				{"arguments", QJsonObject{{"protocol", m_connection.protocol}, {"model", m_connection.model}}},
				{"approvalRequired", true}, {"hosted", true}, {"scope", QJsonArray{"web"}},
				{"purpose",
					message("The model provider runs web searches on its own servers during this turn; one turn may "
							"search several times. This approval applies to this turn only.")},
				{"targets", QJsonArray{QJsonObject{{"path", "web"}, {"name", model}}}}};
			const auto approval = m_hostedApproval;
			emit approvalRequested(approval);
			return true;
		}
	}
	return m_client->start(m_connection, m_key, m_systemPrompt, history, error,
		m_toolTurn ? m_dispatcher->definitions() : QJsonArray{}, m_hostedSearch == HostedSearch::Enabled);
}

bool ChatController::decideHostedSearch(const QJsonObject& approval, bool approved, QString& error)
{
	if (approval.value("sessionId") != m_sessionId || approval.value("turnId") != m_turnId)
	{
		error = message("The approval no longer matches the original tool call.");
		return false;
	}
	m_hostedApproval = {};
	// A denial still answers the message, just without hosted search. Settings
	// changed while waiting are honored: approval never re-enables search.
	m_hostedSearch = approved && hostedSearchEligible() ? HostedSearch::Enabled : HostedSearch::Disabled;
	m_message.insert("hostedSearch", m_hostedSearch == HostedSearch::Enabled);
	if (!saveResult(error) || !startRequest(m_history, error))
	{
		m_message.insert("error", error);
		finish("error");
		return false;
	}
	emit stateChanged();
	return true;
}

void ChatController::receiveTools(QJsonArray calls)
{
	for (int index = 0; index < calls.size(); ++index)
	{
		auto call = calls[index].toObject();
		if (call.contains("providerId") && call.value("providerId").toString().isEmpty())
		{
			// Gemini may omit IDs. Keep that wire-level omission while assigning a
			// fresh local ID across every response in this turn.
			call.insert("id", newId());
			calls[index] = call;
		}
	}
	m_message.insert("tool_calls", calls);
	m_message.insert("status", "complete");
	QString error;
	if (!saveResult(error))
	{
		abortTurn(error, "error");
		return;
	}
	QJsonObject assistant{{"role", "assistant"}, {"content", m_message.value("text")}, {"tool_calls", calls}};
	if (m_message.contains("native"))
	{
		assistant.insert("native", m_message.value("native"));
	}
	m_history.append(assistant);
	m_pendingCalls = calls;
	m_pendingIndex = 0;
	const auto turn = m_turnId;
	QTimer::singleShot(0, this, [this, turn] {
		if (m_toolTurn && m_turnId == turn)
		{
			dispatchNextTool();
		}
	});
	emit sessionChanged(m_sessionId);
	emit stateChanged();
}

bool ChatController::appendToolResult(const QJsonObject& call, const QJsonObject& result, QString& error,
	const QJsonObject& execution)
{
	auto session = m_store.session(m_sessionId);
	auto messages = session.value("messages").toArray();
	bool originalExists = false;
	for (const auto& value : messages)
	{
		const auto item = value.toObject();
		originalExists = originalExists
			|| (item.value("id") == m_message.value("id") && item.value("turnId").toString() == m_turnId);
	}
	if (!originalExists)
	{
		error = message("The request's original session or message no longer exists.");
		return false;
	}
	const auto text = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
	QJsonObject tool{{"role", "tool"},
					 {"tool_call_id", call.value("id")},
					 {"name", call.value("name")},
					 {"is_error", !result.value("ok").toBool()},
					 {"content", text}};
	if (call.contains("providerId"))
	{
		tool.insert("provider_call_id", call.value("providerId"));
	}
	auto stored = tool;
	stored.insert("id", newId());
	stored.insert("turnId", m_turnId);
	stored.insert("status", "complete");
	stored.insert("text", text);
	if (!execution.isEmpty())
	{
		stored.insert("execution", execution);
	}
	messages.append(stored);
	session.insert("messages", messages);
	if (!m_store.saveSession(session, error))
	{
		return false;
	}
	m_history.append(tool);
	return true;
}

void ChatController::dispatchNextTool()
{
	if (!m_toolTurn)
	{
		return;
	}
	QString error;
	// Check the original persisted message before approving/executing another
	// call. A deleted session is never recreated by this controller.
	if (!saveResult(error))
	{
		abortTurn(error, "canceled");
		return;
	}
	if (m_pendingIndex < m_pendingCalls.size())
	{
		const auto call = m_pendingCalls[m_pendingIndex].toObject();
		const auto mode = ConfigManager::inst()->value("agentchat", "approval", "request") == "none"
			? ChatApprovalMode::None
			: ChatApprovalMode::Request;
		if (!m_dispatcher->submit(call, error, mode))
		{
			const auto result = CommandResult::failure("invalid_tool_call", error).toJson();
			if (!appendToolResult(call, result, error))
			{
				abortTurn(error, "error");
				return;
			}
			++m_pendingIndex;
			const QPointer<ChatController> alive(this);
			emit sessionChanged(m_sessionId);
			if (!alive || !m_toolTurn)
			{
				return;
			}
			const auto turn = m_turnId;
			QTimer::singleShot(0, this, [this, turn] {
				if (m_toolTurn && m_turnId == turn)
				{
					dispatchNextTool();
				}
			});
		}
		return;
	}
	if (!validateChatToolHistory(m_history, error))
	{
		abortTurn(error, "error");
		return;
	}
	auto session = m_store.session(m_sessionId);
	auto messages = session.value("messages").toArray();
	m_message = {{"id", newId()}, {"turnId", m_turnId}, {"role", "assistant"}, {"text", ""}, {"status", "streaming"}};
	messages.append(m_message);
	session.insert("messages", messages);
	m_pendingCalls = {};
	m_pendingIndex = 0;
	if (!m_store.saveSession(session, error) || !startRequest(m_history, error))
	{
		m_message.insert("error", error);
		finish("error");
		return;
	}
	emit sessionChanged(m_sessionId);
	emit stateChanged();
}

void ChatController::abortTurn(const QString& reason, const QString& status)
{
	if (m_preparing)
	{
		m_preparationCanceled->store(true);
		m_preparing = false;
	}
	m_client->cancel();
	while (m_toolTurn && m_pendingIndex < m_pendingCalls.size())
	{
		QString error;
		const auto result = CommandResult::failure("canceled", reason).toJson();
		if (!appendToolResult(m_pendingCalls[m_pendingIndex].toObject(), result, error))
		{
			break;
		}
		++m_pendingIndex;
	}
	m_message.insert("error", reason);
	finish(status);
}

void ChatController::finish(const QString& status)
{
	if (m_message.value("status") == "complete" && !m_message.value("tool_calls").toArray().isEmpty())
	{
		// Keep a completed tool-call message paired with its cancellation results
		// in future history even when the overall turn is stopped.
		m_message.insert("turnStatus", status);
	}
	else
	{
		m_message.insert("status", status);
	}
	m_toolTurn = false;
	m_dispatcher->cancel();
	m_key.clear();
	m_systemPrompt.clear();
	m_history = {};
	m_pendingCalls = {};
	m_invalidatedReason.clear();
	// A hosted search approval never carries over to another turn.
	m_hostedApproval = {};
	m_hostedSearch = HostedSearch::Undecided;
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
	if (m_toolTurn)
	{
		abortTurn(message("The chat tool turn was canceled."), "canceled");
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
