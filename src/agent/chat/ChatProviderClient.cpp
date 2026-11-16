#include "agent/chat/ChatProviderClient.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "agent/chat/ChatRequest.h"

namespace lmms::agent::chat {

ChatProviderClient::ChatProviderClient(QObject* parent)
	: QObject(parent)
{ m_network = new QNetworkAccessManager(this); }

ChatProviderClient::~ChatProviderClient()
{ releaseReply(); }

bool ChatProviderClient::active() const
{ return !m_reply.isNull(); }

ChatResponse ChatProviderClient::response() const
{ return m_result; }

QString ChatProviderClient::redact(QString text) const
{
	if (!m_key.isEmpty())
	{
		text.replace(m_key, "••••");
		text.replace(QString::fromLatin1(QUrl::toPercentEncoding(m_key)), "••••");
	}
	return text;
}

void ChatProviderClient::releaseReply()
{
	if (auto* reply = m_reply.data())
	{
		m_reply = nullptr;
		reply->disconnect(this);
		if (!reply->isFinished())
		{
			reply->abort();
		}
		reply->deleteLater();
	}
}

bool ChatProviderClient::start(const ChatConnection& connection, const QString& key, const QString& systemPrompt,
							   const QJsonArray& messages, QString& error)
{
	if (active())
	{
		error = QCoreApplication::translate("AgentChat", "A model request is already running.");
		return false;
	}
	ChatRequest request;
	if (!buildChatRequest(connection, key, systemPrompt, messages, request, error))
	{
		return false;
	}
	m_result = {};
	m_key = key;
	m_stream = {};
	m_jsonBytes.clear();
	m_jsonResponse = false;
	m_decoder = std::make_unique<ChatResponseDecoder>(connection);
	auto* reply = m_network->post(request.network, QJsonDocument(request.body).toJson(QJsonDocument::Compact));
	m_reply = reply;
	connect(reply, &QNetworkReply::uploadProgress, this, &ChatProviderClient::uploadProgress);
	connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
		if (m_reply != reply)
		{
			return;
		}
		const auto previousText = m_result.text;
		const auto previousSummary = m_result.summary;
		if (!readAvailable(reply))
		{
			finishRequest(m_decoder->response().error);
			return;
		}
		if (m_result.text != previousText || m_result.summary != previousSummary)
		{
			emit updated(m_result.text, m_result.summary);
		}
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		if (m_reply != reply)
		{
			return;
		}
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
		{
			finishRequest(QCoreApplication::translate("AgentChat", "Model request failed: HTTP %1, network error %2.")
							  .arg(status)
							  .arg(int(reply->error())));
			return;
		}
		if (!readAvailable(reply))
		{
			finishRequest(m_decoder->response().error);
			return;
		}
		if (m_jsonResponse)
		{
			m_decoder->consumeJson(m_jsonBytes);
		}
		else
		{
			for (const auto& event : m_stream.finish())
			{
				if (!m_decoder->consume(event))
				{
					finishRequest(m_decoder->response().error);
					return;
				}
			}
		}
		m_decoder->finish();
		m_result = m_decoder->response();
		finishRequest(m_result.error);
	});
	return true;
}

bool ChatProviderClient::readAvailable(QNetworkReply* reply)
{
	const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (status < 200 || status >= 300)
	{
		return true; // HTTP failures are handled by the finished callback.
	}
	const auto contentType = reply->rawHeader("Content-Type").split(';').first().trimmed().toLower();
	if (contentType == "application/json" || contentType.endsWith("+json"))
	{
		m_jsonResponse = true;
		m_jsonBytes.append(reply->readAll());
		return true;
	}
	for (const auto& event : m_stream.feed(reply->readAll()))
	{
		if (!m_decoder->consume(event))
		{
			m_result = m_decoder->response();
			return false;
		}
	}
	m_result = m_decoder->response();
	return true;
}

void ChatProviderClient::finishRequest(const QString& error)
{
	if (!error.isEmpty())
	{
		m_result.error = redact(error);
		m_result.complete = false;
		m_result.toolCalls = {};
		m_result.nativeContent = {};
	}
	releaseReply();
	m_jsonBytes.clear();
	m_key.clear();
	emit finished();
}

void ChatProviderClient::cancel()
{
	if (!active())
	{
		return;
	}
	releaseReply();
	m_jsonBytes.clear();
	m_result.complete = false;
	m_result.toolCalls = {};
	m_result.nativeContent = {};
	m_key.clear();
	emit canceled();
}

} // namespace lmms::agent::chat
