#ifndef LMMS_AGENT_CHAT_TOOL_DISPATCHER_H
#define LMMS_AGENT_CHAT_TOOL_DISPATCHER_H

#include <QObject>
#include <memory>

#include "agent/chat/ChatTools.h"

namespace lmms::agent::chat {

enum class ChatApprovalMode
{
	Request,
	None
};

// Dispatches one concrete call at a time on the GUI thread. Approval and network
// waits never own a CommandBus batch or an audio changes guard.
class ChatToolDispatcher : public QObject
{
	Q_OBJECT
public:
	explicit ChatToolDispatcher(QObject* parent = nullptr);
	~ChatToolDispatcher() override;
	bool beginTurn(const QString& sessionId, const QString& turnId, QString& error);
	QJsonArray definitions() const;
	QJsonObject pendingApproval() const;
	bool executing() const;
	bool submit(const QJsonObject& call, QString& error, ChatApprovalMode mode = ChatApprovalMode::Request);
	bool decide(const QString& token, const QString& sessionId, const QString& turnId, const QString& callId,
				const QJsonObject& arguments, bool approved, QString& error);
	void cancel();

signals:
	void approvalRequested(const QJsonObject& approval);
	void toolFinished(const QJsonObject& outcome);
	void invalidated(const QString& sessionId, const QString& turnId, const QString& reason);

private:
	struct State;
	std::unique_ptr<State> m_state;
	void invalidate(const QString& reason);
	void execute(const QString& token);
	void executeNetworkTask();
	void executeDownload();
	void finishNetworkTask(const QJsonObject& result);
	void finishPending(const QJsonObject& result);
};

} // namespace lmms::agent::chat
#endif
