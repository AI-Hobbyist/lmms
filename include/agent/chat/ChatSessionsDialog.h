#ifndef LMMS_AGENT_CHAT_SESSIONS_DIALOG_H
#define LMMS_AGENT_CHAT_SESSIONS_DIALOG_H

#include <QDialog>
#include <QSet>
#include <QString>

class QLabel;
class QLineEdit;
class QComboBox;
class QScrollArea;
class QToolButton;

namespace lmms::agent::chat {

class ChatStore;

class ChatSessionsDialog : public QDialog
{
public:
	explicit ChatSessionsDialog(ChatStore& store, QWidget* parent = nullptr);
	QString selectedSession() const;

private:
	void refresh();
	void updateSelection();
	void openSession(const QString& id);
	void sessionMenu(const QString& id, QToolButton* anchor);
	void archiveSelected();
	void deleteSelected();
	void exportSelected();
	void importSessions();
	void reportError(const QString& error);

	ChatStore& m_store;
	QString m_selectedSession;
	QSet<QString> m_checked;
	QLineEdit* m_search = nullptr;
	QComboBox* m_projects = nullptr;
	QScrollArea* m_rows = nullptr;
	QLabel* m_selection = nullptr;
	QToolButton* m_export = nullptr;
	QToolButton* m_archive = nullptr;
	QToolButton* m_delete = nullptr;
	int m_filter = 0;
};

} // namespace lmms::agent::chat

#endif
