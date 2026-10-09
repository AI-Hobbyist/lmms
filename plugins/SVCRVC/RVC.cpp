#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

#include "Http.h"
#include "svc_plugin.h"

namespace svc_rvc {
namespace {
struct Context
{
	QString address;
	QByteArray token, capabilities, error;
};
QJsonArray speakers(int count)
{
	QJsonArray result;
	for (int i = 0; i < count; ++i)
	{
		result.append(QJsonObject{{"id", QString::number(i)}, {"name", QString("Speaker %1").arg(i)}});
	}
	return result;
}
QJsonObject normalize(const QJsonObject& init)
{
	if (!init.value("models").isArray() || !init.value("parameters").isObject() || !init.value("limits").isObject())
	{
		return {};
	}
	QJsonArray parameters;
	const auto declarations = init.value("parameters").toObject();
	for (auto it = declarations.begin(); it != declarations.end(); ++it)
	{
		if (it.key() == "speaker_id") { continue; }
		auto p = it.value().toObject();
		p.insert("id", it.key());
		p.insert("name", it.key());
		p.insert("unit", "");
		p.insert("scope", "request");
		if (it.key() == "pitch_shift") { p.insert("unit", "semitones"); }
		if (it.key() == "chunk_seconds") { p.insert("unit", "s"); }
		if (it.key() == "resample_sr")
		{
			p.insert("unit", "Hz (0 = model rate)");
			p.insert("minimum", 0);
			p.insert("maximum", 48000);
			p.insert("excluded_range", QJsonArray{1, 15999});
		}
		if (p.value("requires") == "supports_f0") { p.insert("enabled_when", QJsonObject{{"supports_f0", true}}); }
		p.remove("requires");
		p.remove("allowed");
		if (it.key() == "index_rate")
		{
			p.insert("default", 0);
			p.insert("default_from", "default_index_rate");
			p.insert("enabled_when",
				QJsonObject{{"index_rate_enabled", true}, {"index_mode", QJsonArray{"auto", "required"}}});
		}
		if (p.contains("enum"))
		{
			QJsonArray options;
			for (const auto& value : p.value("enum").toArray())
			{
				QJsonObject option{{"id", value}, {"name", value}, {"available", true}};
				if (it.key() == "index_mode" && value == "required")
				{
					option.insert("enabled_when", QJsonObject{{"index_rate_enabled", true}});
					option.insert("reason", "This weight has no usable index");
				}
				if (it.key() == "f0_method")
				{
					option.insert("available", false);
					option.insert("reason", "Backend did not report availability");
					for (const auto& method : init.value("f0_methods").toArray())
					{
						const auto m = method.toObject();
						if (m.value("id") == value)
						{
							option.insert("available", m.value("available").toBool());
							option.insert("reason", m.value("reason").toString("Unavailable"));
						}
					}
				}
				options.append(option);
			}
			p.insert("type", "enum");
			p.insert("options", options);
			p.remove("enum");
		}
		if (p.value("type") == "number") { p.insert("step", .01); }
		else if (p.value("type") == "integer") { p.insert("step", 1); }
		parameters.append(p);
	}
	QJsonArray models;
	for (const auto& entry : init.value("models").toArray())
	{
		const auto source = entry.toObject();
		QJsonArray weights, indexes;
		for (const auto& value : source.value("weights").toArray())
		{
			auto weight = value.toObject();
			const auto count = weight.value("speaker_count").toInt();
			if (count < 1 || count > 4096) { weight.insert("usable", false); }
			weight.insert("id", weight.value("weight_id"));
			weight.insert("name", weight.value("weight_id"));
			weight.insert("available", weight.value("usable").toBool());
			weight.insert("reason", "Weight is unusable or speaker metadata is unsupported");
			weight.insert("speakers", speakers(std::clamp(count, 0, 4096)));
			weight.insert("automatic_index", weight.value("compatible_indexes").toArray().size() <= 1);
			weights.append(weight);
		}
		indexes.append(QJsonObject{{"id", "__automatic__"}, {"name", "Automatic / no index"}, {"available", true},
			{"enabled_when", QJsonObject{{"automatic_index", true}}},
			{"reason", "Multiple compatible indexes require an explicit selection"}});
		for (const auto& value : source.value("indexes").toArray())
		{
			const auto index = value.toObject();
			QJsonArray compatible;
			for (const auto& weight : weights)
			{
				const auto w = weight.toObject();
				if (w.value("compatible_indexes").toArray().contains(index.value("index_id")))
				{
					compatible.append(w.value("id"));
				}
			}
			indexes.append(QJsonObject{{"id", index.value("index_id")}, {"name", index.value("index_id")},
				{"available", index.value("usable").toBool()}, {"reason", "Index is unusable"},
				{"enabled_when", QJsonObject{{"weight_id", compatible}}}});
		}
		auto modelParameters = parameters;
		modelParameters.append(QJsonObject{{"id", "index_id"}, {"name", "Index"}, {"type", "enum"}, {"unit", ""},
			{"scope", "request"}, {"default", "__automatic__"}, {"options", indexes},
			{"enabled_when",
				QJsonObject{{"index_rate_enabled", true}, {"index_mode", QJsonArray{"auto", "required"}}}}});
		QJsonObject model{{"id", source.value("model_id")}, {"name", source.value("display_name")},
			{"weights", weights},
			{"speakers", weights.size() == 1 ? weights.first().toObject().value("speakers") : QJsonValue(QJsonArray{})},
			{"parameters", modelParameters}, {"available", !weights.isEmpty()}};
		model.insert("require_weight_selection", weights.size() > 1);
		models.append(model);
	}
	const auto limits = init.value("limits").toObject();
	return {{"schema_version", 1}, {"engine_id", "RVC"}, {"models", models}, {"parameters", parameters},
		{"auth_required", init.value("auth_required")},
		{"limits",
			QJsonObject{{"min_seconds", .1}, {"max_seconds", limits.value("max_audio_seconds")},
				{"max_upload_bytes", limits.value("max_request_bytes")},
				{"timeout_ms",
					(limits.value("upload_timeout").toDouble() + limits.value("inference_timeout").toDouble())
						* 1000}}}};
}
const char* capabilities(void* opaque)
{
	auto& context = *static_cast<Context*>(opaque);
	try
	{
		Http http(endpoint(context.address, "/api/v1/init"), context.token, false, 10000);
		QByteArray body;
		while (http.pump({}, [&](const uint8_t* data, size_t size) {
			if (body.size() + size > SVC_MAX_PART) { return false; }
			body.append(reinterpret_cast<const char*>(data), size);
			return true;
		}))
		{
		}
		if (!http.error().isEmpty() || http.status() != 200)
		{
			context.error = QString("Discovery HTTP %1: %2").arg(http.status()).arg(http.error()).toUtf8();
			return nullptr;
		}
		const auto root = normalize(QJsonDocument::fromJson(body).object());
		context.capabilities = QJsonDocument(root).toJson(QJsonDocument::Compact);
		char error[512]{};
		if (root.isEmpty()
			|| svc_validate_capabilities(
				   context.capabilities.constData(), context.capabilities.size(), error, sizeof(error))
				!= SVC_OK)
		{
			context.error = error[0] ? QByteArray(error) : QByteArray("Invalid RVC discovery document");
			return nullptr;
		}
		context.error.clear();
		return context.capabilities.constData();
	}
	catch (...)
	{
		context.error = "RVC discovery failed";
		return nullptr;
	}
}
struct Job
{
	svc_request request;
	std::unique_ptr<Http> http;
	svc_stream* stream = nullptr;
	QByteArray errorBody, token;
	svc_status terminal = SVC_OK;
	uint64_t reportedBytes = UINT64_MAX;
	bool reportedUploaded = false;
	~Job() { svc_stream_destroy(stream); }
	void error(const QString& text) noexcept
	{
		terminal = SVC_FAILED;
		try
		{
			auto safe = text;
			if (!token.isEmpty()) { safe.replace(QString::fromUtf8(token), "[redacted]"); }
			const auto json = QJsonDocument(QJsonObject{{"message", safe}, {"http_status", http->status()},
												{"request_id", QString::fromUtf8(http->header("x-request-id"))}})
								  .toJson(QJsonDocument::Compact);
			svc_event event{};
			event.size = sizeof(event);
			event.type = SVC_ERROR;
			event.generation_id = request.generation_id;
			event.segment_id = request.segment_id;
			event.bytes = reinterpret_cast<const uint8_t*>(json.constData());
			event.byte_count = json.size();
			request.callback(request.callback_user, &event);
		}
		catch (...)
		{
		}
	}
	static int deliver(void* user, const svc_event* event)
	{
		auto& job = *static_cast<Job*>(user);
		try
		{
			if (!event->request_id || QByteArray(event->request_id) != job.http->header("x-request-id"))
			{
				job.error("Mismatched HTTP and multipart request ID");
				return 1;
			}
			if (event->type == SVC_ERROR)
			{
				job.error(QString::fromUtf8(reinterpret_cast<const char*>(event->bytes), event->byte_count));
				return 0;
			}
			return job.request.callback(job.request.callback_user, event);
		}
		catch (...)
		{
			job.error("SVC event callback failed");
			return 1;
		}
	}
};
void* start(void* opaque, const svc_request* request)
{
	if (!opaque || !request || request->size < sizeof(svc_request) || request->abi_version != SVC_ABI_VERSION
		|| !request->read || !request->callback || !request->selection_json)
	{
		return nullptr;
	}
	try
	{
		auto& context = *static_cast<Context*>(opaque);
		auto job = std::make_unique<Job>();
		job->request = *request;
		job->request.selection_json = nullptr;
		job->token = context.token;
		const auto selection = QJsonDocument::fromJson(request->selection_json).object();
		QUrl url = endpoint(context.address, "/api/v1/infer");
		QUrlQuery query;
		for (const auto& key : {"model_id", "weight_id", "speaker_id"})
		{
			if (!selection.value(key).isString() || selection.value(key).toString().isEmpty()) { return nullptr; }
			query.addQueryItem(key, selection.value(key).toString());
		}
		auto parameters = selection.value("parameters").toObject();
		const auto caps = QJsonDocument::fromJson(context.capabilities).object();
		QJsonObject weight;
		for (const auto& model : caps.value("models").toArray())
		{
			if (model.toObject().value("id") != selection.value("model_id")) { continue; }
			for (const auto& w : model.toObject().value("weights").toArray())
			{
				if (w.toObject().value("id") == selection.value("weight_id")) { weight = w.toObject(); }
			}
		}
		if (weight.isEmpty() || !weight.value("available").toBool()) { return nullptr; }
		if (!weight.value("supports_f0").toBool())
		{
			for (const auto& key : {"pitch_shift", "f0_method", "protect"})
			{
				parameters.remove(key);
			}
		}
		if (!weight.value("index_rate_enabled").toBool() || parameters.value("index_mode") == "off")
		{
			parameters.insert("index_rate", 0);
			parameters.remove("index_id");
		}
		if (parameters.value("index_id") == "__automatic__")
		{
			parameters.remove("index_id");
			if (parameters.value("index_rate").toDouble(weight.value("default_index_rate").toDouble()) > 0
				&& weight.value("compatible_indexes").toArray().size() != 1)
			{
				return nullptr;
			}
		}
		const auto rate = parameters.value("resample_sr").toInt();
		if (rate && (rate < 16000 || rate > 48000)) { return nullptr; }
		const auto declarations = caps.value("parameters").toArray();
		for (auto it = parameters.begin(); it != parameters.end(); ++it)
		{
			bool known = it.key() == "index_id";
			for (const auto& declaration : declarations)
			{
				known |= declaration.toObject().value("id") == it.key();
			}
			if (!known) { return nullptr; }
			query.addQueryItem(it.key(),
				it.value().isString() ? it.value().toString() : QString::number(it.value().toDouble(), 'g', 16));
		}
		url.setQuery(query);
		job->http = std::make_unique<Http>(url, context.token, true,
			std::clamp(caps.value("limits").toObject().value("timeout_ms").toInt(720000), 1000, 3600000));
		return job.release();
	}
	catch (...)
	{
		return nullptr;
	}
}
svc_status pump(void* opaque)
{
	auto& job = *static_cast<Job*>(opaque);
	if (job.terminal != SVC_OK) { return job.terminal; }
	try
	{
		job.http->pump([&](uint8_t* data, size_t size) { return job.request.read(job.request.input_user, data, size); },
			[&](const uint8_t* data, size_t size) {
				if (job.http->status() != 200)
				{
					if (job.errorBody.size() + size > SVC_MAX_HEADER) { return false; }
					job.errorBody.append(reinterpret_cast<const char*>(data), size);
					return true;
				}
				if (!job.stream)
				{
					const auto content = QString::fromLatin1(job.http->header("content-type"));
					const auto match
						= QRegularExpression("^multipart/mixed\\s*;\\s*boundary=(?:\"([^\"]+)\"|([^;\\s]+))\\s*$",
							QRegularExpression::CaseInsensitiveOption)
							  .match(content);
					if (!match.hasMatch()) { return false; }
					const auto boundary
						= (match.captured(1).isEmpty() ? match.captured(2) : match.captured(1)).toUtf8();
					svc_stream_config config{sizeof(config), SVC_ABI_VERSION, job.request.generation_id,
						job.request.segment_id, boundary.constData(), Job::deliver, &job, 1};
					job.stream = svc_stream_create(&config);
					if (!job.stream) { return false; }
				}
				const auto status = svc_stream_feed(job.stream, data, size);
				if (status != SVC_OK)
				{
					if (job.terminal == SVC_OK) { job.terminal = status; }
					if (status == SVC_PROTOCOL) { job.error(QString::fromUtf8(svc_stream_error(job.stream))); }
					return false;
				}
				return true;
			});
		if (job.terminal != SVC_OK) { return job.terminal; }
		if (job.http->finished())
		{
			if (!job.http->error().isEmpty()) { job.error(job.http->error()); }
			else if (job.http->status() != 200)
			{
				job.error(QString("HTTP %1: %2").arg(job.http->status()).arg(QString::fromUtf8(job.errorBody)));
			}
			else if (!job.stream) { job.error("Missing multipart response"); }
			else
			{
				job.terminal = svc_stream_finish(job.stream);
				if (job.terminal != SVC_COMPLETE) { job.error(QString::fromUtf8(svc_stream_error(job.stream))); }
			}
		}
		else if (!job.stream && (job.reportedBytes != job.http->sent() || job.reportedUploaded != job.http->uploaded()))
		{
			job.reportedBytes = job.http->sent();
			job.reportedUploaded = job.http->uploaded();
			const auto json = QJsonDocument(QJsonObject{{"stage", job.http->uploaded() ? "preprocessing" : "upload"},
												{"uploaded_bytes", double(job.reportedBytes)}})
								  .toJson(QJsonDocument::Compact);
			svc_event event{};
			event.size = sizeof(event);
			event.type = SVC_PROGRESS;
			event.generation_id = job.request.generation_id;
			event.segment_id = job.request.segment_id;
			event.bytes = reinterpret_cast<const uint8_t*>(json.constData());
			event.byte_count = json.size();
			if (job.request.callback(job.request.callback_user, &event))
			{
				job.http->cancel();
				job.terminal = SVC_CANCELLED;
			}
		}
	}
	catch (...)
	{
		job.error("RVC transport failed");
	}
	return job.terminal;
}
void cancel(void* opaque)
{
	auto& job = *static_cast<Job*>(opaque);
	if (job.terminal == SVC_OK)
	{
		job.http->cancel();
		svc_stream_cancel(job.stream);
		job.terminal = SVC_CANCELLED;
	}
}
void destroy(void* opaque)
{ delete static_cast<Job*>(opaque); }
const svc_engine engine{sizeof(svc_engine), SVC_ABI_VERSION, "RVC", capabilities, start, pump, cancel, destroy};
void* createContext(const char* address, const char* token)
{
	try
	{
		return new Context{QString::fromUtf8(address ? address : ""), QByteArray(token ? token : ""), {}, {}};
	}
	catch (...)
	{
		return nullptr;
	}
}
void destroyContext(void* context)
{ delete static_cast<Context*>(context); }
const char* error(void* context)
{ return static_cast<Context*>(context)->error.constData(); }
const svc_plugin plugin{
	sizeof(svc_plugin), SVC_ABI_VERSION, "RVC", "http://127.0.0.1:8000", &engine, createContext, destroyContext, error};
} // namespace
} // namespace svc_rvc

#ifdef _WIN32
#define SVC_EXPORT __declspec(dllexport)
#else
#define SVC_EXPORT __attribute__((visibility("default")))
#endif
extern "C" SVC_EXPORT const svc_plugin* svc_plugin_entry_v1(uint32_t version)
{ return version == SVC_ABI_VERSION ? &svc_rvc::plugin : nullptr; }

#ifdef SVC_LMMS_MODULE
#include "Plugin.h"
extern "C" {
SVC_EXPORT lmms::Plugin::Descriptor svcrvc_plugin_descriptor = {"svcrvc", "RVC", "Singing voice conversion API adapter",
	"LMMS contributors", 0x0100, lmms::Plugin::Type::SVC, nullptr, nullptr, nullptr};
SVC_EXPORT lmms::Plugin* lmms_plugin_main(lmms::Model*, void*)
{ return nullptr; }
}
#endif
