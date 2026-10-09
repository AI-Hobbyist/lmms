#include "agent/mcp/McpProtocol.h"
#include "agent/ToolRegistry.h"
#include "agent/CommandBus.h"
#include "agent/ExportCommands.h"
#include "lmmsversion.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUuid>
#include <QTimer>
#include <QPointer>
#include <cmath>

namespace lmms::agent::mcp {
namespace {
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
		if (fields.first().trimmed() != media)
		{
			continue;
		}
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
		if (enabled)
		{
			return true;
		}
	}
	return false;
}
bool validId(const QJsonValue& id)
{
	return id.isString()
		|| (id.isDouble() && std::isfinite(id.toDouble()) && std::floor(id.toDouble()) == id.toDouble());
}
}
QString McpProtocol::version()
{
	return "2025-11-25";
}
McpProtocol::McpProtocol(HttpMcpServer& server)
	: QObject(&server)
{
	const QPointer<McpProtocol> guard(this);
	server.setHandler([guard](const HttpRequest& request, HttpMcpServer::Reply reply) {
		if (guard)
		{
			guard->handle(request, std::move(reply));
		}
		else
		{
			reply(failure(503, -32603, "The MCP protocol handler is unavailable."));
		}
	});
	connect(&server, &HttpMcpServer::stateChanged, this, [this, &server] {
		if (!server.isRunning())
		{
			reset();
		}
		if (server.state() == HttpMcpServer::State::Stopping && !m_exportTask.isEmpty())
		{
			CommandBus::instance().execute("export.cancel", {{"task", m_exportTask}});
			m_exportTask.clear();
		}
	});
}
void McpProtocol::reset()
{
	m_session.clear();
	m_initialized = false;
	while (!m_queue.empty())
	{
		auto request = std::move(m_queue.front());
		m_queue.pop_front();
		request.reply(failure(503, -32800, "The MCP session stopped or was replaced.", request.id));
	}
}
void McpProtocol::drain()
{
	m_scheduled = false;
	if (m_executing || m_queue.empty())
	{
		return;
	}
	auto request = std::move(m_queue.front());
	m_queue.pop_front();
	m_executing = true;
	const bool exportingBefore = hasActiveAudioExport();
	const auto result = CommandBus::instance().execute(request.name, request.arguments);
	if (result.ok && !exportingBefore && hasActiveAudioExport() && !request.arguments.value("dryRun").toBool())
	{
		auto task = result.data.value("task").toString();
		if (task.isEmpty())
		{
			task = result.data.value("lastResult").toObject().value("task").toString();
		}
		if (!task.isEmpty())
		{
			m_exportTask = task;
		}
	}
	m_executing = false;
	const auto value = result.toJson();
	const QPointer<McpProtocol> guard(this);
	request.reply(success(request.id,
		{{"content",
			 QJsonArray{QJsonObject{
				 {"type", "text"}, {"text", QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact))}}}},
			{"structuredContent", value}, {"isError", !result.ok}}));
	if (!guard)
	{
		return;
	}
	if (!m_queue.empty() && !m_scheduled)
	{
		m_scheduled = true;
		QTimer::singleShot(0, this, [this] { drain(); });
	}
}
void McpProtocol::handle(const HttpRequest& request, HttpMcpServer::Reply reply)
{
	if (request.path != "/mcp")
	{
		reply(failure(404, -32600, "Use /mcp."));
		return;
	}
	if (request.method != "POST")
	{
		auto response = failure(405, -32600, "Only POST is supported; no standalone SSE stream.");
		response.headers.insert("Allow", "POST");
		reply(response);
		return;
	}
	if (request.headers.value("content-type").toLower().split(';').first().trimmed() != "application/json")
	{
		reply(failure(415, -32600, "Content-Type must be application/json."));
		return;
	}
	const auto accept = request.headers.value("accept");
	if (!accepts(accept, "application/json") || !accepts(accept, "text/event-stream"))
	{
		reply(failure(400, -32600, "Accept must include application/json and text/event-stream."));
		return;
	}
	const auto protocol = request.headers.value("mcp-protocol-version");
	if (!protocol.isEmpty() && protocol != version().toUtf8())
	{
		reply(failure(400, -32600, "Unsupported MCP-Protocol-Version."));
		return;
	}
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(request.body, &parse);
	if (parse.error != QJsonParseError::NoError || QString::fromUtf8(request.body).toUtf8() != request.body)
	{
		reply(failure(400, -32700, "Invalid UTF-8 JSON."));
		return;
	}
	if (!document.isObject())
	{
		reply(failure(400, -32600, "Expected a single JSON-RPC object; batches are unsupported."));
		return;
	}
	const auto message = document.object();
	const auto id = message.value("id");
	const bool hasId = message.contains("id");
	if (message.value("jsonrpc") != QJsonValue("2.0") || (hasId && !validId(id)))
	{
		reply(failure(400, -32600, "Invalid JSON-RPC envelope."));
		return;
	}
	const auto methodValue = message.value("method");
	if ((!methodValue.isString() || methodValue.toString().isEmpty())
		&& !(methodValue.isUndefined() && hasId && (message.contains("result") != message.contains("error"))))
	{
		reply(failure(400, -32600, "Invalid JSON-RPC method or response.", hasId ? id : QJsonValue(QJsonValue::Null)));
		return;
	}
	if (message.contains("params") && !message.value("params").isObject())
	{
		reply(failure(400, -32602, "MCP params must be an object.", hasId ? id : QJsonValue(QJsonValue::Null)));
		return;
	}
	const auto method = methodValue.toString();
	const auto params = message.value("params").toObject();
	if ((!methodValue.isUndefined() && (message.contains("result") || message.contains("error")))
		|| (method == "initialize" && !hasId))
	{
		reply(failure(400, -32600, "Invalid JSON-RPC request shape."));
		return;
	}
	if (method == "initialize" && hasId)
	{
		const auto client = params.value("clientInfo").toObject();
		if (!params.value("protocolVersion").isString() || params.value("protocolVersion").toString().isEmpty()
			|| !params.value("capabilities").isObject() || !client.value("name").isString()
			|| !client.value("version").isString())
		{
			reply(failure(200, -32602, "initialize requires protocolVersion, capabilities and clientInfo.", id));
			return;
		}
		reset();
		m_session = QUuid::createUuid().toString(QUuid::WithoutBraces);
		auto response = success(id,
			{{"protocolVersion", version()}, {"capabilities", QJsonObject{{"tools", QJsonObject{}}}},
				{"serverInfo", QJsonObject{{"name", "LMMS"}, {"version", LMMS_VERSION}}}});
		response.headers.insert("MCP-Session-Id", m_session.toUtf8());
		reply(response);
		return;
	}
	if (m_session.isEmpty() || request.headers.value("mcp-session-id") != m_session.toUtf8())
	{
		reply(failure(
			request.headers.contains("mcp-session-id") ? 404 : 400, -32600, "Initialize a local MCP session first."));
		return;
	}
	if (!hasId)
	{
		if (method == "notifications/initialized")
		{
			m_initialized = true;
		}
		if (method == "notifications/cancelled")
		{
			const auto cancelled = params.value("requestId");
			for (auto pending = m_queue.begin(); pending != m_queue.end(); ++pending)
			{
				if (pending->id != cancelled)
				{
					continue;
				}
				auto request = std::move(*pending);
				m_queue.erase(pending);
				request.reply(failure(200, -32800, "Cancelled before execution.", request.id));
				break;
			}
		}
		reply({202, {}, {}});
		return;
	}
	if (methodValue.isUndefined())
	{
		reply({202, {}, {}});
		return;
	}
	if (method == "ping")
	{
		reply(success(id, {}));
		return;
	}
	if (!m_initialized)
	{
		reply(failure(200, -32600, "Send notifications/initialized first.", id));
		return;
	}
	if (method == "tools/list")
	{
		if (params.contains("cursor"))
		{
			reply(failure(200, -32602, "This tool list is not paginated.", id));
			return;
		}
		reply(success(id, {{"tools", ToolRegistry::tools()}}));
		return;
	}
	if (method == "tools/call")
	{
		const auto name = params.value("name");
		if (!name.isString() || (params.contains("arguments") && !params.value("arguments").isObject()))
		{
			reply(failure(200, -32602, "tools/call requires a name and object arguments.", id));
			return;
		}
		bool exported = false;
		for (const auto& tool : ToolRegistry::tools())
		{
			if (tool.toObject().value("name") == name)
			{
				exported = true;
				break;
			}
		}
		if (!exported)
		{
			reply(failure(200, -32602, "Unknown MCP tool.", id));
			return;
		}
		for (const auto& pending : m_queue)
		{
			if (pending.id == id)
			{
				reply(failure(200, -32600, "Duplicate pending request id.", id));
				return;
			}
		}
		if (m_queue.size() >= 16)
		{
			reply(failure(503, -32603, "The MCP command queue is full.", id));
			return;
		}
		m_queue.push_back({id, name.toString(), params.value("arguments").toObject(), std::move(reply)});
		if (!m_scheduled && !m_executing)
		{
			m_scheduled = true;
			QTimer::singleShot(0, this, [this] { drain(); });
		}
		return;
	}
	reply(failure(200, -32601, "Unknown MCP method.", id));
}
}
