#include "agent/chat/ChatSettingsDialog.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
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
#include <QStandardPaths>
#include <QUrl>

#include "ConfigManager.h"
#include "ToolButton.h"
#include "agent/chat/ChatApiSettingsPage.h"
#include "agent/chat/ChatGenerationPage.h"
#include "agent/chat/ChatRequest.h"
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

// Navigation order: API, generation, global, approval, downloads.
// Stacked page order: global, approval, API, generation, downloads.
int pageForTab(int tab)
{
	const int pages[]{2, 3, 0, 1, 4};
	return pages[tab];
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
	, m_connectionStore(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("agent"))
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
	m_apiPage
		= new ChatApiSettingsPage(m_connectionStore, nullptr, ConfigManager::inst()->value("agentchat", "connection"));
	m_pages->addWidget(m_apiPage);
	m_generationPage = new ChatGenerationPage;
	m_pages->addWidget(m_generationPage);
	buildDownloadPage();
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
		connect(button, &QToolButton::clicked, this, [this, index] { selectPage(pageForTab(index)); });
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
	buildSearchSettings(page);

	m_downloadSettings = ChatDownloadSettings::load();
	label(page, QStringLiteral("允许下载素材"), pageRect(QRect(1186, 548, 250, 22)), 14);
	label(page, QStringLiteral("按请求保存音频、MIDI 等素材到指定位置。"), pageRect(QRect(1186, 586, 356, 19)), 12);
	auto* download = new SettingsToggle(page);
	download->setObjectName("agentAllowDownloads");
	download->setGeometry(pageRect(QRect(1514, 551, 38, 22)));
	download->setAccessibleName(QStringLiteral("允许下载素材"));
	download->setChecked(m_downloadSettings.enabled);
	m_allowDownloads = download;
	divider(page, QRect(1186, 528, 366, 1));
	divider(page, QRect(1186, 626, 366, 1));
	icon(page, "agent_chat_shield", pageRect(QRect(1186, 646, 22, 22)));
	label(page, QStringLiteral("沿用批准模式"), pageRect(QRect(1220, 644, 300, 22)), 14, true);
	label(page, QStringLiteral("请求批准：搜索和下载前先确认。\n无需批准：直接执行并显示记录。"),
		pageRect(QRect(1186, 689, 350, 41)));
	m_globalApproval = label(page, {}, pageRect(QRect(1186, 765, 160, 26)), 11);
	m_globalApproval->setProperty("agentSettingsBadge", true);
}

void ChatSettingsDialog::buildSearchSettings(QWidget* page)
{
	m_search = ChatSearchSettings::load();

	label(page, QStringLiteral("允许联网搜索"), pageRect(QRect(1186, 294, 250, 22)), 14);
	label(page, QStringLiteral("查找资料、读取网页和搜索素材来源。"), pageRect(QRect(1186, 331, 356, 19)), 12);
	auto* toggle = new SettingsToggle(page);
	toggle->setObjectName("agentWebSearch");
	toggle->setGeometry(pageRect(QRect(1514, 297, 38, 22)));
	toggle->setAccessibleName(QStringLiteral("允许联网搜索"));
	toggle->setChecked(m_search.enabled);
	m_webSearch = toggle;

	auto* sourceLabel = label(page, QStringLiteral("搜索来源"), pageRect(QRect(1186, 366, 120, 19)), 12);
	const QString names[3] = {QStringLiteral("SearXNG"), QStringLiteral("Brave"), QStringLiteral("模型自带")};
	const char* objectNames[3] = {"agentSearchSearxng", "agentSearchBrave", "agentSearchModel"};
	for (int index = 0; index < 3; ++index)
	{
		auto* segment = new gui::ToolButton(page);
		segment->setObjectName(QString::fromLatin1(objectNames[index]));
		segment->setText(names[index]);
		segment->setAccessibleName(names[index]);
		segment->setCheckable(true);
		segment->setAutoExclusive(true);
		segment->setProperty("agentSearchSegment", true);
		segment->setGeometry(pageRect(QRect(1186 + index * 124, 392, 118, 32)));
		const auto backend = static_cast<ChatSearchBackend>(index);
		connect(segment, &QToolButton::clicked, this, [this, backend] { selectSearchBackend(backend); });
		m_searchSegments[index] = segment;
	}

	m_searchEndpoint = new QLineEdit(page);
	m_searchEndpoint->setObjectName("agentSearchEndpoint");
	m_searchEndpoint->setGeometry(pageRect(QRect(1186, 436, 366, 36)));
	m_searchSecret = new QLineEdit(page);
	m_searchSecret->setObjectName("agentSearchSecret");
	m_searchSecret->setEchoMode(QLineEdit::Password);
	m_searchSecret->setGeometry(pageRect(QRect(1186, 480, 366, 36)));

	// The endpoint field edits the URL of whichever backend is selected.
	connect(m_searchEndpoint, &QLineEdit::textEdited, this, [this](const QString& text) {
		if (m_search.backend == ChatSearchBackend::Brave) { m_search.braveUrl = text; }
		else if (m_search.backend == ChatSearchBackend::SearXNG) { m_search.searxngUrl = text; }
	});
	const auto updateEnabled = [this, sourceLabel](bool enabled) {
		sourceLabel->setEnabled(enabled);
		for (auto* segment : m_searchSegments)
		{
			segment->setEnabled(enabled);
		}
		if (m_apiPage) { updateModelSearchAvailability(); }
		const bool external = enabled && m_search.backend != ChatSearchBackend::Model;
		m_searchEndpoint->setEnabled(external);
		m_searchSecret->setEnabled(external);
	};
	connect(toggle, &QAbstractButton::toggled, this, [this, updateEnabled](bool enabled) {
		m_search.enabled = enabled;
		updateEnabled(enabled);
	});
	selectSearchBackend(m_search.backend);
	updateEnabled(m_search.enabled);
}

void ChatSettingsDialog::selectSearchBackend(ChatSearchBackend backend)
{
	// A secret typed for one backend is cleared on switching, so it can never
	// be saved under another backend's credential.
	if (backend != m_search.backend) { m_searchSecret->clear(); }
	m_search.backend = backend;
	m_searchSegments[static_cast<int>(backend)]->setChecked(true);

	QString error;
	switch (backend)
	{
	case ChatSearchBackend::SearXNG: {
		const bool stored = !m_connectionStore.readKey(ChatSearchSettings::searxngAuthId(), error).isEmpty();
		m_searchEndpoint->setText(m_search.searxngUrl);
		m_searchEndpoint->setPlaceholderText(QStringLiteral("SearXNG 实例 URL，如 http://192.168.1.20:8080"));
		m_searchSecret->setPlaceholderText(stored ? QStringLiteral("已保存认证信息；留空保持不变")
												  : QStringLiteral("认证信息（实例需要时填写，可留空）"));
		break;
	}
	case ChatSearchBackend::Brave: {
		const bool stored = !m_connectionStore.readKey(ChatSearchSettings::braveKeyId(), error).isEmpty();
		m_searchEndpoint->setText(m_search.braveUrl);
		m_searchEndpoint->setPlaceholderText(QStringLiteral("Brave Search API 地址"));
		m_searchSecret->setPlaceholderText(
			stored ? QStringLiteral("已保存 API Key；留空保持不变") : QStringLiteral("Brave Search API Key"));
		break;
	}
	case ChatSearchBackend::Model:
		m_searchEndpoint->clear();
		m_searchEndpoint->setPlaceholderText(QStringLiteral("由当前模型厂商执行，发送前单独批准"));
		m_searchSecret->setPlaceholderText({});
		break;
	}
	const bool external = m_search.enabled && backend != ChatSearchBackend::Model;
	m_searchEndpoint->setEnabled(external);
	m_searchSecret->setEnabled(external);
}

void ChatSettingsDialog::updateModelSearchAvailability()
{
	auto* segment = m_searchSegments[static_cast<int>(ChatSearchBackend::Model)];
	const bool supported = supportsHostedWebSearch(m_apiPage->connectionDraft());
	// An unsupported model keeps a saved choice visible but cannot newly select it.
	segment->setEnabled(m_search.enabled && (supported || segment->isChecked()));
	segment->setToolTip(
		supported ? QString{} : QStringLiteral("当前模型未声明支持模型自带搜索（capabilities.hostedWebSearch）"));
}

bool ChatSettingsDialog::saveSearchSettings(QString& error)
{
	error.clear();
	if (m_search.enabled && m_search.backend == ChatSearchBackend::Model
		&& !supportsHostedWebSearch(m_apiPage->connectionDraft()))
	{
		error = QStringLiteral("当前模型不支持模型自带搜索，请选择 SearXNG 或 Brave，或换用支持的模型。");
		return false;
	}
	const auto secret = m_searchSecret->text();
	if (m_search.enabled && m_search.backend != ChatSearchBackend::Model)
	{
		const bool searxng = m_search.backend == ChatSearchBackend::SearXNG;
		const auto url = QUrl(searxng ? m_search.searxngUrl : m_search.braveUrl);
		// Local-network instances are a supported setup: only the scheme and host are checked.
		if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "http" && url.scheme() != "https"))
		{
			error = searxng ? QStringLiteral("请填写有效的 SearXNG 实例 URL（http 或 https）。")
							: QStringLiteral("请填写有效的 Brave Search API 地址（http 或 https）。");
			return false;
		}
		if (!searxng && secret.isEmpty())
		{
			QString readError;
			if (m_connectionStore.readKey(ChatSearchSettings::braveKeyId(), readError).isEmpty())
			{
				error = QStringLiteral("使用 Brave 搜索需要填写 API Key。");
				return false;
			}
		}
	}
	if (!secret.isEmpty())
	{
		const auto id = m_search.backend == ChatSearchBackend::Brave ? ChatSearchSettings::braveKeyId()
																	 : ChatSearchSettings::searxngAuthId();
		if (!m_connectionStore.writeKey(id, secret, error)) { return false; }
	}
	m_search.save();
	return true;
}

void ChatSettingsDialog::buildDownloadPage()
{
	// Penpot 07: tool locations, download flow and tool verification.
	auto* page = new QWidget;
	page->setObjectName("agentDownloadSettingsPage");
	m_pages->addWidget(page);
	label(page, QStringLiteral("素材下载"), pageRect(QRect(280, 101, 350, 39)), 25, true);
	label(
		page, QStringLiteral("通过下载工具获取外部素材，压缩包下载完成后解压。"), pageRect(QRect(280, 145, 1190, 21)));

	card(page, "agentToolCard", QRect(280, 208, 850, 625));
	label(page, QStringLiteral("工具位置"), pageRect(QRect(306, 232, 300, 28)), 18, true);
	auto* portable = label(page, QStringLiteral("优先便携版"), pageRect(QRect(968, 233, 130, 26)), 11);
	portable->setProperty("agentSettingsBadge", true);
	label(page, QStringLiteral("工具按文件夹放在 lmms.exe 旁的 tools 目录，路径可调整。"),
		pageRect(QRect(306, 277, 780, 21)));

	const auto pathRow = [this, page](const QString& caption, int y, const QString& value, const char* name) {
		label(page, caption, pageRect(QRect(306, y, 300, 19)), 12);
		auto* path = new QLineEdit(value, page);
		path->setObjectName(QString::fromLatin1(name));
		path->setGeometry(pageRect(QRect(306, y + 26, 622, 43)));
		auto* browse
			= textButton(page, QStringLiteral("选择文件…"), pageRect(QRect(946, y + 26, 156, 43)), "agent_chat_folder");
		connect(browse, &QToolButton::clicked, this, [this, path] {
			const auto file = QFileDialog::getOpenFileName(
				this, QStringLiteral("选择工具"), ChatPortableTools::resolve(path->text()), "Programs (*.exe)");
			if (!file.isEmpty()) { path->setText(file); }
		});
		return path;
	};
	m_aria2Path = pathRow(QStringLiteral("aria2 下载工具"), 326, m_downloadSettings.aria2Path, "agentAria2Path");
	m_sevenZipPath
		= pathRow(QStringLiteral("压缩包解压工具"), 429, m_downloadSettings.sevenZipPath, "agentSevenZipPath");
	label(page, QStringLiteral("7zip 所需的配套文件保留在 tools/7zip/ 内。"), pageRect(QRect(306, 511, 600, 18)), 11);
	divider(page, QRect(306, 560, 796, 1));

	label(page, QStringLiteral("素材保存位置"), pageRect(QRect(306, 589, 300, 24)), 16, true);
	// The save location is chosen per request; this read-only field explains that.
	auto* target = new QLineEdit(QStringLiteral("由当前下载请求指定，可选择工程素材目录"), page);
	target->setObjectName("agentDownloadTarget");
	target->setReadOnly(true);
	target->setGeometry(pageRect(QRect(306, 632, 796, 44)));
	icon(page, "agent_chat_folder", pageRect(QRect(1067, 645, 18, 18)));

	auto* extract = new QCheckBox(QStringLiteral("压缩包下载完成后解压"), page);
	extract->setObjectName("agentExtractArchives");
	extract->setGeometry(pageRect(QRect(306, 712, 400, 24)));
	extract->setChecked(m_downloadSettings.extractArchives);
	m_extractArchives = extract;
	label(page, QStringLiteral("下载前同时展示文件保存位置与解压目录。"), pageRect(QRect(339, 746, 600, 18)), 11);

	card(page, "agentDownloadFlowCard", QRect(1162, 208, 414, 625));
	label(page, QStringLiteral("下载流程"), pageRect(QRect(1186, 234, 300, 27)), 17, true);
	const struct
	{
		std::string_view icon;
		const char* title;
		const char* detail;
		int y;
	} steps[]{{"agent_chat_open", "确认来源与目标", "显示 URL、文件名和保存目录", 295},
		{"agent_chat_download", "下载素材", "aria2 进度、速度和剩余时间", 404},
		{"agent_chat_archive", "识别压缩包并解压", "显示解压位置和处理结果", 513},
		{"agent_chat_check", "加入当前会话", "可选择导入工程或保留文件", 622}};
	for (const auto& step : steps)
	{
		icon(page, step.icon, pageRect(QRect(1188, step.y + 4, 23, 23)));
		label(page, QString::fromUtf8(step.title), pageRect(QRect(1226, step.y, 310, 22)), 14);
		label(page, QString::fromUtf8(step.detail), pageRect(QRect(1226, step.y + 34, 310, 18)), 12);
	}

	auto* check = textButton(page, QStringLiteral("检查工具"), pageRect(QRect(280, 868, 140, 36)), "agent_chat_reload");
	check->setObjectName("agentCheckTools");
	m_toolStatus = label(page, QStringLiteral("尚未检查"), pageRect(QRect(438, 878, 900, 18)), 12);
	m_toolStatus->setObjectName("agentToolStatus");
	connect(check, &QToolButton::clicked, this, [this] {
		auto settings = m_downloadSettings;
		settings.aria2Path = m_aria2Path->text();
		settings.sevenZipPath = m_sevenZipPath->text();
		QString report;
		const bool ok = ChatPortableTools::verify(settings, report);
		m_toolStatus->setText((ok ? QStringLiteral("工具可用：") : QStringLiteral("工具不可用：")) + report);
	});
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
	if (m_pages->currentWidget() == m_generationPage)
	{
		m_apiPage->setGenerationDraft(m_generationPage->connection());
	}
	if (page == 3)
	{
		m_generationPage->setConnection(m_apiPage->connectionDraft(), m_requestApproval->isChecked());
	}
	if (page == 0) { updateModelSearchAvailability(); }
	m_pages->setCurrentIndex(page);
	for (int tab = 0; tab < 5; ++tab)
	{
		if (pageForTab(tab) == page)
		{
			m_navigation->findChild<QAbstractButton*>(QStringLiteral("agentSettingsTab%1").arg(tab))->setChecked(true);
		}
	}
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
	if (m_pages->currentWidget() == m_generationPage)
	{
		m_apiPage->setGenerationDraft(m_generationPage->connection());
	}
	QString error;
	ChatStore::readPrompt(m_promptPath->text(), error);
	if (!error.isEmpty())
	{
		QMessageBox::warning(this, QStringLiteral("System Prompt"), error);
		return;
	}
	if (!m_apiPage->save(error))
	{
		QMessageBox::warning(this, QStringLiteral("API"), error);
		return;
	}
	if (!saveSearchSettings(error))
	{
		QMessageBox::warning(this, QStringLiteral("联网搜索"), error);
		return;
	}
	auto* config = ConfigManager::inst();
	config->setValue("agentchat", "connection", m_apiPage->selectedConnectionId());
	config->setValue("agentchat", "promptPath", m_promptPath->text());
	config->setValue("agentchat", "approval", m_noApproval->isChecked() ? "none" : "request");
	m_downloadSettings.enabled = m_allowDownloads->isChecked();
	m_downloadSettings.aria2Path = m_aria2Path->text();
	m_downloadSettings.sevenZipPath = m_sevenZipPath->text();
	m_downloadSettings.extractArchives = m_extractArchives->isChecked();
	m_downloadSettings.save();
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
