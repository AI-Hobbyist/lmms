#ifndef LMMS_AGENT_CHAT_ADVANCED_DIALOG_H
#define LMMS_AGENT_CHAT_ADVANCED_DIALOG_H

#include <QDialog>

#include "agent/chat/ChatConnection.h"

class QComboBox;
class QLineEdit;
class QPlainTextEdit;

namespace lmms::agent::chat {

class ChatAdvancedDialog : public QDialog
{
	Q_OBJECT
public:
	explicit ChatAdvancedDialog(ChatConnection connection, QWidget* parent = nullptr);
	ChatConnection connection() const;
	void accept() override;

private:
	void selectProtocol(const QString& protocol);
	ChatConnection m_connection;
	QComboBox* m_auth = nullptr;
	QLineEdit* m_header = nullptr;
	QLineEdit* m_prefix = nullptr;
	QLineEdit* m_path = nullptr;
	QPlainTextEdit* m_options = nullptr;
};

} // namespace lmms::agent::chat

#endif
