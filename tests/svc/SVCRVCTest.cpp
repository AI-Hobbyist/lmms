#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QtEndian>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <thread>

#include "svc_plugin.h"

namespace {
void check(bool value, const char* message)
{
	if (!value)
	{
		std::fprintf(stderr, "FAIL: %s\n", message);
		std::exit(1);
	}
}
QByteArray part(const char* kind, const QByteArray& body, const QByteArray& extra = {})
{
	return QByteArray("--test\r\nContent-Type: ")
		+ (QByteArray(kind) == "audio" ? "application/octet-stream" : "application/json") + "\r\nContent-Length: "
		+ QByteArray::number(body.size()) + "\r\nX-Event-Type: " + kind + "\r\n" + extra + "\r\n" + body + "\r\n";
}
QByteArray multipart(bool cleanup = true)
{
	return part("start",
			   R"({"request_id":"id","codec":"pcm_s16le","sample_rate":16000,"channels":1,"total":2,"parameters":{}})")
		+ part("audio", QByteArray(8, 0), "X-Chunk-Index: 1\r\nX-Sample-Offset: 0\r\nX-Sample-Count: 4\r\n")
		+ part("progress", R"({"request_id":"id","stage":"infer","status":"running","current":1,"total":2})")
		+ part("audio", QByteArray(8, 0), "X-Chunk-Index: 2\r\nX-Sample-Offset: 4\r\nX-Sample-Count: 4\r\n")
		+ part("progress", R"({"request_id":"id","stage":"infer","status":"running","current":2,"total":2})")
		+ part("done",
			QByteArray(
				R"({"request_id":"id","status":"completed","current":2,"total":2,"samples":8,"parameters":{},"gpu_cleanup_completed":)")
				+ (cleanup ? "true}" : "false}"))
		+ "--test--\r\n";
}
QByteArray fixture()
{
	return R"({"models":[{"model_id":"中文","display_name":"中文模型","weights":[{"weight_id":"f0.pth","usable":true,"supports_f0":true,"speaker_count":2,"sample_rate":40000,"index_rate_enabled":true,"default_index_rate":0.75,"compatible_indexes":["a.index","b.index"]},{"weight_id":"plain.pth","usable":true,"supports_f0":false,"speaker_count":1,"sample_rate":48000,"index_rate_enabled":false,"default_index_rate":0,"compatible_indexes":[]}],"indexes":[{"index_id":"a.index","usable":true},{"index_id":"b.index","usable":true}]}],"f0_methods":[{"id":"pm","available":true},{"id":"rmvpe","available":true},{"id":"fcpe","available":false,"reason":"missing dependency"}],"parameters":{"pitch_shift":{"type":"integer","default":0,"requires":"supports_f0"},"f0_method":{"enum":["pm","rmvpe","fcpe"],"default":"rmvpe","requires":"supports_f0"},"index_rate":{"type":"number","minimum":0,"maximum":1,"default":"0.75 if usable_index else 0"},"resample_sr":{"type":"integer","default":0,"allowed":"0 or 16000..48000"},"rms_mix_rate":{"type":"number","minimum":0,"maximum":1,"default":0.25},"protect":{"type":"number","minimum":0,"maximum":0.5,"default":0.33,"requires":"supports_f0"},"chunk_seconds":{"type":"number","minimum":1,"maximum":30,"default":5},"mode":{"enum":["chunked_file"],"default":"chunked_file"},"index_mode":{"enum":["auto","off","required"],"default":"auto"}},"limits":{"max_request_bytes":104857600,"max_audio_seconds":600,"upload_timeout":120,"inference_timeout":600},"auth_required":false})";
}
struct Server
{
	std::thread worker;
	uint16_t port;
	QByteArray request, input;
	Server(QByteArray body, int status = 200, bool stream = false, bool truncate = false, bool discoverFirst = false)
	{
		std::promise<uint16_t> ready;
		auto future = ready.get_future();
		worker = std::thread([this, body, status, stream, truncate, discoverFirst, ready = std::move(ready)]() mutable {
			QTcpServer server;
			check(server.listen(QHostAddress::LocalHost, 0), "fixture listen");
			ready.set_value(server.serverPort());
			const auto originalBody = body;
			const auto originalStatus = status;
			const auto originalStream = stream;
			const auto originalTruncate = truncate;
			for (int connection = 0; connection < (discoverFirst ? 2 : 1); ++connection)
			{
				body = discoverFirst && !connection ? fixture() : originalBody;
				status = discoverFirst && !connection ? 200 : originalStatus;
				stream = discoverFirst && !connection ? false : originalStream;
				truncate = discoverFirst && !connection ? false : originalTruncate;
				if (!server.waitForNewConnection(15000)) { return; }
				std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
				QByteArray buffer;
				const auto more = [&] {
					if (!socket->bytesAvailable() && !socket->waitForReadyRead(5000)) { return false; }
					buffer += socket->readAll();
					return true;
				};
				while (!buffer.contains("\r\n\r\n"))
				{
					if (!more()) { return; }
				}
				const auto end = buffer.indexOf("\r\n\r\n") + 4;
				request = buffer.left(end);
				buffer.remove(0, end);
				if (request.startsWith("POST"))
				{
					for (;;)
					{
						while (!buffer.contains("\r\n"))
						{
							if (!more()) { return; }
						}
						const auto line = buffer.indexOf("\r\n");
						bool valid;
						const auto count = buffer.left(line).toInt(&valid, 16);
						check(valid && count <= SVC_MAX_FEED && count >= 0, "bounded raw chunk upload");
						buffer.remove(0, line + 2);
						while (buffer.size() < count + 2)
						{
							if (!more()) { return; }
						}
						input += buffer.left(count);
						buffer.remove(0, count + 2);
						if (!count) { break; }
					}
				}
				QByteArray header = "HTTP/1.1 " + QByteArray::number(status)
					+ " Result\r\nConnection: close\r\nX-Request-ID: id\r\nContent-Type: "
					+ (stream ? "multipart/mixed; boundary=test" : "application/json") + "\r\n";
				if (stream) { header += "Transfer-Encoding: chunked\r\n\r\n"; }
				else
				{
					header += "Content-Length: " + QByteArray::number(body.size() + (truncate ? 100 : 0)) + "\r\n\r\n";
				}
				socket->write(header);
				socket->waitForBytesWritten(1000);
				for (int position = 0; position < body.size();)
				{
					if (socket->state() != QAbstractSocket::ConnectedState) { break; }
					const auto fragment = body.mid(position, 17);
					position += fragment.size();
					const auto encoded
						= stream ? QByteArray::number(fragment.size(), 16) + "\r\n" + fragment + "\r\n" : fragment;
					socket->write(encoded);
					socket->waitForBytesWritten(1000);
				}
				if (stream && !truncate)
				{
					socket->write("0\r\n\r\n");
					socket->waitForBytesWritten(1000);
				}
				socket->disconnectFromHost();
				if (socket->state() != QAbstractSocket::UnconnectedState) { socket->waitForDisconnected(1000); }
			}
		});
		port = future.get();
	}
	~Server() { join(); }
	void join()
	{
		if (worker.joinable()) { worker.join(); }
	}
	QByteArray address() const { return "http://127.0.0.1:" + QByteArray::number(port); }
};
struct Observer
{
	QByteArray input = QByteArray(200000, 'a'), error;
	size_t position = 0, audio = 0, done = 0, reads = 0;
	uint64_t samples = 0;
	uint32_t rate = 0;
	bool early = false, cancel = false;
	static int64_t read(void* opaque, uint8_t* out, size_t capacity)
	{
		auto& self = *static_cast<Observer*>(opaque);
		check(capacity <= SVC_MAX_FEED, "input cap");
		++self.reads;
		const auto count = std::min<size_t>(capacity, self.input.size() - self.position);
		std::memcpy(out, self.input.constData() + self.position, count);
		self.position += count;
		return count;
	}
	static int event(void* opaque, const svc_event* event)
	{
		auto& self = *static_cast<Observer*>(opaque);
		check(event->generation_id == 9 && event->segment_id == 2, "identity preserved");
		if (event->type == SVC_AUDIO)
		{
			++self.audio;
			self.samples += event->sample_count;
			self.rate = event->sample_rate;
			self.early |= !self.done;
			return self.cancel;
		}
		if (event->type == SVC_DONE) { ++self.done; }
		if (event->type == SVC_ERROR)
		{
			self.error.append(reinterpret_cast<const char*>(event->bytes), event->byte_count);
		}
		return 0;
	}
};
QByteArray selection(const QString& model = "中文", const QString& weight = "plain.pth")
{
	return QJsonDocument(
		QJsonObject{{"engine_id", "RVC"}, {"model_id", model}, {"weight_id", weight}, {"speaker_id", "0"},
			{"parameters",
				QJsonObject{{"pitch_shift", 12}, {"f0_method", "fcpe"}, {"protect", .33}, {"index_rate", .8},
					{"index_mode", "auto"}, {"index_id", "__automatic__"}, {"resample_sr", 0}, {"chunk_seconds", 1}}}})
		.toJson(QJsonDocument::Compact);
}
svc_status run(const svc_plugin* plugin, void* context, Observer& observer, const QByteArray& json)
{
	svc_request request{sizeof(request), SVC_ABI_VERSION, 9, 2, json.constData(), Observer::read, &observer,
		Observer::event, &observer};
	void* job = plugin->engine->start(context, &request);
	check(job != nullptr, "start request");
	svc_status status = SVC_OK;
	for (int pumps = 0; status == SVC_OK && pumps < 20000; ++pumps)
	{
		status = plugin->engine->pump(job);
	}
	if (status == SVC_OK) { plugin->engine->cancel(job); }
	const auto reads = observer.reads;
	plugin->engine->cancel(job);
	plugin->engine->cancel(job);
	plugin->engine->destroy(job);
	check(reads == observer.reads, "no reads after terminal/cancel");
	return status;
}
} // namespace
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	check(argc >= 2, "module path required");
	QLibrary module(QString::fromLocal8Bit(argv[1]));
	check(module.load(), "load RVC module");
	const auto entry = reinterpret_cast<svc_plugin_entry>(module.resolve("svc_plugin_entry_v1"));
	check(entry && !entry(0), "ABI negotiation");
	const auto* plugin = entry(SVC_ABI_VERSION);
	check(plugin && plugin->engine, "module entry");
	if (argc > 2 && QByteArray(argv[2]) == "--live")
	{
		void* context = plugin->create_context("http://127.0.0.1:8000", "");
		const auto* json = plugin->engine->capabilities(context);
		check(json, plugin->error(context));
		const auto caps = QJsonDocument::fromJson(json).object();
		std::printf("Live catalog models: %lld, auth_required: %d\n", caps.value("models").toArray().size(),
			caps.value("auth_required").toBool());
		QJsonObject model;
		for (const auto& value : caps.value("models").toArray())
		{
			if (value.toObject().value("id") == "芙宁娜") { model = value.toObject(); }
		}
		check(!model.isEmpty(), "Chinese model discovery");
		for (const auto& method : {"pm", "rmvpe", "fcpe", "indexed", "no-index"})
		{
			auto chosen = model;
			if (QByteArray(method) == "no-index")
			{
				for (const auto& value : caps.value("models").toArray())
				{
					if (value.toObject().value("id") == "youzhanv2-xi") { chosen = value.toObject(); }
				}
			}
			const auto weight = chosen.value("weights").toArray().first().toObject();
			const bool indexed = QByteArray(method) == "indexed";
			Observer observer;
			const uint32_t frames = 16000 * 4;
			observer.input = QByteArray(44 + frames * 2, 0);
			auto& wav = observer.input;
			wav.replace(0, 4, "RIFF");
			qToLittleEndian<uint32_t>(36 + frames * 2, wav.data() + 4);
			wav.replace(8, 8, "WAVEfmt ");
			qToLittleEndian<uint32_t>(16, wav.data() + 16);
			qToLittleEndian<uint16_t>(1, wav.data() + 20);
			qToLittleEndian<uint16_t>(1, wav.data() + 22);
			qToLittleEndian<uint32_t>(16000, wav.data() + 24);
			qToLittleEndian<uint32_t>(32000, wav.data() + 28);
			qToLittleEndian<uint16_t>(2, wav.data() + 32);
			qToLittleEndian<uint16_t>(16, wav.data() + 34);
			wav.replace(36, 4, "data");
			qToLittleEndian<uint32_t>(frames * 2, wav.data() + 40);
			for (uint32_t i = 0; i < frames; ++i)
			{
				qToLittleEndian<int16_t>(
					int16_t(6000 * std::sin(i * 6.28318530718 * 220 / 16000)), wav.data() + 44 + i * 2);
			}
			const auto request = QJsonDocument(
				QJsonObject{{"model_id", chosen.value("id")}, {"weight_id", weight.value("id")},
					{"speaker_id", indexed ? "101" : "0"},
					{"parameters",
						QJsonObject{{"f0_method", indexed || QByteArray(method) == "no-index" ? "rmvpe" : method},
							{"pitch_shift", indexed ? 3 : 0}, {"index_rate", indexed ? .75 : 0},
							{"index_mode", indexed ? "auto" : "off"}, {"chunk_seconds", 1},
							{"resample_sr", indexed ? 32000 : 0}, {"rms_mix_rate", indexed ? .7 : .25},
							{"protect", indexed ? .1 : .33}}}})
									 .toJson(QJsonDocument::Compact);
			const auto status = run(plugin, context, observer, request);
			std::printf("Live %s: status=%d audio_parts=%zu first_before_done=%d error=%s\n", method, status,
				observer.audio, observer.early, observer.error.constData());
			std::fflush(stdout);
			check(status == SVC_COMPLETE && observer.audio >= 2 && observer.early && observer.done == 1,
				"live streamed F0 inference");
			check(observer.rate == (indexed ? 32000 : weight.value("sample_rate").toInt())
					&& observer.samples == observer.rate * 4ULL,
				"live resampling and sample totals");
		}
		plugin->destroy_context(context);
		std::puts("PASS live RVC");
		return 0;
	}
	Server discovery(fixture());
	const auto address = discovery.address();
	void* context = plugin->create_context(address.constData(), "");
	const auto* capabilities = plugin->engine->capabilities(context);
	check(capabilities, plugin->error(context));
	discovery.join();
	check(!discovery.request.contains("Authorization"), "empty token omits auth");
	const auto caps = QJsonDocument::fromJson(capabilities).object();
	const auto model = caps.value("models").toArray().first().toObject();
	check(model.value("weights").toArray().size() == 2
			&& model.value("weights").toArray()[0].toObject().value("speakers").toArray().size() == 1
			&& model.value("weights").toArray()[1].toObject().value("speakers").toArray().size() == 1,
		"RVC defaults to speaker 0 regardless of reported speaker count");
	check(model.value("fixed_speaker_id") == "0", "fixed default speaker");
	for (const auto& value : model.value("parameters").toArray())
	{
		const auto parameter = value.toObject();
		check(
			parameter.value("type") == "enum" || parameter.value("control") == "slider", "RVC numeric slider metadata");
	}
	plugin->destroy_context(context);
	for (const auto status : {200, 401, 413, 422, 429, 503, 408, 302})
	{
		Server response(status == 200 ? multipart() : QByteArray("{\"error\":\"fixture-secret\"}"), status,
			status == 200, false, true);
		context = plugin->create_context(response.address().constData(), "fixture-secret");
		check(plugin->engine->capabilities(context), "authenticated discovery");
		Observer observer;
		const auto terminal = run(plugin, context, observer, selection());
		response.join();
		check(response.request.contains("Authorization: Bearer fixture-secret"), "auth only explicitly supplied");
		check(response.request.contains("Transfer-Encoding: chunked")
				&& response.request.contains("Content-Type: audio/wav"),
			"raw HTTP upload");
		check(response.input == observer.input && observer.reads >= 4, "all input sent in bounded blocks");
		check(!response.request.contains("pitch_shift") && !response.request.contains("f0_method")
				&& !response.request.contains("protect"),
			"non F0 parameters omitted");
		check(response.request.contains("index_rate=0") && !response.request.contains("index_id="),
			"no index forces zero");
		check(response.request.contains("%E4%B8%AD%E6%96%87"), "Chinese ID URL encoding");
		check(response.request.contains("speaker_id=0"), "single model speaker request");
		if (status == 200)
		{
			check(terminal == SVC_COMPLETE && observer.audio == 2 && observer.early && observer.done == 1,
				"fragmented multipart completes after first audio");
		}
		else
		{
			check(terminal == SVC_FAILED && !observer.error.contains("fixture-secret") && !observer.error.isEmpty(),
				"HTTP error mapped and token redacted");
		}
		plugin->destroy_context(context);
		std::printf("HTTP fixture %d PASS\n", status);
	}
	for (int variant = 0; variant < 4; ++variant)
	{
		auto body = multipart(variant != 2);
		if (variant == 3) { body.replace("\"id\"", "\"wrong\""); }
		Server response(body, 200, true, variant == 0, true);
		context = plugin->create_context(response.address().constData(), "");
		check(plugin->engine->capabilities(context), "variant discovery");
		Observer observer;
		observer.cancel = variant == 1;
		const auto status = run(plugin, context, observer, selection());
		response.join();
		check(status != SVC_COMPLETE && (variant == 3 || observer.audio >= 1),
			"truncation/cancel/missing cleanup cannot complete");
		if (variant == 3)
		{
			check(status == SVC_FAILED && observer.audio == 0, "HTTP identity mismatch rejected before audio");
		}
		if (variant == 1) { check(status == SVC_CANCELLED, "callback cancel terminal"); }
		plugin->destroy_context(context);
	}
	std::puts("PASS RVC discovery, streaming, errors and cancellation fixtures");
}
