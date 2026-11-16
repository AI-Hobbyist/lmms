#ifndef LMMS_AGENT_CHAT_DOCK_H
#define LMMS_AGENT_CHAT_DOCK_H

#include <QDockWidget>
#include <QHash>
#include <QJsonObject>

#include "agent/chat/ChatAttachment.h"
#include "agent/chat/ChatConnection.h"
#include "agent/chat/ChatStore.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QListWidget;
class QMainWindow;
class QMenu;
class QPlainTextEdit;
class QToolButton;

namespace lmms::agent::chat {

class ChatController;
class ChatTranscript;

class ChatDock : public QDockWidget
{
	Q_OBJECT
public:
	explicit ChatDock(QMainWindow* window);
	~ChatDock() override;
	void setCollapsed(bool collapsed);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void buildWorkspace();
	void arrangeWorkspace();
	void createSession();
	void refreshSessions();
	void selectSession();
	void showSessionMenu(const QPoint& position);
	void showSettings();
	void showSessionManagement();
	void renderSession();
	bool persistCurrent();
	void showError(const QString& error);
	void saveLayout();
	void refreshConnection();
	void showModelMenu();
	void showReasoningMenu();
	void showAttachments();
	void removeFirstAttachment();
	QVector<ChatAttachment> selectedAttachments() const;
	void updateRequestControls();
	void sendOrStop();
	bool retryAvailable() const;
	void updateStreamingMessage(const QString& sessionId, const QJsonObject& message);

	ChatStore m_store;
	ChatConnectionStore m_connectionStore;
	ChatConnection m_connection;
	ChatController* m_controller = nullptr;
	QHash<QString, int> m_scrollPositions;
	QString m_renderedSession;
	QString m_streamMessageId;
	QJsonObject m_current;
	QWidget* m_content = nullptr;
	QWidget* m_headerActions = nullptr;
	QWidget* m_header = nullptr;
	QWidget* m_floatingChrome = nullptr;
	QWidget* m_sessionPanel = nullptr;
	QWidget* m_chatPanel = nullptr;
	QWidget* m_composer = nullptr;
	QLabel* m_title = nullptr;
	QLabel* m_context = nullptr;
	QLabel* m_footer = nullptr;
	QLabel* m_collapsedLabel = nullptr;
	QToolButton* m_restore = nullptr;
	QToolButton* m_newSession = nullptr;
	QToolButton* m_manage = nullptr;
	QToolButton* m_settings = nullptr;
	QToolButton* m_model = nullptr;
	QToolButton* m_attach = nullptr;
	QToolButton* m_attachmentChip = nullptr;
	QToolButton* m_attachmentRemove = nullptr;
	QLabel* m_attachmentCaption = nullptr;
	QToolButton* m_reasoning = nullptr;
	QToolButton* m_approval = nullptr;
	QToolButton* m_send = nullptr;
	QToolButton* m_more = nullptr;
	QListWidget* m_sessions = nullptr;
	QLineEdit* m_search = nullptr;
	QCheckBox* m_archived = nullptr;
	ChatTranscript* m_messages = nullptr;
	QPlainTextEdit* m_input = nullptr;
	QToolButton* m_expand = nullptr;
	bool m_collapsed = false;
	bool m_sessionsVisible = true;
	int m_expandedWidth = 640;
};

void attachChatDock(QMainWindow* window, QMenu* viewMenu);

} // namespace lmms::agent::chat

#endif
