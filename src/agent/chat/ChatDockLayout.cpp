#include <QAction>
#include <QCheckBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QToolButton>

#include "ConfigManager.h"
#include "ToolButton.h"
#include "agent/chat/ChatDock.h"
#include "embed.h"

namespace lmms::agent::chat {
namespace {

class ChatTitleBar : public QWidget
{
public:
	using QWidget::QWidget;

	QSize sizeHint() const override
	{ return {640, minimumHeight()}; }

	QSize minimumSizeHint() const override
	{ return {48, minimumHeight()}; }
};

gui::ToolButton* iconButton(QWidget* parent, const QString& text, std::string_view icon, bool withText = false)
{
	auto* result = new gui::ToolButton(parent);
	result->setText(text);
	result->setToolTip(text);
	result->setAccessibleName(text);
	result->setIcon(embed::getIconPixmap(icon, 18, 18));
	result->setIconSize(QSize(18, 18));
	result->setToolButtonStyle(withText ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
	result->setProperty("agentChatControl", true);
	auto font = result->font();
	font.setPixelSize(13);
	result->setFont(font);
	return result;
}

} // namespace

void ChatDock::buildWorkspace()
{
	// Measurements are from Penpot board 01, relative to its 640 x 878 chat region.
	// The title bar is part of the same measured region, not an extra native row.
	m_header = new ChatTitleBar(this);
	m_header->setObjectName("agentHeader");
	m_header->setFixedHeight(54);
	m_header->installEventFilter(this);
	setTitleBarWidget(m_header);
	m_floatingChrome = new QWidget(m_header);
	m_floatingChrome->setObjectName("agentFloatingChrome");
	auto* floatingIcon = new QLabel(m_floatingChrome);
	floatingIcon->setPixmap(embed::getIconPixmap("agent_chat_agent", 18, 18));
	floatingIcon->setGeometry(16, 9, 18, 18);
	auto* floatingTitle = new QLabel(QStringLiteral("LMMS Agent · 独立窗口"), m_floatingChrome);
	floatingTitle->setGeometry(43, 7, 500, 21);
	auto* floatingClose = iconButton(m_floatingChrome, QStringLiteral("关闭聊天窗口"), "agent_chat_close");
	floatingClose->setObjectName("agentFloatingClose");
	connect(floatingClose, &QToolButton::clicked, this, &QWidget::hide);
	auto* projectIcon = new QLabel(m_floatingChrome);
	projectIcon->setPixmap(embed::getIconPixmap("agent_chat_folder", 18, 18));
	projectIcon->setGeometry(22, 57, 18, 18);
	auto* floatingProject = new QLabel(QStringLiteral("当前工程"), m_floatingChrome);
	floatingProject->setGeometry(52, 53, 700, 21);
	auto* redock = iconButton(m_floatingChrome, QStringLiteral("吸附回右侧"), "agent_chat_detach", true);
	redock->setObjectName("agentRedock");
	connect(redock, &QToolButton::clicked, this, [this] { setFloating(false); });
	m_headerActions = new QWidget(m_header);
	auto* agentIcon = new QLabel(m_headerActions);
	agentIcon->setPixmap(embed::getIconPixmap("agent_chat_agent", 22, 22));
	agentIcon->setGeometry(18, 17, 22, 22);
	auto* title = new QLabel(QStringLiteral("Agent"), m_headerActions);
	title->setObjectName("agentHeaderTitle");
	title->setGeometry(49, 14, 70, 27);
	auto* sessionsToggle = iconButton(m_headerActions, QStringLiteral("会话"), "agent_chat_sessions", true);
	sessionsToggle->setObjectName("agentSessionToggle");
	sessionsToggle->setGeometry(119, 10, 65, 34);
	auto* maximize = iconButton(m_headerActions, QStringLiteral("展开聊天窗口"), "agent_chat_expand");
	maximize->setObjectName("agentMaximize");
	auto* detach = iconButton(m_headerActions, QStringLiteral("分离 / 回接"), "agent_chat_detach");
	detach->setObjectName("agentDetach");
	m_expand = iconButton(m_header, QStringLiteral("收起 Agent"), "agent_chat_collapse");
	m_expand->setObjectName("agentCollapse");
	connect(sessionsToggle, &QToolButton::clicked, this, [this] {
		m_sessionsVisible = !m_sessionsVisible;
		arrangeWorkspace();
	});
	connect(detach, &QToolButton::clicked, this, [this] { setFloating(!isFloating()); });
	connect(maximize, &QToolButton::clicked, this, [this] {
		setFloating(true);
		isMaximized() ? showNormal() : showMaximized();
	});
	connect(m_expand, &QToolButton::clicked, this, [this] { setCollapsed(!m_collapsed); });

	m_content = new QWidget(this);
	m_content->setObjectName("agentWorkspace");
	m_content->installEventFilter(this);
	setWidget(m_content);
	m_collapsedLabel = new QLabel(QStringLiteral("AI"), m_content);
	m_restore = iconButton(m_content, QStringLiteral("展开 Agent"), "agent_chat_left");
	connect(m_restore, &QToolButton::clicked, this, [this] { setCollapsed(false); });
	m_sessionPanel = new QWidget(m_content);
	m_sessionPanel->setObjectName("agentSessionPanel");
	m_newSession = iconButton(m_sessionPanel, QStringLiteral("新建会话"), "agent_chat_new", true);
	m_newSession->setObjectName("agentNewSession");
	m_search = new QLineEdit(m_sessionPanel);
	m_search->setObjectName("agentSessionSearch");
	m_search->setPlaceholderText(QStringLiteral("搜索会话"));
	m_search->addAction(QIcon(embed::getIconPixmap("agent_chat_search", 15, 15)), QLineEdit::LeadingPosition);
	m_archived = new QCheckBox(m_sessionPanel);
	m_archived->hide();
	m_sessions = new QListWidget(m_sessionPanel);
	m_sessions->setObjectName("agentSessionList");
	m_sessions->setFrameShape(QFrame::NoFrame);
	m_sessions->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_sessions->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_sessions->setContextMenuPolicy(Qt::CustomContextMenu);
	m_manage = iconButton(m_sessionPanel, QStringLiteral("会话管理"), "agent_chat_archive", true);
	m_manage->setObjectName("agentSessionManagement");
	m_settings = iconButton(m_sessionPanel, QStringLiteral("Agent 设置"), "agent_chat_settings", true);
	m_settings->setObjectName("agentSettings");
	connect(m_newSession, &QToolButton::clicked, this, &ChatDock::createSession);
	connect(m_settings, &QToolButton::clicked, this, &ChatDock::showSettings);
	connect(m_manage, &QToolButton::clicked, this, &ChatDock::showSessionManagement);

	m_chatPanel = new QWidget(m_content);
	m_chatPanel->setObjectName("agentConversation");
	m_title = new QLabel(m_chatPanel);
	m_title->setObjectName("agentConversationTitle");
	m_more = iconButton(m_chatPanel, QStringLiteral("会话操作"), "agent_chat_more");
	connect(m_more, &QToolButton::clicked, this, [this] { showSessionMenu(QPoint(0, 0)); });
	m_context = new QLabel(QStringLiteral("  当前工程"), m_chatPanel);
	m_context->setObjectName("agentProjectContext");
	m_messages = new QTextBrowser(m_chatPanel);
	m_messages->setObjectName("agentMessages");
	m_messages->setFrameShape(QFrame::NoFrame);
	m_messages->setOpenExternalLinks(false);
	m_model = iconButton(m_chatPanel, QStringLiteral("未配置模型"), "agent_chat_settings", true);
	m_model->setObjectName("agentModel");
	m_model->setEnabled(false);
	m_composer = new QFrame(m_chatPanel);
	m_composer->setObjectName("agentComposer");
	m_input = new QPlainTextEdit(m_composer);
	m_input->setObjectName("agentInput");
	m_input->setFrameShape(QFrame::NoFrame);
	m_input->setPlaceholderText(QStringLiteral("继续描述你想要的编配…"));
	m_attach = iconButton(m_composer, QStringLiteral("添加附件（M2 接入）"), "agent_chat_attach");
	m_attach->setEnabled(false);
	m_reasoning = iconButton(m_composer, QStringLiteral("思考 · 自动"), "agent_chat_brain", true);
	m_reasoning->setEnabled(false);
	const auto approvalMode = ConfigManager::inst()->value("agentchat", "approval", "request");
	m_approval
		= iconButton(m_composer, approvalMode == "none" ? QStringLiteral("无需批准") : QStringLiteral("请求批准"),
					 "agent_chat_shield", true);
	connect(m_approval, &QToolButton::clicked, this, &ChatDock::showSettings);
	m_approval->setObjectName("agentApproval");
	m_send = iconButton(m_composer, QStringLiteral("发送（尚未接入 API）"), "agent_chat_send");
	m_send->setObjectName("agentSend");
	m_send->setEnabled(false);
	m_footer = new QLabel(QStringLiteral("Enter 发送  ·  Shift+Enter 换行"), m_chatPanel);
	m_footer->setObjectName("agentComposerHint");
	connect(this, &QDockWidget::topLevelChanged, this, [this] {
		setMinimumWidth(m_collapsed ? 48 : (isFloating() ? 600 : 560));
		arrangeWorkspace();
	});
	arrangeWorkspace();
}

void ChatDock::arrangeWorkspace()
{
	if (!m_footer)
	{
		return;
	}
	const int headerWidth = m_header->width();
	const bool floating = isFloating() && !m_collapsed;
	const int headerTop = floating ? 98 : 0;
	m_header->setFixedHeight(headerTop + 54);
	setMinimumHeight(headerTop + 414 + (floating ? 24 : 0));
	m_floatingChrome->setVisible(floating);
	m_floatingChrome->setGeometry(0, 0, headerWidth, 98);
	m_floatingChrome->findChild<QToolButton*>("agentFloatingClose")->setGeometry(headerWidth - 41, 2, 34, 34);
	m_floatingChrome->findChild<QToolButton*>("agentRedock")->setGeometry(headerWidth - 175, 49, 155, 36);
	m_headerActions->setGeometry(0, headerTop, headerWidth, 54);
	m_headerActions->findChild<QToolButton*>("agentMaximize")->setGeometry(headerWidth - 117, 10, 34, 34);
	m_headerActions->findChild<QToolButton*>("agentDetach")->setGeometry(headerWidth - 82, 10, 34, 34);
	m_headerActions->findChild<QToolButton*>("agentDetach")->setVisible(!floating);
	m_expand->setVisible(!floating);
	m_expand->setGeometry(m_collapsed ? QRect(5, 13, 38, 38) : QRect(headerWidth - 46, headerTop + 10, 34, 34));
	m_collapsedLabel->setVisible(m_collapsed);
	m_restore->setVisible(m_collapsed);
	m_collapsedLabel->setGeometry(15, 3, 18, 19);
	m_restore->setGeometry(7, 31, 34, 34);
	m_sessionPanel->setVisible(m_sessionsVisible && !m_collapsed);
	m_chatPanel->setVisible(!m_collapsed);
	if (m_collapsed)
	{
		return;
	}
	const int panelWidth = floating ? 220 : 180;
	const int sessionWidth = m_sessionsVisible ? panelWidth : 0;
	const int bodyHeight = m_content->height() - (floating ? 24 : 0);
	m_sessionPanel->setGeometry(0, 0, panelWidth, bodyHeight);
	m_newSession->setGeometry(14, 16, panelWidth - 28, 36);
	m_search->setGeometry(14, 64, panelWidth - 28, 32);
	m_sessions->setGeometry(12, 120, panelWidth - 24, qMax(0, bodyHeight - 232));
	m_manage->setGeometry(12, bodyHeight - 98, panelWidth - 24, 36);
	m_settings->setGeometry(12, bodyHeight - 48, panelWidth - 24, 34);
	m_chatPanel->setGeometry(sessionWidth, 0, m_content->width() - sessionWidth, bodyHeight);
	const int chatWidth = m_chatPanel->width();
	const int innerWidth = chatWidth - 40;
	m_title->setGeometry(20, 19, innerWidth - 42, 27);
	m_more->setGeometry(chatWidth - 47, 15, 34, 34);
	m_context->setGeometry(20, 57, innerWidth, 29);
	m_messages->setGeometry(20, 108, innerWidth, qMax(0, bodyHeight - 360));
	m_model->setGeometry(20, bodyHeight - 228, qMin(240, innerWidth), 22);
	m_composer->setGeometry(20, bodyHeight - 197, innerWidth, 155);
	m_input->setGeometry(14, 10, innerWidth - 28, 84);
	m_attach->setGeometry(6, 106, 34, 34);
	m_reasoning->setGeometry(45, 106, 112, 34);
	m_approval->setGeometry(165, 106, 112, 34);
	m_send->setGeometry(innerWidth - 40, 106, 34, 34);
	m_footer->setGeometry(20, bodyHeight - 31, innerWidth, 16);
}

} // namespace lmms::agent::chat
