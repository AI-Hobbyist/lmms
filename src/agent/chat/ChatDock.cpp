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
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "ConfigManager.h"
#include "ToolButton.h"
#include "agent/chat/ChatSessionsDialog.h"
#include "agent/chat/ChatSettingsDialog.h"
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
{
	setObjectName("agentChatDock");
	setAllowedAreas(Qt::RightDockWidgetArea);
	setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);
	buildWorkspace();
	connect(m_search, &QLineEdit::textChanged, this, &ChatDock::refreshSessions);
	connect(m_archived, &QCheckBox::toggled, this, &ChatDock::refreshSessions);
	connect(m_sessions, &QListWidget::currentItemChanged, this, [this] { selectSession(); });
	connect(m_sessions, &QWidget::customContextMenuRequested, this, &ChatDock::showSessionMenu);
	connect(m_input, &QPlainTextEdit::textChanged, this, [this] {
		m_current.insert("draft", m_input->toPlainText());
		persistCurrent();
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
	saveSetting("session", m_current.value("id").toString());
	m_title->setText(m_current.value("title").toString());
	const QSignalBlocker blocker(m_input);
	m_input->setPlainText(m_current.value("draft").toString());
	m_messages->clear();
	for (const auto& message : m_current.value("messages").toArray())
	{
		const auto object = message.toObject();
		m_messages->append(
			QStringLiteral("<b>%1</b><p>%2</p>")
				.arg(object.value("role").toString().toHtmlEscaped(), object.value("text").toString().toHtmlEscaped()));
	}
	if (m_current.value("messages").toArray().isEmpty())
	{
		m_messages->setPlainText(QStringLiteral("开始新的音乐创作会话。\n\n默认请求批准；工程工具将在 M3 接入。"));
	}
}

void ChatDock::showSessionMenu(const QPoint& position)
{
	const auto selected = m_sessions->selectedItems();
	if (selected.isEmpty())
	{
		return;
	}
	QMenu menu(this);
	auto* rename = menu.addAction(QStringLiteral("重命名"));
	rename->setEnabled(selected.size() == 1);
	auto* pin = menu.addAction(QStringLiteral("切换置顶"));
	auto* archive = menu.addAction(m_archived->isChecked() ? QStringLiteral("取消归档") : QStringLiteral("归档"));
	auto* exportAction = menu.addAction(QStringLiteral("导出 JSON（不包含附件文件）"));
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
	for (const auto* item : selected)
	{
		const auto id = item->data(Qt::UserRole).toString();
		auto session = m_store.session(id);
		QString error;
		if (action == remove)
		{
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
			const auto path
				= QFileDialog::getSaveFileName(this, QStringLiteral("导出会话"), "session.json", "JSON (*.json)");
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
