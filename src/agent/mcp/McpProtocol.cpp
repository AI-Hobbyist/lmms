#include "agent/mcp/McpProtocol.h"
#include "agent/ToolRegistry.h"
#include "lmmsversion.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUuid>
#include <cmath>

namespace lmms::agent::mcp
{
namespace
{
HttpResponse json(int status, const QJsonObject& body)
{
	return {status, {{"Content-Type", "application/json"}}, QJsonDocument(body).toJson(QJsonDocument::Compact)};
}
HttpResponse failure(int status, int code, const QString& message, QJsonValue id = QJsonValue::Null)
{
	return json(status, {{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}});
}
HttpResponse success(QJsonValue id, const QJsonObject& result)
{
	return json(200, {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}
bool accepts(const QByteArray& header, const QByteArray& media)
{
	for (const auto& part : header.toLower().split(','))
	{
		const auto fields = part.split(';');
		if (fields.first().trimmed() != media) { continue; }
		bool enabled = true;
		for (int i = 1; i < fields.size(); ++i)
		{
			const auto parameter = fields[i].trimmed();
			if (parameter.startsWith("q="))
			{
				bool valid = false;
				const auto quality = parameter.mid(2).toDouble(&valid);
				enabled = valid && quality > 0 && quality <= 1;
			}
		}
		if (enabled) { return true; }
	}
	return false;
}
bool validId(const QJsonValue& id)
{
	return id.isString() || (id.isDouble() && std::isfinite(id.toDouble()) && std::floor(id.toDouble()) == id.toDouble());
}
}
QString McpProtocol::version() { return "2025-11-25"; }
McpProtocol::McpProtocol(HttpMcpServer& server) : QObject(&server)
{
	server.setHandler([this](const HttpRequest& request, HttpMcpServer::Reply reply) { handle(request, std::move(reply)); });
	connect(&server, &HttpMcpServer::stateChanged, this, [this, &server] {
		if (!server.isRunning()) { reset(); }
	});
}
void McpProtocol::reset() { m_session.clear(); m_initialized = false; }
void McpProtocol::handle(const HttpRequest& request, HttpMcpServer::Reply reply)
{
	if (request.path != "/mcp") { reply(failure(404, -32600, "Use /mcp.")); return; }
	if (request.method != "POST")
	{
		auto response = failure(405, -32600, "Only POST is supported; no standalone SSE stream.");
		response.headers.insert("Allow", "POST"); reply(response); return;
	}
	if (request.headers.value("content-type").toLower().split(';').first().trimmed() != "application/json")
	{
		reply(failure(415, -32600, "Content-Type must be application/json.")); return;
	}
	const auto accept = request.headers.value("accept");
	if (!accepts(accept, "application/json") || !accepts(accept, "text/event-stream"))
	{
		reply(failure(400, -32600, "Accept must include application/json and text/event-stream.")); return;
	}
	const auto protocol = request.headers.value("mcp-protocol-version");
	if (!protocol.isEmpty() && protocol != version().toUtf8())
	{
		reply(failure(400, -32600, "Unsupported MCP-Protocol-Version.")); return;
	}
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(request.body, &parse);
	if (parse.error != QJsonParseError::NoError || QString::fromUtf8(request.body).toUtf8() != request.body)
	{
		reply(failure(400, -32700, "Invalid UTF-8 JSON.")); return;
	}
	if (!document.isObject()) { reply(failure(400, -32600, "Expected a single JSON-RPC object; batches are unsupported.")); return; }
	const auto message = document.object();
	const auto id = message.value("id");
	const bool hasId = message.contains("id");
	if (message.value("jsonrpc") != QJsonValue("2.0") || (hasId && !validId(id)))
	{
		reply(failure(400, -32600, "Invalid JSON-RPC envelope.")); return;
	}
	const auto methodValue = message.value("method");
	if ((!methodValue.isString() || methodValue.toString().isEmpty()) &&
		!(methodValue.isUndefined() && hasId && (message.contains("result") != message.contains("error"))))
	{
		reply(failure(400, -32600, "Invalid JSON-RPC method or response.", hasId ? id : QJsonValue(QJsonValue::Null))); return;
	}
	if (message.contains("params") && !message.value("params").isObject())
	{
		reply(failure(400, -32602, "MCP params must be an object.", hasId ? id : QJsonValue(QJsonValue::Null))); return;
	}
	const auto method = methodValue.toString();
	const auto params = message.value("params").toObject();
	if (method == "initialize" && hasId)
	{
		const auto client = params.value("clientInfo").toObject();
		if (!params.value("protocolVersion").isString() || params.value("protocolVersion").toString().isEmpty() ||
			!params.value("capabilities").isObject() || !client.value("name").isString() || !client.value("version").isString())
		{
			reply(failure(200, -32602, "initialize requires protocolVersion, capabilities and clientInfo.", id)); return;
		}
		reset();
		m_session = QUuid::createUuid().toString(QUuid::WithoutBraces);
		auto response = success(id, {{"protocolVersion", version()}, {"capabilities", QJsonObject{{"tools", QJsonObject{}}}},
			{"serverInfo", QJsonObject{{"name", "LMMS"}, {"version", LMMS_VERSION}}}});
		response.headers.insert("MCP-Session-Id", m_session.toUtf8());
		reply(response); return;
	}
	if (m_session.isEmpty() || request.headers.value("mcp-session-id") != m_session.toUtf8())
	{
		reply(failure(request.headers.contains("mcp-session-id") ? 404 : 400, -32600, "Initialize a local MCP session first.")); return;
	}
	if (!hasId)
	{
		if (method == "notifications/initialized") { m_initialized = true; }
		reply({202, {}, {}}); return;
	}
	if (methodValue.isUndefined()) { reply({202, {}, {}}); return; }
	if (method == "ping") { reply(success(id, {})); return; }
	if (!m_initialized) { reply(failure(200, -32600, "Send notifications/initialized first.", id)); return; }
	if (method == "tools/list")
	{
		if (params.contains("cursor")) { reply(failure(200, -32602, "This tool list is not paginated.", id)); return; }
		reply(success(id, {{"tools", ToolRegistry::tools()}})); return;
	}
	reply(failure(200, -32601, "Unknown MCP method.", id));
}
}
