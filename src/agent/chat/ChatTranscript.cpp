#include "agent/chat/ChatTranscript.h"

#include <QCoreApplication>
#include <QFontMetrics>
#include <QFrame>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QResizeEvent>
#include <QScrollBar>
#include <QToolButton>

#include "ToolButton.h"
#include "embed.h"

namespace lmms::agent::chat {
namespace {

QString translated(const char* text)
{ return QCoreApplication::translate("AgentChat", text); }

QLabel* label(QWidget* parent, int pixels)
{
	auto* result = new QLabel(parent);
	result->setTextFormat(Qt::PlainText);
	auto font = result->font();
	font.setPixelSize(pixels);
	result->setFont(font);
	return result;
}

QString messageText(const QJsonObject& message)
{
	auto text = message.value("text").toString();
	if (message.value("role").toString() == "tool" && text.isEmpty())
	{
		const auto content = message.value("content");
		text = content.isString() ? content.toString()
								  : QString::fromUtf8((content.isArray() ? QJsonDocument(content.toArray())
																		 : QJsonDocument(content.toObject()))
														  .toJson(QJsonDocument::Indented));
	}
	const auto error = message.value("error").toString();
	if (!error.isEmpty())
	{
		text += "\n\n" + translated("Request failed: %1").arg(error);
	}
	if (message.value("status").toString() == "canceled")
	{
		text += "\n\n" + translated("Stopped");
	}
	return text;
}

} // namespace

class ChatApprovalCard : public QFrame
{
public:
	explicit ChatApprovalCard(QWidget* parent)
		: QFrame(parent)
	{
		setObjectName("agentApprovalCard");
		setFrameShape(QFrame::StyledPanel);
		auto* icon = label(this, 12);
		icon->setPixmap(embed::getIconPixmap("agent_chat_shield", 19, 19));
		icon->setGeometry(12, 15, 19, 19);
		m_title = label(this, 13);
		m_title->setObjectName("agentApprovalTitle");
		m_scope = label(this, 12);
		m_scope->setObjectName("agentApprovalScope");
		m_explanation = label(this, 11);
		m_explanation->setText(translated("Approve to execute the internal project tool"));
		approve = button("agentApprovalApprove", translated("Approve once"), "agent_chat_check", {12, 109, 98, 34});
		details = button("agentApprovalDetails", translated("View changes"), "agent_chat_open", {120, 109, 100, 34});
		deny = button("agentApprovalDeny", translated("Deny"), "agent_chat_close", {230, 109, 65, 34});
		hide();
	}

	void setApproval(const QJsonObject& approval)
	{
		const bool same = approval == m_data;
		m_data = approval;
		setVisible(!approval.isEmpty());
		if (!same)
		{
			approve->setEnabled(true);
			deny->setEnabled(true);
		}
		m_title->setText(translated("Awaiting approval: %1").arg(approval.value("command").toString()));
		QStringList targets;
		for (const auto& value : approval.value("targets").toArray())
		{
			const auto target = value.toObject();
			const auto name = target.value("name").toString();
			targets.append(target.value("path").toString() + (name.isEmpty() ? QString{} : " · " + name));
		}
		m_scopeText = targets.join(" · ");
		m_scope->setToolTip("<qt>" + m_scopeText.toHtmlEscaped() + "</qt>");
		m_title->setToolTip("<qt>" + approval.value("purpose").toString().toHtmlEscaped() + "</qt>");
	}

	QJsonObject approval() const
	{ return m_data; }

	void arrange(int width)
	{
		setFixedSize(width, 157);
		m_title->setGeometry(39, 13, qMax(1, width - 53), 21);
		m_scope->setGeometry(14, 48, qMax(1, width - 28), 18);
		m_scope->setText(QFontMetrics(m_scope->font()).elidedText(m_scopeText, Qt::ElideMiddle, m_scope->width()));
		m_explanation->setGeometry(14, 73, qMax(1, width - 28), 16);
	}

	QToolButton* approve = nullptr;
	QToolButton* details = nullptr;
	QToolButton* deny = nullptr;

private:
	QToolButton* button(const char* name, const QString& text, std::string_view icon, const QRect& geometry)
	{
		auto* result = new gui::ToolButton(this);
		result->setObjectName(QString::fromLatin1(name));
		result->setText(text);
		result->setToolTip(text);
		result->setAccessibleName(text);
		result->setIcon(embed::getIconPixmap(icon, 16, 16));
		result->setIconSize({16, 16});
		result->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		result->setProperty("agentChatControl", true);
		result->setGeometry(geometry);
		return result;
	}

	QJsonObject m_data;
	QString m_scopeText;
	QLabel* m_title = nullptr;
	QLabel* m_scope = nullptr;
	QLabel* m_explanation = nullptr;
};

class ChatMessage : public QFrame
{
public:
	ChatMessage(const QJsonObject& message, QWidget* parent)
		: QFrame(parent)
		, m_user(message.value("role").toString() == "user")
	{
		const auto id = message.value("id").toString();
		setObjectName("agentMessage_" + id);
		setProperty("agentUserBubble", m_user);
		m_body = label(this, 13);
		m_body->setObjectName("agentMessageText_" + id);
		m_body->setWordWrap(true);
		m_body->setAlignment(Qt::AlignLeft | Qt::AlignTop);
		m_body->setTextInteractionFlags(Qt::TextSelectableByMouse);
		if (!m_user)
		{
			m_icon = label(this, 12);
			m_icon->setPixmap(embed::getIconPixmap("agent_chat_agent", 22, 22));
			m_icon->setGeometry(0, 0, 22, 22);
			m_title = label(this, 12);
			m_title->setText(message.value("role").toString() == "tool" ? translated("Tool result")
																		: QStringLiteral("Agent"));
			m_title->setGeometry(32, 0, 66, 19);
			m_summary = label(this, 11);
			m_summary->setText(translated("Thought · Summary"));
			m_summary->setGeometry(100, 1, 160, 16);
			m_summary->setObjectName("agentMessageSummary_" + id);
		}
		for (const auto& value : message.value("attachments").toArray())
		{
			const auto attachment = value.toObject();
			auto* row = new QWidget(this);
			row->setObjectName(QStringLiteral("agentMessageAttachment_%1_%2").arg(id).arg(m_attachments.size()));
			auto* icon = label(row, 13);
			icon->setPixmap(embed::getIconPixmap(
				attachment.value("type").toString() == "midi" ? "agent_chat_midi" : "agent_chat_attach", 16, 16));
			icon->setGeometry(0, 3, 16, 16);
			auto* name = label(row, 13);
			name->setText(attachment.value("name").toString());
			name->setGeometry(24, 0, 100, 18);
			name->setToolTip("<qt>"
							 + (attachment.value("name").toString() + " · " + attachment.value("mimeType").toString())
								   .toHtmlEscaped()
							 + "</qt>");
			auto* size = label(row, 11);
			size->setText(QLocale().formattedDataSize(attachment.value("size").toInteger()));
			size->setGeometry(129, 1, 130, 16);
			m_attachments.append(row);
		}
		setMessage(message);
	}

	void setMessage(const QJsonObject& message)
	{
		m_message = message;
		m_body->setText(messageText(message));
		if (m_summary)
		{
			const auto summary = message.value("summary").toString();
			m_summary->setVisible(!summary.isEmpty());
			m_summary->setToolTip("<qt>" + summary.toHtmlEscaped().replace('\n', "<br>") + "</qt>");
		}
	}

	QString id() const
	{ return m_message.value("id").toString(); }

	QString plainText() const
	{
		auto text = m_body->text();
		for (const auto& value : m_message.value("attachments").toArray())
		{
			text += "\n" + value.toObject().value("name").toString();
		}
		return text;
	}

	void arrange(int width)
	{
		const int left = m_user ? 14 : 0;
		const int top = m_user ? 13 : 37;
		const int bodyWidth = qMax(1, width - 2 * left);
		const int bodyHeight = qMax(21,
									QFontMetrics(m_body->font())
										.boundingRect(QRect(0, 0, bodyWidth, 1000000), Qt::TextWordWrap, m_body->text())
										.height());
		m_body->setGeometry(left, top, bodyWidth, bodyHeight);
		int height = top + bodyHeight + (m_user ? 13 : 0);
		if (!m_attachments.isEmpty())
		{
			int y = qMax(74, top + bodyHeight + 16);
			for (auto* row : m_attachments)
			{
				row->setGeometry(14, y, qMax(1, width - 28), 21);
				y += 26;
			}
			height = y - 26 + 39;
		}
		setFixedSize(width, height);
	}

private:
	QJsonObject m_message;
	bool m_user;
	QLabel* m_body = nullptr;
	QLabel* m_icon = nullptr;
	QLabel* m_title = nullptr;
	QLabel* m_summary = nullptr;
	QVector<QWidget*> m_attachments;
};

ChatTranscript::ChatTranscript(QWidget* parent)
	: QScrollArea(parent)
{
	setFrameShape(QFrame::NoFrame);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_contents = new QWidget;
	m_contents->setObjectName("agentTranscriptContents");
	setWidget(m_contents);
	m_contents->setAutoFillBackground(false);
	m_empty = label(m_contents, 13);
	m_empty->setWordWrap(true);
	m_empty->setText(translated("Start a new music conversation."));
	m_approval = new ChatApprovalCard(m_contents);
	for (const auto accepted : {true, false})
	{
		connect(accepted ? m_approval->approve : m_approval->deny, &QToolButton::clicked, this, [this, accepted] {
			const auto approval = m_approval->approval();
			m_approval->approve->setEnabled(false);
			m_approval->deny->setEnabled(false);
			emit approvalDecided(approval, accepted);
		});
	}
	connect(m_approval->details, &QToolButton::clicked, this,
			[this] { emit approvalDetailsRequested(m_approval->approval()); });
}

void ChatTranscript::setApproval(const QJsonObject& approval)
{
	auto* scroll = verticalScrollBar();
	const int position = scroll->value();
	const bool follow = position >= scroll->maximum() - 2;
	m_approval->setApproval(approval);
	m_empty->setVisible(m_rows.isEmpty() && approval.isEmpty());
	arrangeMessages();
	scroll->setValue(follow ? scroll->maximum() : position);
}

void ChatTranscript::setMessages(const QJsonArray& messages)
{
	qDeleteAll(m_rows);
	m_rows.clear();
	for (const auto& message : messages)
	{
		m_rows.append(new ChatMessage(message.toObject(), m_contents));
		m_rows.last()->show();
	}
	m_empty->setVisible(messages.isEmpty() && m_approval->approval().isEmpty());
	arrangeMessages();
}

void ChatTranscript::updateMessage(const QJsonObject& message)
{
	auto* scroll = verticalScrollBar();
	const int position = scroll->value();
	const bool follow = position >= scroll->maximum() - 2;
	for (auto* row : m_rows)
	{
		if (row->id() == message.value("id").toString())
		{
			row->setMessage(message);
			arrangeMessages();
			scroll->setValue(follow ? scroll->maximum() : position);
			return;
		}
	}
}

QString ChatTranscript::toPlainText() const
{
	QStringList messages;
	for (const auto* row : m_rows)
	{
		messages.append(row->plainText());
	}
	return messages.join("\n\n");
}

void ChatTranscript::resizeEvent(QResizeEvent* event)
{
	QScrollArea::resizeEvent(event);
	arrangeMessages();
}

void ChatTranscript::arrangeMessages()
{
	if (!m_empty)
	{
		return;
	}
	const int width = qMax(1, viewport()->width());
	int y = 0;
	for (auto* row : m_rows)
	{
		row->arrange(width);
		row->move(0, y);
		y += row->height() + 27;
	}
	if (!m_approval->approval().isEmpty())
	{
		// Penpot 01: a 157px card follows the preceding 50px result by 16px.
		if (y > 0)
		{
			y -= 11;
		}
		m_approval->arrange(width);
		m_approval->move(0, y);
		y += m_approval->height() + 27;
	}
	m_contents->setFixedSize(width, qMax(viewport()->height(), y > 0 ? y - 27 : 0));
	m_empty->setGeometry(0, 0, width, 42);
}

} // namespace lmms::agent::chat
