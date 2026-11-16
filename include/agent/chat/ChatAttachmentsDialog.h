#ifndef LMMS_AGENT_CHAT_ATTACHMENTS_DIALOG_H
#define LMMS_AGENT_CHAT_ATTACHMENTS_DIALOG_H

#include <QDialog>

#include "agent/chat/ChatAttachment.h"

class QLabel;
class QScrollArea;

namespace lmms::agent::chat {

class ChatAttachmentsDialog : public QDialog
{
	Q_OBJECT
public:
	ChatAttachmentsDialog(ChatConnection connection, QVector<ChatAttachment> attachments, QWidget* parent = nullptr);
	void addFiles(const QStringList& paths);
	QVector<ChatAttachment> attachments() const;

protected:
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dropEvent(QDropEvent* event) override;

private:
	void refresh();
	ChatConnection m_connection;
	QVector<ChatAttachment> m_attachments;
	QScrollArea* m_rows = nullptr;
	QLabel* m_notice = nullptr;
};

} // namespace lmms::agent::chat
#endif
