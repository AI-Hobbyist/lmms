#include "agent/chat/ChatTranscript.h"

#include <QCoreApplication>
#include <QFontMetrics>
#include <QFrame>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QResizeEvent>
#include <QScrollBar>

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
	m_empty->setVisible(messages.isEmpty());
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
	m_contents->setFixedSize(width, qMax(viewport()->height(), y > 0 ? y - 27 : 0));
	m_empty->setGeometry(0, 0, width, 42);
}

} // namespace lmms::agent::chat
