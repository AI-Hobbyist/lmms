#ifndef LMMS_AGENT_CHAT_TRANSCRIPT_H
#define LMMS_AGENT_CHAT_TRANSCRIPT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QScrollArea>
#include <QVector>

class QLabel;

namespace lmms::agent::chat {

class ChatMessage;
class ChatApprovalCard;

class ChatTranscript : public QScrollArea
{
	Q_OBJECT
public:
	explicit ChatTranscript(QWidget* parent = nullptr);
	void setMessages(const QJsonArray& messages);
	void updateMessage(const QJsonObject& message);
	QString toPlainText() const;
	void setApproval(const QJsonObject& approval);

signals:
	void approvalDecided(const QJsonObject& approval, bool approved);
	void approvalDetailsRequested(const QJsonObject& approval);

protected:
	void resizeEvent(QResizeEvent* event) override;

private:
	void arrangeMessages();
	QWidget* m_contents = nullptr;
	QLabel* m_empty = nullptr;
	QVector<ChatMessage*> m_rows;
	ChatApprovalCard* m_approval = nullptr;
};

} // namespace lmms::agent::chat
#endif
