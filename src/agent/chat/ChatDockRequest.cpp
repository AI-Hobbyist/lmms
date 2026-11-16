#include <QFontMetrics>
#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QScreen>
#include <QScrollBar>
#include <QToolButton>

#include "ConfigManager.h"
#include "ToolButton.h"
#include "agent/chat/ChatAttachmentsDialog.h"
#include "agent/chat/ChatController.h"
#include "agent/chat/ChatDock.h"
#include "agent/chat/ChatTranscript.h"
#include "embed.h"

namespace lmms::agent::chat {
namespace {

const QStringList ReasoningLevels{"auto", "off", "low", "medium", "high", "xhigh"};

bool supportsReasoning(const ChatConnection& connection, const QString& level)
{
	return level == "auto"
		|| (connection.capabilities.value("reasoningLevels").toArray().contains(level)
			&& !connection.capabilities.value("reasoningParameters").toObject().value(level).toObject().isEmpty());
}

} // namespace

void ChatDock::refreshConnection()
{
	QString error;
	const auto connections = m_connectionStore.connections(error);
	const auto selected
		= m_current.value("connectionId").toString(ConfigManager::inst()->value("agentchat", "connection"));
	m_connection = {};
	for (const auto& connection : connections)
	{
		if (m_connection.id.isEmpty() || connection.id == selected)
		{
			m_connection = connection;
		}
		if (connection.id == selected)
		{
			break;
		}
	}
	const auto level = m_current.value("reasoning").toString(m_connection.reasoning);
	m_connection.reasoning = supportsReasoning(m_connection, level) ? level : QStringLiteral("auto");
	m_model->setText(m_connection.id.isEmpty() ? tr("Configure a model")
											   : m_connection.name + " / " + m_connection.model);
	m_model->setToolTip(error.isEmpty() ? tr("API and model settings") : error);
	updateRequestControls();
}

void ChatDock::updateRequestControls()
{
	const bool running = m_controller && m_controller->active();
	m_model->setEnabled(!running);
	m_reasoning->setEnabled(!running && !m_connection.id.isEmpty());
	m_attach->setEnabled(!running);
	m_attach->setToolTip(tr("Add attachments"));
	m_attach->setAccessibleName(tr("Add attachments"));
	m_attachmentChip->setEnabled(!running);
	const auto attachments = selectedAttachments();
	if (!attachments.isEmpty())
	{
		const auto caption
			= attachments.first().name + (attachments.size() > 1 ? tr(" +%1").arg(attachments.size() - 1) : QString{});
		m_attachmentCaption->setText(QFontMetrics(m_attachmentCaption->font())
										 .elidedText(caption, Qt::ElideMiddle, m_attachmentCaption->width()));
		m_attachmentChip->setAccessibleName(caption);
		m_attachmentChip->setToolTip("<qt>" + caption.toHtmlEscaped() + "</qt>");
		const auto remove = tr("Remove attachment: %1").arg(attachments.first().name);
		m_attachmentRemove->setAccessibleName(remove);
		m_attachmentRemove->setToolTip("<qt>" + remove.toHtmlEscaped() + "</qt>");
	}
	if (!running)
	{
		m_footer->setText(retryAvailable() ? tr("Request failed · Click Send to retry")
										   : tr("Enter to send · Shift+Enter for a new line"));
	}
	const QStringList captions{tr("Auto"), tr("Off"), tr("Low"), tr("Medium"), tr("High"), tr("Extra high")};
	const int level = ReasoningLevels.indexOf(m_connection.reasoning);
	m_reasoning->setText(tr("Reasoning · %1").arg(level >= 0 ? captions[level] : captions.first()));
	const auto text
		= running ? tr("Stop current request") : (retryAvailable() ? tr("Retry last failed request") : tr("Send"));
	m_send->setText(text);
	m_send->setToolTip(text);
	m_send->setAccessibleName(text);
	m_send->setIcon(embed::getIconPixmap(running ? "agent_chat_stop" : "agent_chat_send", 18, 18));
	m_send->setEnabled(
		running
		|| (!m_connection.id.isEmpty() && !m_connection.model.isEmpty()
			&& (!m_input->toPlainText().trimmed().isEmpty() || !attachments.isEmpty() || retryAvailable())));
}

QVector<ChatAttachment> ChatDock::selectedAttachments() const
{
	QVector<ChatAttachment> attachments;
	for (const auto& value : m_current.value("draftAttachments").toArray())
	{
		const auto object = value.toObject();
		attachments.append({object.value("path").toString(), object.value("name").toString(),
							object.value("mimeType").toString(), object.value("type").toString(),
							object.value("size").toInteger()});
	}
	return attachments;
}

void ChatDock::showAttachments()
{
	ChatAttachmentsDialog dialog(m_connection, selectedAttachments(), this);
	dialog.exec();
	QJsonArray selected;
	for (const auto& attachment : dialog.attachments())
	{
		selected.append(attachment.toJson());
	}
	m_current.insert("draftAttachments", selected);
	if (persistCurrent())
	{
		updateRequestControls();
		arrangeWorkspace();
	}
}

void ChatDock::removeFirstAttachment()
{
	auto attachments = m_current.value("draftAttachments").toArray();
	attachments.removeAt(0);
	m_current.insert("draftAttachments", attachments);
	if (persistCurrent())
	{
		updateRequestControls();
		arrangeWorkspace();
	}
}

void ChatDock::showModelMenu()
{
	QString error;
	const auto connections = m_connectionStore.connections(error);
	if (!error.isEmpty())
	{
		showError(error);
		return;
	}
	QMenu menu(this);
	menu.setObjectName("agentModelMenu");
	for (const auto& connection : connections)
	{
		auto* action = menu.addAction(connection.name + " / " + connection.model);
		action->setData(connection.id);
		action->setCheckable(true);
		action->setChecked(connection.id == m_connection.id);
	}
	menu.addSeparator();
	auto* settings = menu.addAction(tr("API and model settings"));
	const auto* selected = menu.exec(m_model->mapToGlobal(QPoint(0, m_model->height())));
	if (selected == settings)
	{
		showSettings();
	}
	else if (selected)
	{
		m_current.insert("connectionId", selected->data().toString());
		refreshConnection();
		m_current.insert("reasoning", m_connection.reasoning);
		persistCurrent();
	}
}

void ChatDock::showReasoningMenu()
{
	// Penpot board 11: 316x384 card, title (22,21), six 294x38
	// choices at (11,66 + 45*i). Keep the native controls at those rectangles.
	auto* popup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
	popup->setObjectName("agentReasoningPopup");
	popup->setProperty("agentSettingsCard", true);
	popup->setAttribute(Qt::WA_DeleteOnClose);
	popup->setFixedSize(316, 384);
	auto* title = new QLabel(tr("Reasoning level"), popup);
	title->setGeometry(22, 21, 250, 27);
	auto titleFont = title->font();
	titleFont.setPixelSize(17);
	title->setFont(titleFont);
	const QStringList captions{tr("Auto"), tr("Off"), tr("Low"), tr("Medium"), tr("High"), tr("Extra high")};
	for (int index = 0; index < ReasoningLevels.size(); ++index)
	{
		const auto level = ReasoningLevels[index];
		auto* choice = new gui::ToolButton(popup);
		choice->setObjectName("agentQuickReasoning_" + level);
		choice->setProperty("agentChatControl", true);
		choice->setGeometry(11, 66 + index * 45, 294, 38);
		choice->setCheckable(true);
		choice->setAutoExclusive(true);
		choice->setChecked(m_connection.reasoning == level);
		choice->setEnabled(supportsReasoning(m_connection, level));
		choice->setAccessibleName(captions[index]);
		auto* caption = new QLabel(captions[index], choice);
		caption->setGeometry(19, 7, 100, 21);
		caption->setAttribute(Qt::WA_TransparentForMouseEvents);
		if (choice->isChecked())
		{
			auto* check = new QLabel(choice);
			check->setPixmap(embed::getIconPixmap("agent_chat_check", 16, 16));
			check->setGeometry(264, 10, 16, 16);
			check->setAttribute(Qt::WA_TransparentForMouseEvents);
		}
		if (!choice->isEnabled())
		{
			auto* hint = new QLabel(tr("Not supported"), choice);
			hint->setGeometry(108, 7, 170, 21);
			hint->setAttribute(Qt::WA_TransparentForMouseEvents);
		}
		connect(choice, &QToolButton::clicked, popup, [this, popup, level] {
			m_current.insert("reasoning", level);
			m_connection.reasoning = level;
			if (persistCurrent())
			{
				updateRequestControls();
				popup->close();
			}
		});
	}
	auto position = m_reasoning->mapToGlobal(QPoint(0, -popup->height()));
	const auto available = m_reasoning->screen()->availableGeometry();
	position.setX(qBound(available.left(), position.x(), available.right() - popup->width() + 1));
	if (position.y() < available.top())
	{
		position.setY(m_reasoning->mapToGlobal(QPoint(0, m_reasoning->height())).y());
	}
	popup->move(position);
	popup->show();
}

bool ChatDock::retryAvailable() const
{
	if (!m_input->toPlainText().trimmed().isEmpty() || !selectedAttachments().isEmpty())
	{
		return false;
	}
	const auto messages = m_current.value("messages").toArray();
	const auto last = messages.isEmpty() ? QJsonObject{} : messages.last().toObject();
	return last.value("role") == "assistant" && last.value("status") == "error";
}

void ChatDock::sendOrStop()
{
	if (m_controller->active())
	{
		m_controller->stop();
		return;
	}
	QString error;
	const auto key = m_connectionStore.readKey(m_connection.id, error);
	if (!error.isEmpty())
	{
		showError(error);
		return;
	}
	const auto prompt = ChatStore::readPrompt(
		ConfigManager::inst()->value("agentchat", "promptPath", "data/system_prompt.txt"), error);
	if (!error.isEmpty())
	{
		showError(error);
		return;
	}
	const auto sessionId = m_current.value("id").toString();
	const bool started = retryAvailable() ? m_controller->retry(sessionId, m_connection, key, prompt, error)
										  : m_controller->send(sessionId, m_connection, key, prompt,
															   m_input->toPlainText(), error, selectedAttachments());
	if (!started)
	{
		showError(error);
	}
}

void ChatDock::updateStreamingMessage(const QString& sessionId, const QJsonObject& message)
{
	if (m_current.value("id").toString() == sessionId && message.value("id").toString() == m_streamMessageId)
	{
		m_messages->updateMessage(message);
	}
}

} // namespace lmms::agent::chat
