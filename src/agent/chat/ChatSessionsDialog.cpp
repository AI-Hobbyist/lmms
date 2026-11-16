#include "agent/chat/ChatSessionsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "ToolButton.h"
#include "agent/chat/ChatStore.h"
#include "embed.h"

namespace lmms::agent::chat {
namespace {

QLabel* label(QWidget* parent, const QString& text, QRect rectangle, int pixels = 13, bool bold = false)
{
	auto* result = new QLabel(text, parent);
	result->setGeometry(rectangle);
	auto font = result->font();
	font.setPixelSize(pixels);
	font.setBold(bold);
	result->setFont(font);
	return result;
}

QToolButton* button(QWidget* parent, const QString& text, QRect rectangle, std::string_view icon = {})
{
	auto* result = new gui::ToolButton(parent);
	result->setGeometry(rectangle);
	result->setText(text);
	result->setAccessibleName(text);
	result->setToolTip(text);
	result->setProperty("agentChatControl", true);
	auto font = result->font();
	font.setPixelSize(13);
	result->setFont(font);
	if (!icon.empty())
	{
		result->setIcon(embed::getIconPixmap(icon, 18, 18));
		result->setIconSize({18, 18});
	}
	result->setToolButtonStyle(text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
	return result;
}

void divider(QWidget* parent, QRect rectangle)
{
	auto* line = new QFrame(parent);
	line->setFrameShape(QFrame::HLine);
	line->setGeometry(rectangle);
}

} // namespace

ChatSessionsDialog::ChatSessionsDialog(ChatStore& store, QWidget* parent)
	: QDialog(parent)
	, m_store(store)
{
	setObjectName("agentSessionsDialog");
	setWindowTitle(QStringLiteral("Agent 会话管理"));
	resize(1600, 1000);
	setMinimumSize(1000, 700);
	// Preserve the measured board geometry; smaller desktops can scroll it.
	auto* scroll = new QScrollArea(this);
	scroll->setObjectName("agentManagerScroll");
	scroll->setFrameShape(QFrame::NoFrame);
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(scroll);
	auto* page = new QWidget;
	page->setFixedSize(1600, 1000);
	scroll->setWidget(page);
	auto* header = new QWidget(page);
	header->setObjectName("agentSettingsHeader");
	header->setGeometry(0, 0, 1600, 66);
	auto* agent = label(header, {}, {24, 23, 23, 23});
	agent->setPixmap(embed::getIconPixmap("agent_chat_agent", 23, 23));
	label(header, QStringLiteral("Agent 会话"), {60, 17, 200, 33}, 21, true);
	auto* back = button(header, QStringLiteral("返回聊天"), {1406, 15, 166, 36}, "agent_chat_left");
	connect(back, &QToolButton::clicked, this, &QDialog::reject);
	label(page, QStringLiteral("会话管理"), {48, 111, 400, 42}, 27, true);
	label(page, QStringLiteral("会话与工程文件分开管理，切换会话不会重新加载工程。"), {48, 156, 800, 21});
	m_search = new QLineEdit(page);
	m_search->setObjectName("agentManagerSearch");
	m_search->setGeometry(48, 215, 716, 44);
	m_search->setPlaceholderText(QStringLiteral("搜索标题、消息或工程名称"));
	m_search->addAction(QIcon(embed::getIconPixmap("agent_chat_search", 18, 18)), QLineEdit::LeadingPosition);
	m_projects = new QComboBox(page);
	m_projects->setGeometry(786, 218, 172, 38);
	m_projects->addItem(QStringLiteral("全部工程"), QString{});
	QSet<QString> projects;
	for (const auto& value : store.sessions())
	{
		const auto project = value.toObject().value("projectName").toString();
		if (!project.isEmpty() && !projects.contains(project))
		{
			projects.insert(project);
			m_projects->addItem(project, project);
		}
	}
	auto* importButton = button(page, QStringLiteral("导入会话"), {1222, 218, 128, 38}, "agent_chat_upload");
	connect(importButton, &QToolButton::clicked, this, &ChatSessionsDialog::importSessions);
	auto* create = button(page, QStringLiteral("新建会话"), {1366, 218, 186, 38}, "agent_chat_new");
	connect(create, &QToolButton::clicked, this, [this] {
		auto session = ChatStore::newSession();
		QString error;
		if (m_store.saveSession(session, error))
		{
			openSession(session.value("id").toString());
		}
		else
		{
			reportError(error);
		}
	});
	const QStringList filters{QStringLiteral("全部"), QStringLiteral("已置顶"), QStringLiteral("已归档")};
	const QRect filterRects[]{{48, 289, 95, 34}, {157, 289, 102, 34}, {273, 289, 102, 34}};
	for (int i = 0; i < filters.size(); ++i)
	{
		auto* filter = button(page, filters[i], filterRects[i]);
		filter->setObjectName(QStringLiteral("agentManagerFilter%1").arg(i));
		filter->setCheckable(true);
		filter->setAutoExclusive(true);
		filter->setChecked(i == 0);
		connect(filter, &QToolButton::clicked, this, [this, i] {
			m_filter = i;
			refresh();
		});
	}
	auto* batch = new QFrame(page);
	batch->setProperty("agentSettingsCard", true);
	batch->setGeometry(48, 350, 1504, 53);
	m_selection = label(batch, {}, {21, 16, 900, 21});
	m_export = button(batch, QStringLiteral("导出"), {1150, 8, 100, 36}, "agent_chat_export");
	m_archive = button(batch, QStringLiteral("归档"), {1256, 8, 100, 36}, "agent_chat_archive");
	m_archive->setObjectName("agentManagerArchive");
	m_delete = button(batch, QStringLiteral("删除"), {1362, 8, 122, 36}, "agent_chat_trash");
	connect(m_export, &QToolButton::clicked, this, &ChatSessionsDialog::exportSelected);
	connect(m_archive, &QToolButton::clicked, this, &ChatSessionsDialog::archiveSelected);
	connect(m_delete, &QToolButton::clicked, this, &ChatSessionsDialog::deleteSelected);
	label(page, QStringLiteral("会话"), {95, 432, 400, 18}, 11);
	label(page, QStringLiteral("工程"), {692, 432, 240, 18}, 11);
	label(page, QStringLiteral("模型连接"), {985, 432, 200, 18}, 11);
	label(page, QStringLiteral("最近更新"), {1219, 432, 200, 18}, 11);
	divider(page, {48, 466, 1504, 1});
	m_rows = new QScrollArea(page);
	m_rows->setObjectName("agentManagerRows");
	m_rows->setFrameShape(QFrame::NoFrame);
	m_rows->setGeometry(48, 469, 1504, 440);
	m_rows->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	label(page, QStringLiteral("删除或归档会话不会删除 LMMS 工程文件和已下载素材。"), {48, 934, 1000, 19}, 12);
	connect(m_search, &QLineEdit::textChanged, this, &ChatSessionsDialog::refresh);
	connect(m_projects, &QComboBox::currentIndexChanged, this, &ChatSessionsDialog::refresh);
	refresh();
}

QString ChatSessionsDialog::selectedSession() const
{ return m_selectedSession; }

void ChatSessionsDialog::refresh()
{
	m_checked.clear();
	auto* rows = new QWidget;
	int row = 0;
	for (const auto& value : m_store.sessions())
	{
		const auto session = value.toObject();
		const auto title = session.value("title").toString();
		const auto project = session.value("projectName").toString();
		const auto searchText
			= title + project + QString::fromUtf8(QJsonDocument(session.value("messages").toArray()).toJson());
		if ((m_filter == 1 && !session.value("pinned").toBool())
			|| (m_filter == 2 && !session.value("archived").toBool())
			|| (!m_projects->currentData().toString().isEmpty() && m_projects->currentData().toString() != project)
			|| !searchText.contains(m_search->text(), Qt::CaseInsensitive))
		{
			continue;
		}
		const auto id = session.value("id").toString();
		const int y = row++ * 80;
		auto* check = new QCheckBox(rows);
		check->setGeometry(14, y + 31, 17, 17);
		check->setObjectName("agentManagerSelect_" + id);
		check->setAccessibleName(QStringLiteral("选择 %1").arg(title));
		connect(check, &QCheckBox::toggled, this, [this, id](bool selected) {
			if (selected)
			{
				m_checked.insert(id);
			}
			else
			{
				m_checked.remove(id);
			}
			updateSelection();
		});
		auto* chat = label(rows, {}, {52, y + 26, 19, 19});
		chat->setPixmap(embed::getIconPixmap("agent_chat_sessions", 19, 19));
		auto* open = button(rows, title, {85, y + 22, 480, 22});
		open->setText({});
		auto* caption = label(open, title, {0, 0, 480, 22}, 14);
		caption->setObjectName("agentManagerTitle_" + id);
		caption->setAttribute(Qt::WA_TransparentForMouseEvents);
		open->setObjectName("agentManagerOpen_" + id);
		open->setProperty("agentSessionTitle", true);
		connect(open, &QToolButton::clicked, this, [this, id] { openSession(id); });
		const auto state = session.value("archived").toBool() ? QStringLiteral("已归档")
			: session.value("pinned").toBool()				  ? QStringLiteral("已置顶")
															  : QStringLiteral("会话");
		label(rows, state, {85, y + 49, 480, 16}, 10);
		label(rows, project.isEmpty() ? QStringLiteral("未关联工程") : project, {644, y + 27, 260, 21});
		const auto model = session.value("connectionName").toString();
		label(rows, model.isEmpty() ? QStringLiteral("未配置") : model, {937, y + 27, 210, 21});
		const auto updated = QDateTime::fromString(session.value("updatedAt").toString(), Qt::ISODateWithMs);
		label(rows, updated.toLocalTime().toString("yyyy-MM-dd HH:mm"), {1171, y + 27, 220, 19}, 12);
		auto* more = button(rows, {}, {1439, y + 22, 34, 34}, "agent_chat_more");
		more->setToolTip(QStringLiteral("会话操作"));
		more->setAccessibleName(QStringLiteral("管理 %1").arg(title));
		connect(more, &QToolButton::clicked, this, [this, id, more] { sessionMenu(id, more); });
		divider(rows, {0, y + 79, 1504, 1});
	}
	rows->setFixedSize(1504, qMax(1, row * 80));
	m_rows->setWidget(rows);
	updateSelection();
}

void ChatSessionsDialog::updateSelection()
{
	m_selection->setText(QStringLiteral("已选择 %1 个会话").arg(m_checked.size()));
	m_export->setEnabled(!m_checked.isEmpty());
	m_archive->setEnabled(!m_checked.isEmpty());
	m_delete->setEnabled(!m_checked.isEmpty());
}

void ChatSessionsDialog::openSession(const QString& id)
{
	m_selectedSession = id;
	accept();
}

void ChatSessionsDialog::sessionMenu(const QString& id, QToolButton* anchor)
{
	auto session = m_store.session(id);
	QMenu menu(this);
	auto* rename = menu.addAction(QStringLiteral("重命名"));
	auto* pin = menu.addAction(session.value("pinned").toBool() ? QStringLiteral("取消置顶") : QStringLiteral("置顶"));
	auto* archive
		= menu.addAction(session.value("archived").toBool() ? QStringLiteral("取消归档") : QStringLiteral("归档"));
	const auto* action = menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
	if (action == rename)
	{
		bool ok = false;
		const auto title = QInputDialog::getText(this, QStringLiteral("重命名会话"), QStringLiteral("名称"),
												 QLineEdit::Normal, session.value("title").toString(), &ok)
							   .trimmed();
		if (!ok || title.isEmpty())
		{
			return;
		}
		session.insert("title", title);
	}
	else if (action == pin || action == archive)
	{
		const auto key = action == pin ? "pinned" : "archived";
		session.insert(key, !session.value(key).toBool());
	}
	else
	{
		return;
	}
	QString error;
	if (!m_store.saveSession(session, error))
	{
		reportError(error);
	}
	refresh();
}

void ChatSessionsDialog::archiveSelected()
{
	for (const auto& id : m_checked)
	{
		auto session = m_store.session(id);
		session.insert("archived", true);
		QString error;
		if (!m_store.saveSession(session, error))
		{
			reportError(error);
			break;
		}
	}
	refresh();
}

void ChatSessionsDialog::deleteSelected()
{
	if (QMessageBox::question(this, QStringLiteral("删除会话"),
							  QStringLiteral("删除所选 %1 个会话？工程文件和素材不会被删除。").arg(m_checked.size()))
		!= QMessageBox::Yes)
	{
		return;
	}
	for (const auto& id : m_checked)
	{
		QString error;
		if (!m_store.removeSession(id, error))
		{
			reportError(error);
			break;
		}
	}
	refresh();
}

void ChatSessionsDialog::exportSelected()
{
	const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择会话导出目录"));
	if (directory.isEmpty())
	{
		return;
	}
	for (const auto& id : m_checked)
	{
		QString error;
		if (!m_store.exportSession(id, QDir(directory).filePath(id + ".json"), error))
		{
			reportError(error);
			return;
		}
	}
}

void ChatSessionsDialog::importSessions()
{
	const auto paths = QFileDialog::getOpenFileNames(this, QStringLiteral("导入会话"), {}, "JSON (*.json)");
	for (const auto& path : paths)
	{
		QString error;
		if (m_store.importSession(path, error).isEmpty())
		{
			reportError(error);
			break;
		}
	}
	refresh();
}

void ChatSessionsDialog::reportError(const QString& error)
{ QMessageBox::warning(this, QStringLiteral("会话管理"), error); }

} // namespace lmms::agent::chat
