#include "agent/chat/ChatWebSearch.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTimer>
#include <QUrlQuery>

#include "agent/CommandBus.h"

namespace lmms::agent::chat {
namespace {

QString message(const char* source)
{ return QCoreApplication::translate("AgentChat", source); }

constexpr int TransferTimeoutMs = 20000;
constexpr qint64 MaximumPageBytes = 2 * 1024 * 1024;
constexpr int MaximumPageCharacters = 20000;

QJsonObject failure(const QString& code, const QString& text, const QString& hint = {})
{ return CommandResult::failure(code, text, hint).toJson(); }

// SearXNG instances are addressed by their base URL; the JSON API lives at /search.
QUrl searxngSearchUrl(const QString& base, const QString& query)
{
	QUrl url(base);
	auto path = url.path();
	if (!path.endsWith("/search"))
	{
		if (!path.endsWith('/')) { path += '/'; }
		path += "search";
	}
	url.setPath(path);
	QUrlQuery parameters;
	parameters.addQueryItem("q", query);
	parameters.addQueryItem("format", "json");
	url.setQuery(parameters);
	return url;
}

// "user:password" is sent as HTTP Basic authentication; any other value is a bearer token.
QByteArray authorizationHeader(const QString& credential)
{
	if (credential.contains(':')) { return "Basic " + credential.toUtf8().toBase64(); }
	return "Bearer " + credential.toUtf8();
}

QJsonArray searxngResults(const QJsonObject& body, int count)
{
	QJsonArray results;
	for (const auto& value : body.value("results").toArray())
	{
		const auto item = value.toObject();
		results.append(QJsonObject{{"title", item.value("title").toString()}, {"url", item.value("url").toString()},
			{"snippet", item.value("content").toString()}});
		if (results.size() >= count) { break; }
	}
	return results;
}

QJsonArray braveResults(const QJsonObject& body, int count)
{
	QJsonArray results;
	for (const auto& value : body.value("web").toObject().value("results").toArray())
	{
		const auto item = value.toObject();
		results.append(QJsonObject{{"title", item.value("title").toString()}, {"url", item.value("url").toString()},
			{"snippet", item.value("description").toString()}});
		if (results.size() >= count) { break; }
	}
	return results;
}

} // namespace

ChatWebSearch::ChatWebSearch(QObject* parent)
	: QObject(parent)
	, m_network(new QNetworkAccessManager(this))
{
}

ChatWebSearch::~ChatWebSearch()
{ abort(); }

QString ChatWebSearch::command()
{ return QStringLiteral("chat.webSearch"); }

QJsonObject ChatWebSearch::definition()
{
	const QJsonObject properties{{"query", QJsonObject{{"type", "string"}, {"description", "Search query."}}},
		{"count",
			QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 10},
				{"description", "Maximum number of results (default 5)."}}}};
	return {{"name", command()},
		{"description",
			"Search the web with the search source configured by the user. Returns titles, URLs and snippets."},
		{"inputSchema",
			QJsonObject{{"type", "object"}, {"properties", properties}, {"required", QJsonArray{"query"}},
				{"additionalProperties", false}}},
		{"annotations", QJsonObject{{"readOnlyHint", true}, {"openWorldHint", true}}}};
}

void ChatWebSearch::start(
	const ChatSearchSettings& settings, const QString& credential, const QString& query, int count)
{
	abort();
	m_reading = false;
	m_backend = settings.backend;
	m_query = query;
	m_count = count;

	QNetworkRequest request;
	request.setTransferTimeout(TransferTimeoutMs);
	request.setRawHeader("Accept", "application/json");
	if (settings.backend == ChatSearchBackend::Brave)
	{
		QUrl url(settings.braveUrl);
		QUrlQuery parameters;
		parameters.addQueryItem("q", query);
		parameters.addQueryItem("count", QString::number(count));
		url.setQuery(parameters);
		request.setUrl(url);
		request.setRawHeader("X-Subscription-Token", credential.toUtf8());
	}
	else
	{
		request.setUrl(searxngSearchUrl(settings.searxngUrl, query));
		if (!credential.isEmpty()) { request.setRawHeader("Authorization", authorizationHeader(credential)); }
	}
	auto* reply = m_network->get(request);
	m_reply = reply;
	connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReply(reply); });
}

QString ChatWebSearch::readCommand()
{ return QStringLiteral("chat.readPage"); }

QJsonObject ChatWebSearch::readDefinition()
{
	const QJsonObject properties{
		{"url", QJsonObject{{"type", "string"}, {"description", "http or https address of the page to read."}}}};
	return {{"name", readCommand()},
		{"description", "Read a web page and return its title and readable text, for example a search result to cite."},
		{"inputSchema",
			QJsonObject{{"type", "object"}, {"properties", properties}, {"required", QJsonArray{"url"}},
				{"additionalProperties", false}}},
		{"annotations", QJsonObject{{"readOnlyHint", true}, {"openWorldHint", true}}}};
}

void ChatWebSearch::startRead(const QString& address)
{
	abort();
	const QUrl url(address);
	if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "http" && url.scheme() != "https"))
	{
		// Report asynchronously like a network result, so callers have one code path.
		QTimer::singleShot(0, this, [this] {
			emit finished(failure("invalid_url", message("Only http and https page addresses can be read.")));
		});
		return;
	}
	m_reading = true;
	m_pageUrl = url;

	QNetworkRequest request(url);
	request.setTransferTimeout(TransferTimeoutMs);
	request.setRawHeader("Accept", "text/html, text/plain;q=0.9");
	auto* reply = m_network->get(request);
	m_reply = reply;
	connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReply(reply); });
	connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 received, qint64) {
		if (reply == m_reply && received > MaximumPageBytes)
		{
			abort();
			emit finished(failure("page_too_large", message("The page is larger than the 2 MB reading limit.")));
		}
	});
}

void ChatWebSearch::handlePage(QNetworkReply* reply)
{
	if (reply->error() != QNetworkReply::NoError)
	{
		emit finished(failure("page_read_failed", message("Reading the page failed: %1").arg(reply->errorString())));
		return;
	}
	const auto type = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
	const bool html = type.contains("html");
	if (!html && !type.startsWith("text/plain"))
	{
		emit finished(failure("unsupported_content",
			message("The page content type is not readable text: %1").arg(type.isEmpty() ? "unknown" : type)));
		return;
	}
	const auto content = QString::fromUtf8(reply->readAll());
	QString title;
	QString text = content;
	if (html)
	{
		static const QRegularExpression titlePattern("<title[^>]*>(.*?)</title>",
			QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
		static const QRegularExpression hiddenPattern("<(script|style|noscript)[^>]*>.*?</\\1>",
			QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
		QTextDocument document;
		document.setHtml(titlePattern.match(content).captured(1));
		title = document.toPlainText().simplified();
		auto visible = content;
		visible.remove(hiddenPattern);
		document.setHtml(visible);
		text = document.toPlainText();
	}
	// Collapse blank runs so the limit is spent on readable text.
	QStringList lines;
	for (const auto& line : text.split('\n'))
	{
		const auto trimmed = line.simplified();
		if (!trimmed.isEmpty()) { lines.append(trimmed); }
	}
	text = lines.join('\n');
	const bool truncated = text.size() > MaximumPageCharacters;
	text.truncate(MaximumPageCharacters);
	emit finished(
		CommandResult::success(QJsonObject{{"url", m_pageUrl.toString()}, {"finalUrl", reply->url().toString()},
								   {"title", title}, {"text", text}, {"truncated", truncated}})
			.toJson());
}

void ChatWebSearch::abort()
{
	if (!m_reply) { return; }
	// Detach first so the aborted reply's finished signal is ignored.
	auto* reply = m_reply.data();
	m_reply = nullptr;
	reply->disconnect(this);
	reply->abort();
	reply->deleteLater();
}

bool ChatWebSearch::active() const
{ return !m_reply.isNull(); }

void ChatWebSearch::handleReply(QNetworkReply* reply)
{
	if (reply != m_reply)
	{
		reply->deleteLater();
		return;
	}
	m_reply = nullptr;
	reply->deleteLater();
	if (m_reading)
	{
		m_reading = false;
		handlePage(reply);
		return;
	}

	const bool searxng = m_backend != ChatSearchBackend::Brave;
	const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	const auto bytes = reply->readAll();
	QJsonParseError parseError;
	const auto document = QJsonDocument::fromJson(bytes, &parseError);
	const bool json = parseError.error == QJsonParseError::NoError && document.isObject();

	if (reply->error() != QNetworkReply::NoError || !json)
	{
		const auto detail = reply->error() != QNetworkReply::NoError
			? reply->errorString()
			: message("The response is not JSON (HTTP %1).").arg(status);
		if (searxng)
		{
			// A disabled JSON format usually shows up as HTTP 403 or an HTML page.
			emit finished(failure("searxng_json_unavailable", message("SearXNG search failed: %1").arg(detail),
				message("Enable the json format under search.formats in the instance's settings.yml.")));
		}
		else
		{
			emit finished(failure("brave_search_failed", message("Brave search failed: %1").arg(detail)));
		}
		return;
	}

	const auto body = document.object();
	const auto results = searxng ? searxngResults(body, m_count) : braveResults(body, m_count);
	emit finished(CommandResult::success(
		QJsonObject{{"backend", ChatSearchSettings::backendName(m_backend)}, {"query", m_query}, {"results", results}})
			.toJson());
}

} // namespace lmms::agent::chat
