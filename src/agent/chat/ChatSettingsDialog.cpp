#include "agent/chat/ChatSettingsDialog.h"

#include <QAbstractButton>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFrame>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QUrl>

#include "ConfigManager.h"
#include "ToolButton.h"
#include "agent/chat/ChatStore.h"
#include "embed.h"

namespace lmms::agent::chat {
namespace {

QRect pageRect(QRect rectangle)
{ return rectangle.translated(-232, -66); }

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

QLabel* icon(QWidget* parent, std::string_view name, QRect rectangle)
{
	auto* result = new QLabel(parent);
	result->setPixmap(embed::getIconPixmap(name, rectangle.width(), rectangle.height()));
	result->setGeometry(rectangle);
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	return result;
}

gui::ToolButton* textButton(QWidget* parent, const QString& text, QRect rectangle, std::string_view iconName = {},
							int fontSize = 13)
{
	auto* result = new gui::ToolButton(parent);
	result->setGeometry(rectangle);
	result->setAccessibleName(text);
	result->setToolTip(text);
	result->setProperty("agentSettingsButton", true);
	const int textLeft = iconName.empty() ? 10 : 38;
	if (!iconName.empty())
	{
		auto* image = icon(result, iconName, QRect(12, (rectangle.height() - 18) / 2, 18, 18));
		image->setObjectName("agentSettingsButtonIcon");
	}
	auto* caption
		= label(result, text, QRect(textLeft, 0, rectangle.width() - textLeft - 8, rectangle.height()), fontSize);
	caption->setObjectName("agentSettingsButtonCaption");
	caption->setAttribute(Qt::WA_TransparentForMouseEvents);
	return result;
}

QFrame* card(QWidget* page, const char* name, QRect rectangle)
{
	auto* result = new QFrame(page);
	result->setObjectName(name);
	result->setProperty("agentSettingsCard", true);
	result->setGeometry(pageRect(rectangle));
	return result;
}

void divider(QWidget* page, QRect rectangle)
{
	auto* line = new QFrame(page);
	line->setFrameShape(QFrame::HLine);
	line->setGeometry(pageRect(rectangle));
}

class SettingsToggle : public QAbstractButton
{
public:
	explicit SettingsToggle(QWidget* parent)
		: QAbstractButton(parent)
	{
		setCheckable(true);
		setFocusPolicy(Qt::StrongFocus);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(Qt::NoPen);
		painter.setBrush(palette().color(isChecked() ? QPalette::Highlight : QPalette::Mid));
		painter.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);
		painter.setBrush(palette().color(QPalette::ButtonText));
		const int diameter = height() - 6;
		painter.drawEllipse(QRect(isChecked() ? width() - diameter - 3 : 3, 3, diameter, diameter));
	}
};

void makeCardTextTransparent(QAbstractButton* button)
{
	for (auto* child : button->findChildren<QLabel*>())
	{
		child->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
}

} // namespace

ChatSettingsDialog::ChatSettingsDialog(QWidget* parent)
	: QDialog(parent)
{
	setObjectName("agentSettingsDialog");
	setWindowTitle(QStringLiteral("Agent 设置"));
	resize(1600, 1000);
	setMinimumSize(1000, 700);
	m_scroll = new QScrollArea(this);
	m_scroll->setObjectName("agentSettingsScroll");
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_pages = new QStackedWidget;
	// Preserve the measured reference layout on small screens; native scrollbars
	// expose the content instead of compressing or overlapping controls.
	m_pages->setFixedSize(1368, 874);
	m_scroll->setWidget(m_pages);
	buildGlobalPage();
	buildApprovalPage();
	buildChrome();
	selectPage(0);
	updateApprovalLabels();
}

void ChatSettingsDialog::buildChrome()
{
	m_header = new QWidget(this);
	m_header->setObjectName("agentSettingsHeader");
	icon(m_header, "agent_chat_agent", QRect(24, 23, 23, 23));
	label(m_header, QStringLiteral("Agent 设置"), QRect(60, 17, 240, 33), 21, true);
	auto* close = new gui::ToolButton(m_header);
	close->setGeometry(1548, 13, 40, 40);
	close->setIcon(embed::getIconPixmap("agent_chat_close", 20, 20));
	close->setIconSize(QSize(20, 20));
	close->setObjectName("agentSettingsClose");
	close->setAccessibleName(QStringLiteral("关闭设置"));
	connect(close, &QToolButton::clicked, this, &QDialog::reject);
	m_navigation = new QWidget(this);
	m_navigation->setObjectName("agentSettingsNavigation");
	label(m_navigation, QStringLiteral("工作区"), QRect(24, 28, 100, 16), 10);
	const QStringList captions{QStringLiteral("API 与模型"), QStringLiteral("生成参数"), QStringLiteral("全局设置"),
							   QStringLiteral("批准模式"), QStringLiteral("素材下载")};
	const std::string_view icons[]{"agent_chat_key", "agent_chat_sliders", "agent_chat_open", "agent_chat_shield",
								   "agent_chat_download"};
	for (int index = 0; index < captions.size(); ++index)
	{
		auto* button = textButton(m_navigation, captions[index], QRect(12, 66 + index * 54, 208, 44), icons[index], 14);
		button->findChild<QLabel*>("agentSettingsButtonIcon")->setPixmap(embed::getIconPixmap(icons[index], 19, 19));
		button->findChild<QLabel*>("agentSettingsButtonIcon")->setGeometry(16, 12, 19, 19);
		button->findChild<QLabel*>("agentSettingsButtonCaption")->setGeometry(48, 10, 152, 22);
		button->setCheckable(true);
		button->setAutoExclusive(true);
		button->setObjectName(QStringLiteral("agentSettingsTab%1").arg(index));
		if (index == 2 || index == 3)
		{
			connect(button, &QToolButton::clicked, this, [this, index] { selectPage(index - 2); });
		}
		else
		{
			button->setEnabled(false);
			button->setToolTip(index == 4 ? QStringLiteral("M4 接入") : QStringLiteral("M2 接入"));
		}
	}
	m_footer = new QWidget(this);
	m_footer->setObjectName("agentSettingsFooter");
	auto* back = textButton(m_footer, QStringLiteral("返回聊天"), QRect(20, 12, 126, 35), "agent_chat_left");
	connect(back, &QToolButton::clicked, this, &QDialog::reject);
	label(m_footer, QStringLiteral("仅保存本地设置；不会发送测试请求"), QRect(264, 14, 400, 18), 11);
	m_buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, m_footer);
	m_buttons->setObjectName("agentSettingsButtons");
	// The native buttons retain QDialogButtonBox roles and keyboard behavior.
	// Position them to the two measured rectangles instead of platform spacing.
	m_buttons->layout()->setEnabled(false);
	m_buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
	m_buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存设置"));
	m_buttons->button(QDialogButtonBox::Cancel)->setGeometry(0, 0, 104, 35);
	m_buttons->button(QDialogButtonBox::Save)->setGeometry(118, 0, 130, 35);
	connect(m_buttons, &QDialogButtonBox::accepted, this, &ChatSettingsDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ChatSettingsDialog::buildGlobalPage()
{
	auto* page = new QWidget;
	page->setObjectName("agentGlobalSettingsPage");
	m_pages->addWidget(page);
	label(page, QStringLiteral("全局设置"), pageRect(QRect(280, 101, 350, 39)), 25, true);
	label(page, QStringLiteral("从外置文件读取 System Prompt；管理联网搜索与素材下载能力。"),
		  pageRect(QRect(280, 145, 1190, 21)));
	card(page, "agentPromptCard", QRect(280, 208, 850, 624));
	icon(page, "agent_chat_open", pageRect(QRect(305, 233, 24, 24)));
	label(page, QStringLiteral("System Prompt 文件"), pageRect(QRect(344, 230, 400, 28)), 18, true);
	label(page, QStringLiteral("内容保存在文本文件中，设置页只指定路径。"), pageRect(QRect(306, 278, 790, 21)));
	label(page, QStringLiteral("文件路径"), pageRect(QRect(306, 326, 200, 19)), 12);
	m_promptPath
		= new QLineEdit(ConfigManager::inst()->value("agentchat", "promptPath", "data/system_prompt.txt"), page);
	m_promptPath->setObjectName("agentPromptPath");
	m_promptPath->setGeometry(pageRect(QRect(306, 352, 610, 43)));
	auto* browse
		= textButton(page, QStringLiteral("选择文件…"), pageRect(QRect(934, 352, 168, 43)), "agent_chat_folder");
	connect(browse, &QToolButton::clicked, this, [this] {
		const auto file
			= QFileDialog::getOpenFileName(this, QStringLiteral("选择 System Prompt"),
										   ChatStore::resolvePromptPath(m_promptPath->text()), "Text (*.txt);;All (*)");
		if (!file.isEmpty())
		{
			m_promptPath->setText(file);
		}
	});
	label(page, QStringLiteral("相对路径以 LMMS 程序目录为基准，支持选择其他 .txt 文件。"),
		  pageRect(QRect(307, 410, 790, 18)), 11);
	auto* source = label(page, QStringLiteral("默认来源"), pageRect(QRect(306, 464, 86, 26)), 11);
	source->setProperty("agentSettingsBadge", true);
	const auto updateSource = [source](const QString& path) {
		source->setText(path == "data/system_prompt.txt" ? QStringLiteral("默认来源") : QStringLiteral("自定义来源"));
	};
	connect(m_promptPath, &QLineEdit::textChanged, this, updateSource);
	updateSource(m_promptPath->text());
	label(page, QStringLiteral("UTF-8 文本  ·  外部维护"), pageRect(QRect(410, 468, 300, 19)), 12);
	auto* open = textButton(page, QStringLiteral("打开文件"), pageRect(QRect(306, 518, 136, 36)), "agent_chat_open");
	auto* reload
		= textButton(page, QStringLiteral("重新读取"), pageRect(QRect(454, 518, 136, 36)), "agent_chat_reload");
	auto* reset = textButton(page, QStringLiteral("恢复默认路径"), pageRect(QRect(602, 518, 162, 36)));
	connect(open, &QToolButton::clicked, this, [this] {
		QDesktopServices::openUrl(QUrl::fromLocalFile(ChatStore::resolvePromptPath(m_promptPath->text())));
	});
	connect(reload, &QToolButton::clicked, this, &ChatSettingsDialog::reloadPrompt);
	connect(reset, &QToolButton::clicked, this, [this] { m_promptPath->setText("data/system_prompt.txt"); });
	divider(page, QRect(306, 586, 794, 1));
	label(page, QStringLiteral("默认提示词职责"), pageRect(QRect(306, 615, 400, 22)), 14, true);
	label(page,
		  QStringLiteral("音乐创作与编辑；调用已有内部工具；遵守当前批准模式；\n核对工具结果；按请求搜索与下载素材。"),
		  pageRect(QRect(306, 655, 770, 41)));
	label(page, QStringLiteral("修改文件后重新读取，后续消息使用更新后的提示词。"), pageRect(QRect(306, 752, 780, 18)),
		  11);
	card(page, "agentNetworkCard", QRect(1162, 208, 414, 624));
	icon(page, "agent_chat_globe", pageRect(QRect(1186, 234, 25, 25)));
	label(page, QStringLiteral("联网与外部素材"), pageRect(QRect(1227, 231, 320, 27)), 17, true);
	label(page, QStringLiteral("允许联网搜索"), pageRect(QRect(1186, 294, 250, 22)), 14);
	label(page, QStringLiteral("查找资料、读取网页和搜索素材来源。"), pageRect(QRect(1186, 331, 356, 19)), 12);
	label(page, QStringLiteral("允许下载素材"), pageRect(QRect(1186, 428, 250, 22)), 14);
	label(page, QStringLiteral("按请求保存音频、MIDI 等素材到指定位置。"), pageRect(QRect(1186, 466, 356, 19)), 12);
	for (int y : {297, 431})
	{
		auto* toggle = new SettingsToggle(page);
		toggle->setGeometry(pageRect(QRect(1514, y, 38, 22)));
		toggle->setAccessibleName(y == 297 ? QStringLiteral("允许联网搜索") : QStringLiteral("允许下载素材"));
		toggle->setChecked(y != 297);
		toggle->setEnabled(false);
		toggle->setToolTip(QStringLiteral("M4 接入；联网搜索默认关闭"));
	}
	divider(page, QRect(1186, 391, 366, 1));
	divider(page, QRect(1186, 530, 366, 1));
	icon(page, "agent_chat_shield", pageRect(QRect(1186, 566, 22, 22)));
	label(page, QStringLiteral("沿用批准模式"), pageRect(QRect(1220, 564, 300, 22)), 14, true);
	label(page, QStringLiteral("请求批准：搜索和下载前先确认。\n无需批准：直接执行并显示记录。"),
		  pageRect(QRect(1186, 609, 350, 41)));
	m_globalApproval = label(page, {}, pageRect(QRect(1186, 725, 160, 26)), 11);
	m_globalApproval->setProperty("agentSettingsBadge", true);
}

void ChatSettingsDialog::buildApprovalPage()
{
	auto* page = new QWidget;
	page->setObjectName("agentApprovalSettingsPage");
	m_pages->addWidget(page);
	label(page, QStringLiteral("批准模式"), pageRect(QRect(280, 101, 350, 39)), 25, true);
	label(page, QStringLiteral("控制工程操作、联网搜索和素材下载在执行前是否需要你确认。"),
		  pageRect(QRect(280, 145, 1190, 21)));
	m_requestApproval = new gui::ToolButton(page);
	m_requestApproval->setGeometry(pageRect(QRect(280, 208, 850, 222)));
	m_requestApproval->setObjectName("agentRequestApproval");
	m_requestApproval->setProperty("agentApprovalCard", true);
	m_requestApproval->setAccessibleName(QStringLiteral("请求批准"));
	m_requestApproval->setCheckable(true);
	m_requestApproval->setAutoExclusive(true);
	icon(m_requestApproval, "agent_chat_shield", QRect(26, 26, 28, 28));
	label(m_requestApproval, QStringLiteral("请求批准"), QRect(72, 25, 300, 31), 20, true);
	auto* badge = label(m_requestApproval, QStringLiteral("默认"), QRect(213, 30, 60, 26), 11);
	badge->setProperty("agentSettingsBadge", true);
	label(m_requestApproval, QStringLiteral("工具执行前，先展示目的和影响范围，等待你批准。"), QRect(27, 87, 760, 22),
		  14);
	label(m_requestApproval,
		  QStringLiteral(
			  "工程编辑：显示将改动的轨道、片段和音符。\n联网搜索：显示查询内容。素材下载：显示来源和保存位置。"),
		  QRect(27, 134, 768, 41));
	m_approvalCheck = icon(m_requestApproval, "agent_chat_check", QRect(798, 31, 23, 23));
	m_noApproval = new gui::ToolButton(page);
	m_noApproval->setGeometry(pageRect(QRect(280, 454, 850, 191)));
	m_noApproval->setObjectName("agentNoApproval");
	m_noApproval->setProperty("agentApprovalCard", true);
	m_noApproval->setAccessibleName(QStringLiteral("无需批准"));
	m_noApproval->setCheckable(true);
	m_noApproval->setAutoExclusive(true);
	icon(m_noApproval, "agent_chat_play", QRect(26, 26, 28, 28));
	label(m_noApproval, QStringLiteral("无需批准"), QRect(72, 25, 300, 31), 20, true);
	label(m_noApproval, QStringLiteral("模型请求的工具直接执行，不再弹出批准请求。"), QRect(27, 85, 760, 22), 14);
	label(m_noApproval, QStringLiteral("执行记录持续显示，可随时停止。修改可能立即写入当前工程。"),
		  QRect(27, 127, 760, 21));
	makeCardTextTransparent(m_requestApproval);
	makeCardTextTransparent(m_noApproval);
	m_noApproval->setChecked(ConfigManager::inst()->value("agentchat", "approval", "request") == "none");
	m_requestApproval->setChecked(!m_noApproval->isChecked());
	connect(m_requestApproval, &QAbstractButton::toggled, this, &ChatSettingsDialog::updateApprovalLabels);
	connect(m_noApproval, &QAbstractButton::toggled, this, &ChatSettingsDialog::updateApprovalLabels);
	card(page, "agentCurrentApprovalCard", QRect(1162, 208, 414, 437));
	label(page, QStringLiteral("当前模式"), pageRect(QRect(1186, 232, 300, 25)), 16, true);
	m_currentApproval = label(page, {}, pageRect(QRect(1186, 281, 110, 26)), 11);
	m_currentApproval->setProperty("agentSettingsBadge", true);
	label(page, QStringLiteral("适用于"), pageRect(QRect(1186, 337, 300, 19)), 12);
	label(page, QStringLiteral("工程内部工具"), pageRect(QRect(1222, 377, 300, 21)));
	label(page, QStringLiteral("联网搜索"), pageRect(QRect(1222, 436, 300, 21)));
	label(page, QStringLiteral("外部素材下载"), pageRect(QRect(1222, 495, 300, 21)));
	icon(page, "agent_chat_midi", pageRect(QRect(1188, 379, 20, 20)));
	icon(page, "agent_chat_globe", pageRect(QRect(1188, 438, 20, 20)));
	icon(page, "agent_chat_download", pageRect(QRect(1188, 497, 20, 20)));
	card(page, "agentApprovalExplanation", QRect(280, 681, 1296, 151));
	label(page, QStringLiteral("批准会出现在聊天记录中"), pageRect(QRect(306, 707, 700, 25)), 16, true);
	label(page, QStringLiteral("可以批准本次、查看变更或拒绝。批准之前保持当前工程和下载目标不变。"),
		  pageRect(QRect(306, 750, 1200, 21)));
	label(page, QStringLiteral("聊天输入区始终显示当前模式，方便随时识别。"), pageRect(QRect(306, 791, 1000, 18)), 11);
}

void ChatSettingsDialog::selectPage(int page)
{
	m_pages->setCurrentIndex(page);
	m_navigation->findChild<QAbstractButton*>(QStringLiteral("agentSettingsTab%1").arg(page + 2))->setChecked(true);
}

void ChatSettingsDialog::updateApprovalLabels()
{
	if (!m_currentApproval)
	{
		return;
	}
	const auto text = m_noApproval->isChecked() ? QStringLiteral("无需批准") : QStringLiteral("请求批准");
	m_currentApproval->setText(text);
	m_globalApproval->setText(QStringLiteral("当前：") + text);
	m_approvalCheck->setVisible(m_requestApproval->isChecked());
}

void ChatSettingsDialog::reloadPrompt()
{
	QString error;
	const auto prompt = ChatStore::readPrompt(m_promptPath->text(), error);
	QMessageBox::information(
		this, QStringLiteral("System Prompt"),
		error.isEmpty() ? QStringLiteral("读取成功，%1 个字符；保存设置后用于后续请求。").arg(prompt.size()) : error);
}

void ChatSettingsDialog::accept()
{
	QString error;
	ChatStore::readPrompt(m_promptPath->text(), error);
	if (!error.isEmpty())
	{
		QMessageBox::warning(this, QStringLiteral("System Prompt"), error);
		return;
	}
	auto* config = ConfigManager::inst();
	config->setValue("agentchat", "promptPath", m_promptPath->text());
	config->setValue("agentchat", "approval", m_noApproval->isChecked() ? "none" : "request");
	config->saveConfigFile();
	QDialog::accept();
}

void ChatSettingsDialog::resizeEvent(QResizeEvent* event)
{
	QDialog::resizeEvent(event);
	if (!m_footer)
	{
		return;
	}
	m_header->setGeometry(0, 0, width(), 66);
	m_header->findChild<QWidget*>("agentSettingsClose")->move(width() - 52, 13);
	m_navigation->setGeometry(0, 66, 232, height() - 126);
	m_scroll->setGeometry(232, 66, width() - 232, height() - 126);
	m_footer->setGeometry(0, height() - 60, width(), 60);
	m_buttons->setGeometry(width() - 272, 12, 248, 35);
}

} // namespace lmms::agent::chat
