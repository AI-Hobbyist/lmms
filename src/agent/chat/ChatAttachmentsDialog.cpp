#include "agent/chat/ChatAttachmentsDialog.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFrame>
#include <QLabel>
#include <QLocale>
#include <QMimeData>
#include <QScrollArea>
#include <QToolButton>

#include "ToolButton.h"
#include "embed.h"

namespace lmms::agent::chat {
namespace {

QLabel* label(QWidget* parent, const QString& text, QRect geometry, int pixels = 13)
{
	auto* result = new QLabel(text, parent);
	result->setTextFormat(Qt::PlainText);
	result->setGeometry(geometry);
	auto font = result->font();
	font.setPixelSize(pixels);
	result->setFont(font);
	return result;
}

gui::ToolButton* button(QWidget* parent, const QString& text, QRect geometry, std::string_view icon)
{
	auto* result = new gui::ToolButton(parent);
	result->setGeometry(geometry);
	result->setText(text);
	result->setAccessibleName(text);
	result->setToolTip(text);
	result->setIcon(embed::getIconPixmap(icon, 18, 18));
	result->setProperty("agentChatControl", true);
	return result;
}

} // namespace

ChatAttachmentsDialog::ChatAttachmentsDialog(ChatConnection connection, QVector<ChatAttachment> attachments,
											 QWidget* parent)
	: QDialog(parent)
	, m_connection(std::move(connection))
	, m_attachments(std::move(attachments))
{
	setObjectName("agentAttachmentsDialog");
	setWindowTitle(tr("Add attachments"));
	setFixedSize(848, 471);
	setAcceptDrops(true);
	auto* background = new QFrame(this);
	background->setProperty("agentSettingsCard", true);
	background->setGeometry(rect());
	label(this, tr("Add attachments"), {24, 21, 700, 28}, 18);
	auto* close = button(this, tr("Close"), {800, 18, 32, 32}, "agent_chat_close");
	close->setObjectName("agentAttachmentsClose");
	connect(close, &QToolButton::clicked, this, &QDialog::reject);
	auto* drop = new QFrame(this);
	drop->setObjectName("agentAttachmentDropArea");
	drop->setProperty("agentSettingsCard", true);
	drop->setGeometry(24, 73, 800, 121);
	auto* image = label(this, {}, {408, 94, 29, 29});
	image->setPixmap(embed::getIconPixmap("agent_chat_attach", 29, 29));
	label(this, tr("Drop files or choose files"), {253, 142, 340, 22}, 14);
	auto* browse = button(this, tr("Choose files…"), {618, 118, 172, 36}, "agent_chat_folder");
	browse->setObjectName("agentAttachmentBrowse");
	browse->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	connect(browse, &QToolButton::clicked, this, [this] {
		addFiles(QFileDialog::getOpenFileNames(this, tr("Choose attachments"), {}, tr("All files (*)")));
	});
	label(this, tr("Attachment name"), {44, 213, 400, 18}, 11);
	label(this, tr("Status"), {517, 213, 280, 18}, 11);
	m_rows = new QScrollArea(this);
	m_rows->setObjectName("agentAttachmentRows");
	m_rows->setFrameShape(QFrame::NoFrame);
	m_rows->setGeometry(24, 238, 800, 174);
	m_rows->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_notice = label(this, tr("Selected files are sent only when you send a message; unsupported types are reported."),
					 {24, 433, 800, 18}, 11);
	refresh();
}

QVector<ChatAttachment> ChatAttachmentsDialog::attachments() const
{ return m_attachments; }

void ChatAttachmentsDialog::addFiles(const QStringList& paths)
{
	for (const auto& path : paths)
	{
		ChatAttachment attachment;
		QString error;
		if (!inspectChatAttachment(path, attachment, error))
		{
			m_notice->setText(error);
			m_notice->setToolTip(error);
			continue;
		}
		bool duplicate = false;
		for (const auto& current : m_attachments)
		{
			duplicate = duplicate || current.path.compare(attachment.path, Qt::CaseInsensitive) == 0;
		}
		if (!duplicate)
		{
			m_attachments.append(attachment);
		}
	}
	refresh();
}

void ChatAttachmentsDialog::refresh()
{
	auto* rows = new QWidget;
	rows->setFixedSize(800, qMax(174, int(m_attachments.size()) * 58));
	for (int index = 0; index < m_attachments.size(); ++index)
	{
		const auto attachment = m_attachments[index];
		const int y = index * 58;
		auto* image = label(rows, {}, {20, y + 8, 19, 19});
		image->setPixmap(
			embed::getIconPixmap(attachment.type == "midi" ? "agent_chat_midi" : "agent_chat_attach", 19, 19));
		auto* name = label(rows, attachment.name, {52, y + 6, 250, 21});
		name->setToolTip(attachment.path);
		name->setObjectName(QStringLiteral("agentAttachmentName_%1").arg(index));
		label(rows, attachment.mimeType + " · " + QLocale().formattedDataSize(attachment.size), {318, y + 8, 174, 18},
			  11);
		QString error;
		const bool supported = supportsChatAttachment(m_connection, attachment, error);
		auto* status = label(rows,
							 supported ? (attachment.type == "midi" ? tr("Local reference") : tr("Ready to send"))
									   : tr("Not supported by this model"),
							 {492, y + 7, 260, 19}, 12);
		status->setObjectName(QStringLiteral("agentAttachmentStatus_%1").arg(index));
		status->setToolTip(error);
		auto* remove = button(rows, tr("Remove attachment"), {759, y + 1, 30, 30}, "agent_chat_close");
		remove->setIconSize({14, 14});
		remove->setObjectName(QStringLiteral("agentAttachmentRemove_%1").arg(index));
		connect(remove, &QToolButton::clicked, this, [this, index] {
			m_attachments.removeAt(index);
			refresh();
		});
	}
	if (auto* previous = m_rows->takeWidget())
	{
		previous->deleteLater();
	}
	m_rows->setWidget(rows);
}

void ChatAttachmentsDialog::dragEnterEvent(QDragEnterEvent* event)
{
	if (event->mimeData()->hasUrls())
	{
		for (const auto& url : event->mimeData()->urls())
		{
			if (url.isLocalFile())
			{
				event->acceptProposedAction();
				return;
			}
		}
	}
}

void ChatAttachmentsDialog::dropEvent(QDropEvent* event)
{
	QStringList paths;
	for (const auto& url : event->mimeData()->urls())
	{
		if (url.isLocalFile())
		{
			paths.append(url.toLocalFile());
		}
	}
	addFiles(paths);
	event->acceptProposedAction();
}

} // namespace lmms::agent::chat
