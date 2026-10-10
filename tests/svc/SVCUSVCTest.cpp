#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QtEndian>
#include <cstdio>
#include <cstring>
#include <future>
#include <thread>

#include "svc_plugin.h"

namespace {
void check(bool valid, const char* message)
{
	if (!valid)
	{
		std::fprintf(stderr, "FAIL: %s\n", message);
		std::exit(1);
	}
}
QByteArray wave(uint32_t rate, const QByteArray& pcm)
{
	QByteArray bytes(44, 0);
	bytes.replace(0, 4, "RIFF");
	qToLittleEndian<uint32_t>(36 + pcm.size(), bytes.data() + 4);
	bytes.replace(8, 8, "WAVEfmt ");
	qToLittleEndian<uint32_t>(16, bytes.data() + 16);
	qToLittleEndian<uint16_t>(1, bytes.data() + 20);
	qToLittleEndian<uint16_t>(1, bytes.data() + 22);
	qToLittleEndian<uint32_t>(rate, bytes.data() + 24);
	qToLittleEndian<uint32_t>(rate * 2, bytes.data() + 28);
	qToLittleEndian<uint16_t>(2, bytes.data() + 32);
	qToLittleEndian<uint16_t>(16, bytes.data() + 34);
	bytes.replace(36, 4, "data");
	qToLittleEndian<uint32_t>(pcm.size(), bytes.data() + 40);
	return bytes + pcm;
}
QByteArray models()
{
	return R"([{"id":"ddsp6/测试 +&模型","backend":"ddsp6","name":"测试模型","status":"ready","multi_speaker":true,"default_speaker":null,"speakers":[{"id":0,"name":"测试甲"},{"id":1,"name":"测试乙"}]}])";
}
QByteArray settings()
{
	return R"({"model":"ddsp6/测试 +&模型","backend":"ddsp6","device":"cuda","speakers":[{"id":0,"name":"测试甲"},{"id":1,"name":"测试乙"}],"indexes":[],"clusters":[],"enhancers":[],"vocoders":["default"],"parameters":[
	{"key":"speaker","type":"speaker","default":null,"stage":"convert","condition":"always"},
	{"key":"trans","type":"number","default":0,"minimum":-48,"maximum":48,"stage":"convert","condition":"always"},
	{"key":"resample_sr","type":"integer","default":null,"nullable":true,"minimum":8000,"maximum":192000,"stage":"convert","condition":"always"},
	{"key":"f0_method","type":"string","default":null,"nullable":true,"enum":["fcpe","rmvpe"],"stage":"convert","condition":"always"},
	{"key":"compile_graph","type":"boolean","default":false,"stage":"construct","condition":"always"},
	{"key":"target_loudness","type":"number","default":null,"nullable":true,"minimum":-70,"maximum":0,"stage":"convert","condition":"always"},
	{"key":"restore_loudness","type":"boolean","default":true,"stage":"convert","condition":"target_loudness is set"}
	]})";
}
struct Server
{
	std::thread worker;
	uint16_t port;
	QByteArray request, input;
	Server(QByteArray result, int status = 200, bool truncated = false, bool chunked = true, bool discoveryOnly = false,
		bool badSettings = false)
	{
		std::promise<uint16_t> ready;
		auto future = ready.get_future();
		worker = std::thread([this, result, status, truncated, chunked, discoveryOnly, badSettings,
								 ready = std::move(ready)]() mutable {
			QTcpServer server;
			check(server.listen(QHostAddress::LocalHost, 0), "fixture listen");
			ready.set_value(server.serverPort());
			for (int connection = 0; connection < (discoveryOnly ? 2 : 3); ++connection)
			{
				check(server.waitForNewConnection(15000), "fixture connection timeout");
				std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
				QByteArray buffer;
				const auto more = [&] {
					if (!socket->bytesAvailable() && !socket->waitForReadyRead(15000)) { return false; }
					buffer += socket->readAll();
					return true;
				};
				while (!buffer.contains("\r\n\r\n"))
				{
					check(more(), "request header");
				}
				const auto end = buffer.indexOf("\r\n\r\n") + 4;
				request = buffer.left(end);
				buffer.remove(0, end);
				check(request.contains("Authorization: Bearer test-secret"), "Bearer authentication");
				const QUrl url("http://localhost" + QString::fromUtf8(request.split(' ')[1]));
				const QUrlQuery query(url);
				if (connection == 0) { check(url.path() == "/model", "model route"); }
				else
				{
					check(query.queryItemValue("model", QUrl::FullyDecoded) == QString::fromUtf8("ddsp6/测试 +&模型"),
						"encoded Chinese, plus, ampersand, space model identity");
					if (connection == 1)
					{
						check(
							url.path() == "/settings" && query.queryItemValue("backend") == "ddsp6", "settings route");
					}
					else
					{
						check(url.path() == "/infer" && request.startsWith("POST ")
								&& request.contains("Content-Type: audio/wav"),
							"raw WAV upload route");
						for (;;)
						{
							while (!buffer.contains("\r\n"))
							{
								check(more(), "upload chunk header");
							}
							const auto line = buffer.indexOf("\r\n");
							bool valid;
							const auto count = buffer.left(line).toInt(&valid, 16);
							check(valid && count >= 0 && count <= SVC_MAX_FEED, "bounded upload");
							buffer.remove(0, line + 2);
							while (buffer.size() < count + 2)
							{
								check(more(), "upload bytes");
							}
							input += buffer.left(count);
							buffer.remove(0, count + 2);
							if (!count) { break; }
						}
					}
				}
				const bool infer = connection == 2;
				const auto body = connection == 0 ? models()
					: connection == 1			  ? (badSettings ? QByteArray("{}") : settings())
												  : result;
				const auto code = infer ? status : 200;
				const bool stream = infer && chunked;
				QByteArray header = "HTTP/1.1 " + QByteArray::number(code)
					+ " Result\r\nConnection: close\r\nContent-Type: "
					+ (infer && status == 200 ? "audio/wav\r\nX-USVC-Sample-Rate: 32000" : "application/json") + "\r\n";
				if (stream) { header += "Transfer-Encoding: chunked\r\n\r\n"; }
				else
				{
					header += "Content-Length: " + QByteArray::number(body.size() + (infer && truncated ? 100 : 0))
						+ "\r\n\r\n";
				}
				socket->write(header);
				socket->waitForBytesWritten(1000);
				for (int position = 0; position < body.size(); position += 113)
				{
					const auto fragment = body.mid(position, 113);
					socket->write(
						stream ? QByteArray::number(fragment.size(), 16) + "\r\n" + fragment + "\r\n" : fragment);
					if (!socket->waitForBytesWritten(1000)) { break; }
				}
				if (stream && !truncated)
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
	QByteArray source, pcm, error;
	qint64 position = 0;
	uint32_t rate = 0;
	uint64_t chunks = 0, samples = 0;
	int starts = 0, done = 0, reads = 0, events = 0;
	bool cancelAudio = false;
	static int64_t read(void* opaque, uint8_t* out, size_t capacity)
	{
		auto& self = *static_cast<Observer*>(opaque);
		check(capacity <= SVC_MAX_FEED, "input capacity");
		++self.reads;
		const auto count = std::min<qint64>(capacity, self.source.size() - self.position);
		std::memcpy(out, self.source.constData() + self.position, count);
		self.position += count;
		return count;
	}
	static int event(void* opaque, const svc_event* event)
	{
		auto& self = *static_cast<Observer*>(opaque);
		++self.events;
		check(event->generation_id == 9 && event->segment_id == 2, "event identity");
		if (event->type == SVC_START) { ++self.starts; }
		if (event->type == SVC_DONE) { ++self.done; }
		if (event->type == SVC_ERROR)
		{
			self.error.append(reinterpret_cast<const char*>(event->bytes), event->byte_count);
		}
		if (event->type == SVC_AUDIO)
		{
			check(self.starts == 1 && !self.done && event->channels == 1 && event->byte_count <= SVC_MAX_FEED,
				"ordered bounded PCM events");
			check(event->chunk_index == ++self.chunks && event->sample_offset == self.samples
					&& event->byte_count == event->sample_count * 2,
				"contiguous chunks");
			self.samples += event->sample_count;
			self.rate = event->sample_rate;
			self.pcm.append(reinterpret_cast<const char*>(event->bytes), event->byte_count);
			return self.cancelAudio;
		}
		return 0;
	}
};
svc_status run(const svc_plugin* plugin, void* context, Observer& observer, const QJsonObject& selection)
{
	const auto bytes = QJsonDocument(selection).toJson(QJsonDocument::Compact);
	svc_request request{sizeof(request), SVC_ABI_VERSION, 9, 2, bytes.constData(), Observer::read, &observer,
		Observer::event, &observer};
	void* job = plugin->engine->start(context, &request);
	check(job && !observer.events, "start emits no callbacks");
	QElapsedTimer timer;
	timer.start();
	svc_status status = SVC_OK;
	while (status == SVC_OK && timer.elapsed() < 610000)
	{
		status = plugin->engine->pump(job);
	}
	check(status != SVC_OK, "bounded live timeout");
	const auto reads = observer.reads, events = observer.events;
	plugin->engine->cancel(job);
	plugin->engine->cancel(job);
	check(plugin->engine->pump(job) == status && reads == observer.reads && events == observer.events,
		"terminal/cancel idempotence");
	plugin->engine->destroy(job);
	return status;
}
QJsonObject selection(const QJsonObject& model)
{
	QJsonObject params;
	for (const auto& value : model.value("parameters").toArray())
	{
		const auto p = value.toObject();
		params.insert(p.value("id").toString(), p.value("default"));
	}
	return {{"model_id", model.value("id")},
		{"speaker_id", model.value("speakers").toArray().last().toObject().value("id")}, {"parameters", params}};
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	check(argc >= 2, "module argument");
	QLibrary module(QString::fromLocal8Bit(argv[1]));
	check(module.load(), qPrintable(module.errorString()));
	const auto entry = reinterpret_cast<svc_plugin_entry>(module.resolve("svc_plugin_entry_v1"));
	check(entry && !entry(0), "ABI negotiation");
	const auto* plugin = entry(SVC_ABI_VERSION);
	check(plugin && QByteArray(plugin->engine->engine_id) == "USVC", "USVC module");
	if (argc > 2 && QByteArray(argv[2]) == "--live")
	{
		check(argc >= 6, "--live input model output.wav");
		void* context = plugin->create_context("http://127.0.0.1:8001", "");
		const auto* json = plugin->engine->capabilities(context);
		check(json, plugin->error(context));
		const auto caps = QJsonDocument::fromJson(json).object();
		QJsonObject model;
		for (const auto& value : caps.value("models").toArray())
		{
			if (value.toObject().value("id") == QString::fromLocal8Bit(argv[4])) { model = value.toObject(); }
		}
		check(!model.isEmpty(), "live model discovery");
		Observer observer;
		QFile input(QString::fromLocal8Bit(argv[3]));
		check(input.open(QIODevice::ReadOnly), "live source");
		observer.source = input.readAll();
		const auto status = run(plugin, context, observer, selection(model));
		check(status == SVC_COMPLETE, observer.error.constData());
		check(observer.starts == 1 && observer.done == 1 && !observer.pcm.isEmpty(), "live complete audio");
		const auto output = wave(observer.rate, observer.pcm);
		QFile file(QString::fromLocal8Bit(argv[5]));
		check(file.open(QIODevice::WriteOnly) && file.write(output) == output.size(), "live WAV archive");
		std::printf("PASS live model=%s fixture=%d models=%lld rate=%u samples=%llu SHA256=%s\n",
			model.value("id").toString().toUtf8().constData(), model.value("test_fixture").toBool(),
			caps.value("models").toArray().size(), observer.rate, static_cast<unsigned long long>(observer.samples),
			QCryptographicHash::hash(output, QCryptographicHash::Sha256).toHex().constData());
		plugin->destroy_context(context);
		return 0;
	}
	const auto pcm = QByteArray(150000, '\x20');
	const auto wav = wave(32000, pcm);
	for (int scenario = 0; scenario < 7; ++scenario)
	{
		auto body = scenario == 2
			? QByteArray(R"({"error":{"code":"invalid_parameter","message":"bad input","field":"trans"}})")
			: wav;
		if (scenario == 4) { body[0] = 'X'; }
		if (scenario == 5) { qToLittleEndian<uint32_t>(44100, body.data() + 24); }
		Server server(body, scenario == 2 ? 422 : 200, scenario == 3, scenario != 1);
		void* context = plugin->create_context(server.address().constData(), "test-secret");
		const auto* json = plugin->engine->capabilities(context);
		check(json, plugin->error(context));
		const auto caps = QJsonDocument::fromJson(json).object();
		const auto model = caps.value("models").toArray().first().toObject();
		check(model.value("category_path").toArray() == QJsonArray{"ddsp6"}, "category from backend");
		check(model.value("speakers").toArray().last().toObject().value("id") == "1", "stable speaker IDs");
		auto chosen = selection(model);
		auto params = chosen.value("parameters").toObject();
		params.insert("trans", 12);
		// A retained dependent option must not be sent while target_loudness is null.
		params.insert("restore_loudness", "[false]");
		chosen.insert("parameters", params);
		Observer observer;
		observer.source = wav;
		observer.cancelAudio = scenario == 6;
		const auto status = run(plugin, context, observer, chosen);
		server.join();
		check(server.input == wav, "raw input unchanged");
		const QUrl url("http://localhost" + QString::fromUtf8(server.request.split(' ')[1]));
		const auto posted
			= QJsonDocument::fromJson(QUrlQuery(url).queryItemValue("params", QUrl::FullyDecoded).toUtf8()).object();
		check(posted == QJsonObject{{"speaker", 1}, {"trans", 12}},
			"only changed active parameters with numeric speaker");
		if (scenario <= 1)
		{
			check(status == SVC_COMPLETE && observer.done == 1 && observer.pcm == pcm && observer.rate == 32000,
				"chunked and Content-Length WAV completion");
		}
		else if (scenario == 6) { check(status == SVC_CANCELLED && !observer.done, "callback cancellation"); }
		else
		{
			check(status == SVC_FAILED && !observer.done && observer.pcm.isEmpty(), "reject bad response before audio");
		}
		if (scenario == 2)
		{
			check(observer.error.contains("invalid_parameter") && observer.error.contains("trans"),
				"structured API error");
		}
		plugin->destroy_context(context);
	}
	{
		Server server({}, 200, false, true, true, true);
		void* context = plugin->create_context(server.address().constData(), "test-secret");
		check(!plugin->engine->capabilities(context) && QByteArray(plugin->error(context)).contains("settings"),
			"invalid settings rejected");
		plugin->destroy_context(context);
	}
	std::puts("PASS USVC discovery, encoding, defaults, PCM, errors, truncation and cancellation");
}
