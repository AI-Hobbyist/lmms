#include "agent/chat/ChatAttachment.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMimeDatabase>
#include <QStringDecoder>

namespace lmms::agent::chat {
namespace {

// Bound inline base64 copies on the supported 16 GB Windows machines. A model
// can declare a smaller maxAttachmentBytes limit, never a larger process limit.
constexpr qint64 MaxAttachmentBytes = 64 * 1024 * 1024;

QString message(const char* source)
{ return QCoreApplication::translate("AgentChat", source); }

} // namespace

QJsonObject ChatAttachment::toJson() const
{ return {{"path", path}, {"name", name}, {"mimeType", mimeType}, {"type", type}, {"size", size}}; }

bool inspectChatAttachment(const QString& path, ChatAttachment& attachment, QString& error)
{
	error.clear();
	attachment = {};
	const QFileInfo file(path);
	if (!file.isFile() || !file.isReadable())
	{
		error = message("Attachment is not a readable file: %1").arg(path);
		return false;
	}
	attachment.path = file.absoluteFilePath();
	attachment.name = file.fileName();
	attachment.size = file.size();
	attachment.mimeType = QMimeDatabase().mimeTypeForFile(file.fileName(), QMimeDatabase::MatchExtension).name();
	if (attachment.mimeType == "audio/vnd.wave" || attachment.mimeType == "audio/x-wav")
	{
		attachment.mimeType = "audio/wav";
	}
	if (file.suffix().compare("mid", Qt::CaseInsensitive) == 0
		|| file.suffix().compare("midi", Qt::CaseInsensitive) == 0)
	{
		attachment.type = "midi";
		attachment.mimeType = "audio/midi";
	}
	else if (attachment.mimeType.startsWith("text/") || attachment.mimeType == "application/json"
			 || attachment.mimeType == "application/xml")
	{
		attachment.type = "text";
	}
	else if (attachment.mimeType.startsWith("image/"))
	{
		attachment.type = "image";
	}
	else if (attachment.mimeType.startsWith("audio/"))
	{
		attachment.type = "audio";
	}
	else
	{
		attachment.type = "file";
	}
	return true;
}

bool supportsChatAttachment(const ChatConnection& connection, const ChatAttachment& attachment, QString& error)
{
	error.clear();
	const auto configuredLimit = connection.capabilities.value("maxAttachmentBytes").toInteger(MaxAttachmentBytes);
	const qint64 limit = qMin(MaxAttachmentBytes, configuredLimit);
	if (attachment.size <= 0 || limit <= 0 || attachment.size > limit)
	{
		error = message("Attachment is empty or exceeds the %1 byte inline limit: %2").arg(limit).arg(attachment.name);
		return false;
	}
	if (attachment.type == "text" || attachment.type == "midi")
	{
		return true;
	}
	const auto types = connection.capabilities.value("inputTypes").toArray();
	const bool gemini = connection.protocol == "gemini";
	const bool chat = connection.protocol == "openai-chat" || connection.protocol == "openai-compatible"
		|| connection.protocol == "azure-chat";
	const bool responses = connection.protocol.endsWith("responses");
	const bool anthropic = connection.protocol == "anthropic-messages";
	bool supported = false;
	if (types.contains(attachment.type))
	{
		if (attachment.type == "image")
		{
			const QStringList formats{"image/png", "image/jpeg", "image/webp", "image/gif"};
			supported = (chat || responses || anthropic || gemini) && formats.contains(attachment.mimeType);
		}
		else if (attachment.type == "audio")
		{
			const QStringList chatFormats{"audio/wav", "audio/x-wav", "audio/mpeg"};
			supported = gemini || (chat && chatFormats.contains(attachment.mimeType));
		}
		else if (attachment.type == "file")
		{
			supported = (chat || responses || anthropic || gemini) && attachment.mimeType == "application/pdf";
		}
	}
	const auto mimeTypes = connection.capabilities.value("inputMimeTypes").toArray();
	if (!mimeTypes.isEmpty() && !mimeTypes.contains(attachment.mimeType))
	{
		supported = false;
	}
	if (!supported)
	{
		error = message("This model and protocol do not support %1 attachments: %2")
					.arg(attachment.mimeType, attachment.name);
	}
	return supported;
}

bool prepareChatAttachment(const ChatConnection& connection, const ChatAttachment& attachment, QJsonObject& part,
						   QString& error)
{
	part = {};
	ChatAttachment current;
	if (!inspectChatAttachment(attachment.path, current, error) || !supportsChatAttachment(connection, current, error))
	{
		return false;
	}
	if (current.size != attachment.size || current.mimeType != attachment.mimeType)
	{
		error = message("Attachment changed after selection; select it again: %1").arg(attachment.name);
		return false;
	}
	if (current.type == "midi")
	{
		part = {{"type", "text"},
				{"text",
				 message("Local MIDI reference (not uploaded or parsed): %1 (%2 bytes).")
					 .arg(current.path)
					 .arg(current.size)}};
		return true;
	}
	QFile file(current.path);
	if (!file.open(QIODevice::ReadOnly))
	{
		error = file.errorString();
		return false;
	}
	const auto bytes = file.read(current.size + 1);
	if (bytes.size() != current.size || !file.atEnd() || file.error() != QFileDevice::NoError)
	{
		error = message("Cannot read the complete attachment: %1").arg(current.name);
		return false;
	}
	if (current.type == "text")
	{
		QStringDecoder decoder(QStringDecoder::Utf8);
		const QString text = decoder(bytes);
		if (decoder.hasError())
		{
			error = message("Text attachments must be UTF-8: %1").arg(current.name);
			return false;
		}
		part = {{"type", "text"}, {"text", message("Attachment data: %1\n%2").arg(current.name, text)}};
	}
	else
	{
		part = {{"type", current.type},
				{"name", current.name},
				{"mimeType", current.mimeType},
				{"data", QString::fromLatin1(bytes.toBase64())}};
	}
	return true;
}

} // namespace lmms::agent::chat
