#include "agent/chat/ChatDock.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScreen>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "ConfigManager.h"
#include "ToolButton.h"
#include "agent/chat/ChatController.h"
#include "agent/chat/ChatSessionsDialog.h"
#include "agent/chat/ChatSettingsDialog.h"
#include "agent/chat/ChatTranscript.h"
#include "embed.h"

namespace lmms::agent::chat {
namespace {

QString setting(const QString& key, const QString& fallback = {})
{ return ConfigManager::inst()->value("agentchat", key, fallback); }

void saveSetting(const QString& key, const QString& value)
{ ConfigManager::inst()->setValue("agentchat", key, value); }

} // namespace

ChatDock::ChatDock(QMainWindow* window)
	: QDockWidget(QStringLiteral("Agent"), window)
	, m_store(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("agent/sessions"))
	, m_connectionStore(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("agent"))
{
	setObjectName("agentChatDock");
	setAllowedAreas(Qt::RightDockWidgetArea);
	setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);
	buildWorkspace();
	m_controller = new ChatController(m_store, this);
	m_controller->setToolsEnabled(true);
	connect(m_controller, &ChatController::approvalRequested, this, [this](const QJsonObject& approval) {
		if (approval.value("sessionId") == m_current.value("id"))
		{
			m_current = m_store.session(m_current.value("id").toString());
			renderSession();
		}
		refreshSessions();
	});
	connect(m_messages, &ChatTranscript::approvalDecided, this, [this](const QJsonObject& approval, bool approved) {
		QString error;
		if (!m_controller->decideTool(approval, approved, error))
		{
			showError(error);
		}
		updateRequestControls();
	});
	connect(m_messages, &ChatTranscript::approvalDetailsRequested, this, [this](const QJsonObject& approval) {
		auto review = approval;
		review.remove("token");
		QMessageBox details(QMessageBox::Information, tr("Tool call details"), approval.value("command").toString(),
							QMessageBox::Close, this);
		details.setObjectName("agentToolDetails");
		details.setTextFormat(Qt::PlainText);
		details.setInformativeText(approval.value("purpose").toString());
		details.setDetailedText(QString::fromUtf8(QJsonDocument(review).toJson(QJsonDocument::Indented)));
		details.exec();
	});
	connect(m_controller, &ChatController::messageUpdated, this, &ChatDock::updateStreamingMessage);
	connect(m_controller, &ChatController::stateChanged, this, &ChatDock::updateRequestControls);
	connect(m_controller, &ChatController::sessionChanged, this, [this](const QString& id) {
		if (m_current.value("id").toString() == id)
		{
			const auto session = m_store.session(id);
			if (!session.isEmpty())
			{
				m_current = session;
				renderSession();
			}
		}
		refreshSessions();
	});
	connect(m_send, &QToolButton::clicked, this, &ChatDock::sendOrStop);
	connect(m_model, &QToolButton::clicked, this, &ChatDock::showModelMenu);
	connect(m_reasoning, &QToolButton::clicked, this, &ChatDock::showReasoningMenu);
	connect(m_attach, &QToolButton::clicked, this, &ChatDock::showAttachments);
	connect(m_attachmentChip, &QToolButton::clicked, this, &ChatDock::showAttachments);
	connect(m_attachmentRemove, &QToolButton::clicked, this, &ChatDock::removeFirstAttachment);
	connect(m_controller, &ChatController::attachmentStatus, this,
			[this](const QString& id, const QString& status, qint64 sent, qint64 total) {
				if (m_current.value("id").toString() != id)
				{
					return;
				}
				m_footer->setText(status == "preparing" ? tr("Reading and encoding selected attachments…")
														: tr("Request upload: %1 / %2 bytes").arg(sent).arg(total));
			});
	m_input->installEventFilter(this);
	connect(m_search, &QLineEdit::textChanged, this, &ChatDock::refreshSessions);
	connect(m_archived, &QCheckBox::toggled, this, &ChatDock::refreshSessions);
	connect(m_sessions, &QListWidget::currentItemChanged, this, [this] { selectSession(); });
	connect(m_sessions, &QWidget::customContextMenuRequested, this, &ChatDock::showSessionMenu);
	connect(m_input, &QPlainTextEdit::textChanged, this, [this] {
		m_current.insert("draft", m_input->toPlainText());
		persistCurrent();
		updateRequestControls();
	});
	window->installEventFilter(this);
	m_current = m_store.session(setting("session"));
	if (m_current.isEmpty())
	{
		createSession();
	}
	else
	{
		refreshSessions();
		renderSession();
	}
	window->addDockWidget(Qt::RightDockWidgetArea, this);
	m_expandedWidth = qBound(560, setting("width", "640").toInt(), 1200);
	window->resizeDocks({this}, {m_expandedWidth}, Qt::Horizontal);
	if (setting("floating") == "1")
	{
		setFloating(true);
		restoreGeometry(QByteArray::fromBase64(setting("geometry").toLatin1()));
		bool visible = false;
		for (auto* screen : QGuiApplication::screens())
		{
			visible = visible || screen->availableGeometry().intersects(frameGeometry());
		}
		if (!visible)
		{
			move(window->pos() + QPoint(40, 40));
		}
	}
	setCollapsed(setting("collapsed") == "1");
	refreshConnection();
	arrangeWorkspace();
}

ChatDock::~ChatDock()
{
	// QObject children are destroyed after C++ members; persist cancellation
	// while the injected store and the view are still alive.
	m_controller->stop();
}

void ChatDock::setCollapsed(bool collapsed)
{
	if (collapsed && !m_collapsed)
	{
		m_expandedWidth = width();
	}
	m_collapsed = collapsed;
	m_content->setVisible(true);
	m_headerActions->setVisible(!collapsed);
	m_expand->setText(collapsed ? QStringLiteral("Agent ›") : QStringLiteral("‹ 收起"));
	m_expand->setIcon(embed::getIconPixmap(collapsed ? "agent_chat_agent" : "agent_chat_collapse", 20, 20));
	m_expand->setIconSize(collapsed ? QSize(23, 23) : QSize(18, 18));
	m_expand->setToolTip(collapsed ? QStringLiteral("展开 Agent") : QStringLiteral("收起 Agent"));
	m_expand->setAccessibleName(m_expand->toolTip());
	setMinimumWidth(collapsed ? 48 : (isFloating() ? 600 : 560));
	setMaximumWidth(collapsed ? 48 : QWIDGETSIZE_MAX);
	if (!collapsed)
	{
		if (isFloating())
		{
			resize(m_expandedWidth, height());
		}
		else
		{
			static_cast<QMainWindow*>(parentWidget())->resizeDocks({this}, {m_expandedWidth}, Qt::Horizontal);
		}
	}
	arrangeWorkspace();
}

void ChatDock::saveLayout()
{
	saveSetting("collapsed", m_collapsed ? "1" : "0");
	saveSetting("width", QString::number(m_collapsed ? m_expandedWidth : width()));
	saveSetting("floating", isFloating() ? "1" : "0");
	saveSetting("geometry", QString::fromLatin1(saveGeometry().toBase64()));
}

bool ChatDock::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_input && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress))
	{
		auto* key = static_cast<QKeyEvent*>(event);
		if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && key->modifiers() == Qt::NoModifier)
		{
			event->accept();
			if (event->type() == QEvent::KeyPress && !m_controller->active() && m_send->isEnabled())
			{
				sendOrStop();
			}
			return true;
		}
	}
	if ((watched == m_content || watched == m_header) && event->type() == QEvent::Resize)
	{
		arrangeWorkspace();
	}
	if (watched == parentWidget() && event->type() == QEvent::Close)
	{
		saveLayout();
	}
	return QDockWidget::eventFilter(watched, event);
}

bool ChatDock::persistCurrent()
{
	QString error;
	if (!m_store.saveSession(m_current, error))
	{
		showError(error);
		return false;
	}
	return true;
}

void ChatDock::showError(const QString& error)
{ QMessageBox::warning(this, QStringLiteral("Agent"), error); }

void ChatDock::createSession()
{
	m_current = ChatStore::newSession();
	if (persistCurrent())
	{
		refreshSessions();
		renderSession();
	}
}

void ChatDock::refreshSessions()
{
	const QSignalBlocker blocker(m_sessions);
	m_sessions->clear();
	const auto sessions = m_store.sessions();
	for (bool pinned : {true, false})
	{
		for (const auto& value : sessions)
		{
			const auto session = value.toObject();
			if (session.value("pinned").toBool() != pinned
				|| session.value("archived").toBool() != m_archived->isChecked()
				|| !session.value("title").toString().contains(m_search->text(), Qt::CaseInsensitive))
			{
				continue;
			}
			auto* item = new QListWidgetItem(
				(pinned ? QStringLiteral("★ ") : QString{}) + session.value("title").toString(), m_sessions);
			const auto approval = m_controller->pendingApproval();
			if (approval.value("sessionId") == session.value("id") && approval.value("approvalRequired").toBool())
			{
				item->setText(item->text() + "\n" + tr("Awaiting tool approval"));
			}
			item->setData(Qt::UserRole, session.value("id").toString());
			item->setSizeHint(QSize(152, 72));
			if (session.value("id") == m_current.value("id"))
			{
				m_sessions->setCurrentItem(item);
			}
		}
	}
}

void ChatDock::selectSession()
{
	if (const auto* item = m_sessions->currentItem())
	{
		m_current = m_store.session(item->data(Qt::UserRole).toString());
		renderSession();
	}
}

void ChatDock::renderSession()
{
	auto* scroll = m_messages->verticalScrollBar();
	const bool follow = scroll->value() >= scroll->maximum() - 2;
	if (!m_renderedSession.isEmpty())
	{
		m_scrollPositions.insert(m_renderedSession, scroll->value());
	}
	const auto id = m_current.value("id").toString();
	const bool sameSession = id == m_renderedSession;
	saveSetting("session", m_current.value("id").toString());
	m_title->setText(m_current.value("title").toString());
	const QSignalBlocker blocker(m_input);
	m_input->setPlainText(m_current.value("draft").toString());
	m_streamMessageId.clear();
	QJsonArray messages;
	for (const auto& message : m_current.value("messages").toArray())
	{
		auto object = message.toObject();
		if (m_controller->activeSessionId() == id && object.value("id") == m_controller->currentMessage().value("id"))
		{
			object = m_controller->currentMessage();
			m_streamMessageId = object.value("id").toString();
		}
		messages.append(object);
	}
	m_messages->setMessages(messages);
	m_renderedSession = id;
	scroll->setValue(sameSession && follow ? scroll->maximum() : m_scrollPositions.value(id, scroll->maximum()));
	refreshConnection();
	arrangeWorkspace();
}

void ChatDock::showSessionMenu(const QPoint& position)
{
	QStringList selected;
	for (const auto* item : m_sessions->selectedItems())
	{
		selected.append(item->data(Qt::UserRole).toString());
	}
	if (selected.isEmpty())
	{
		return;
	}
	QMenu menu(this);
	auto* rename = menu.addAction(QStringLiteral("重命名"));
	rename->setEnabled(selected.size() == 1);
	auto* pin = menu.addAction(QStringLiteral("切换置顶"));
	auto* archive = menu.addAction(m_archived->isChecked() ? QStringLiteral("取消归档") : QStringLiteral("归档"));
	auto* exportAction = menu.addAction(tr("Export JSON (includes sent attachment content)"));
	exportAction->setEnabled(selected.size() == 1);
	menu.addSeparator();
	auto* remove = menu.addAction(QStringLiteral("删除"));
	auto* action = menu.exec(m_sessions->viewport()->mapToGlobal(position));
	if (!action)
	{
		return;
	}
	if (action == remove
		&& QMessageBox::question(
			   this, QStringLiteral("删除会话"),
			   QStringLiteral("删除所选 %1 个会话？原始附件和工程素材不会被删除。").arg(selected.size()))
			!= QMessageBox::Yes)
	{
		return;
	}
	for (const auto& id : selected)
	{
		auto session = m_store.session(id);
		QString error;
		if (action == remove)
		{
			if (m_controller->activeSessionId() == id)
			{
				m_controller->stop();
			}
			if (!m_store.removeSession(id, error))
			{
				showError(error);
			}
			if (id == m_current.value("id").toString())
			{
				m_current = {};
			}
			continue;
		}
		if (action == exportAction)
		{
			const auto path = QFileDialog::getSaveFileName(
				this, tr("Export session (includes sent attachment content)"), "session.json", "JSON (*.json)");
			if (!path.isEmpty() && !m_store.exportSession(id, path, error))
			{
				showError(error);
			}
			continue;
		}
		if (action == rename)
		{
			bool ok = false;
			const auto title = QInputDialog::getText(this, QStringLiteral("重命名会话"), QStringLiteral("名称"),
													 QLineEdit::Normal, session.value("title").toString(), &ok)
								   .trimmed();
			if (!ok || title.isEmpty())
			{
				continue;
			}
			session.insert("title", title);
		}
		else if (action == pin)
		{
			session.insert("pinned", !session.value("pinned").toBool());
		}
		else if (action == archive)
		{
			session.insert("archived", !m_archived->isChecked());
		}
		if (!m_store.saveSession(session, error))
		{
			showError(error);
		}
		if (id == m_current.value("id").toString())
		{
			m_current = session;
		}
	}
	if (m_current.isEmpty())
	{
		createSession();
	}
	else
	{
		refreshSessions();
	}
}

void ChatDock::showSessionManagement()
{
	ChatSessionsDialog dialog(m_store, this);
	connect(&dialog, &ChatSessionsDialog::sessionAboutToRemove, this, [this](const QString& id) {
		if (m_controller->activeSessionId() == id)
		{
			m_controller->stop();
		}
	});
	dialog.exec();
	const auto selected = dialog.selectedSession();
	m_current = m_store.session(selected.isEmpty() ? m_current.value("id").toString() : selected);
	if (m_current.isEmpty())
	{
		createSession();
	}
	else
	{
		refreshSessions();
		renderSession();
	}
}

void ChatDock::showSettings()
{
	ChatSettingsDialog dialog(this);
	if (dialog.exec() == QDialog::Accepted)
	{
		m_approval->setText(setting("approval", "request") == "none" ? QStringLiteral("无需批准")
																	 : QStringLiteral("请求批准"));
		refreshConnection();
	}
}

void attachChatDock(QMainWindow* window, QMenu* viewMenu)
{
	auto* dock = new ChatDock(window);
	auto* action = new QAction(QStringLiteral("Agent 聊天"), dock);
	viewMenu->addAction(action);
	// MainWindow rebuilds this menu before each display.
	QObject::connect(viewMenu, &QMenu::aboutToShow, dock, [viewMenu, action] { viewMenu->addAction(action); });
	QObject::connect(action, &QAction::triggered, dock, [dock] {
		dock->show();
		dock->setCollapsed(false);
		dock->raise();
	});
}

} // namespace lmms::agent::chat
