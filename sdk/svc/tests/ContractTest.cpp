#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "svc.hpp"

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
		+ (std::strcmp(kind, "audio") == 0 ? "application/octet-stream" : "application/json; charset=utf-8")
		+ "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nX-Event-Type: " + kind + "\r\n" + extra
		+ "\r\n" + body + "\r\n";
}

QByteArray start()
{
	return part("start",
		R"({"request_id":"id","codec":"pcm_s16le","sample_rate":16000,"channels":1,"total":2,"parameters":{}})");
}

QByteArray audio(int index, int offset)
{
	// Boundary-like binary must be consumed by length, never searched as text.
	return part("audio", QByteArray("--test\r\n", 8),
		"X-Chunk-Index: " + QByteArray::number(index) + "\r\nX-Sample-Offset: " + QByteArray::number(offset)
			+ "\r\nX-Sample-Count: 4\r\n");
}

QByteArray progress(int index)
{
	return part("progress",
		"{\"request_id\":\"id\",\"stage\":\"infer\",\"status\":\"running\",\"current\":" + QByteArray::number(index)
			+ ",\"total\":2}");
}

QByteArray done()
{
	return part("done",
		R"({"request_id":"id","status":"completed","current":2,"total":2,"samples":8,"parameters":{},"gpu_cleanup_completed":true})");
}

struct Observer
{
	std::vector<svc_event_type> types;
	QByteArray pcm;
	bool cancel = false;
	static int callback(void* user, const svc_event* event)
	{
		auto& self = *static_cast<Observer*>(user);
		check(event->generation_id == 7 && event->segment_id == 3, "frozen identity");
		check(std::strcmp(event->request_id, "id") == 0, "request identity");
		self.types.push_back(event->type);
		if (event->type == SVC_AUDIO)
		{
			self.pcm.append(reinterpret_cast<const char*>(event->bytes), static_cast<int>(event->byte_count));
			return self.cancel;
		}
		return 0;
	}
};

svc::Stream stream(Observer& observer)
{
	svc_stream_config config{sizeof(config), SVC_ABI_VERSION, 7, 3, "test", Observer::callback, &observer, 1};
	return svc::Stream(svc_stream_create(&config));
}

svc_status feed(svc_stream* parser, const QByteArray& bytes)
{ return svc_stream_feed(parser, reinterpret_cast<const uint8_t*>(bytes.constData()), bytes.size()); }

void rejected(const QByteArray& bytes, const char* label)
{
	Observer observer;
	auto parser = stream(observer);
	feed(parser.get(), bytes);
	check(svc_stream_finish(parser.get()) == SVC_PROTOCOL, label);
}

struct Input
{
	int reads = 0;
	int callbacks = 0;
	bool cancel = false;
	static int64_t read(void* user, uint8_t* target, size_t capacity)
	{
		auto& input = *static_cast<Input*>(user);
		check(capacity == SVC_MAX_FEED, "bounded input pull");
		if (input.reads++ == 2) { return 0; }
		std::memset(target, input.reads, capacity);
		return static_cast<int64_t>(capacity);
	}
	static int callback(void* user, const svc_event* event)
	{
		auto& input = *static_cast<Input*>(user);
		++input.callbacks;
		return input.cancel && event->type == SVC_AUDIO;
	}
};
} // namespace

int main()
{
	const auto bytes = start() + audio(1, 0) + progress(1) + audio(2, 4) + progress(2) + done() + "--test--\r\n";
	for (int fragment = 1; fragment <= bytes.size(); ++fragment)
	{
		Observer observer;
		auto parser = stream(observer);
		check(static_cast<bool>(parser), "create parser");
		for (int position = 0; position < bytes.size(); position += fragment)
		{
			check(feed(parser.get(), bytes.mid(position, fragment)) == SVC_OK, "arbitrary fragment parse");
		}
		check(svc_stream_finish(parser.get()) == SVC_COMPLETE, "validated closing boundary and EOF");
		check(observer.types.size() == 6 && observer.pcm.size() == 16, "all events and copied PCM");
	}
	for (int cutoff = 0; cutoff < bytes.size(); ++cutoff)
	{
		rejected(bytes.left(cutoff), "every truncated boundary/header/body/terminal");
	}
	Observer observer;
	auto parser = stream(observer);
	check(feed(parser.get(), start() + audio(1, 0)) == SVC_OK, "first audio valid");
	check(observer.pcm.size() == 8 && observer.types.back() == SVC_AUDIO, "first audio published before done");
	svc_stream_cancel(parser.get());
	const auto count = observer.types.size();
	check(feed(parser.get(), bytes) == SVC_CANCELLED && observer.types.size() == count, "cancel stops callbacks");
	Observer callbackCancel;
	callbackCancel.cancel = true;
	auto cancelled = stream(callbackCancel);
	check(feed(cancelled.get(), bytes) == SVC_CANCELLED && callbackCancel.types.size() == 2, "callback cancellation");

	rejected(start() + audio(2, 0), "out-of-order audio");
	rejected(start() + audio(1, 1), "offset gap");
	rejected(start() + audio(1, 0) + progress(1) + audio(1, 4), "duplicate audio");
	rejected(start() + start(), "duplicate start");
	rejected(audio(1, 0), "audio before start");
	rejected(bytes + "garbage", "trailing bytes");
	auto modified = bytes;
	modified.replace("\"samples\":8", "\"samples\":9");
	rejected(modified, "done sample mismatch");
	modified = bytes;
	modified.replace("\"gpu_cleanup_completed\":true", "\"gpu_cleanup_completed\":false");
	rejected(modified, "cleanup incomplete");
	modified = bytes;
	modified.replace("\"request_id\":\"id\",\"stage\"", "\"request_id\":\"other\",\"stage\"");
	rejected(modified, "request mismatch");
	modified = bytes;
	modified.replace("X-Sample-Count: 4", "X-Sample-Count: 18446744073709551615");
	rejected(modified, "count overflow");
	rejected("--test\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx", "duplicate header");
	rejected(
		"--test\r\nContent-Length: 8388609\r\nX-Event-Type: audio\r\nContent-Type: application/octet-stream\r\n\r\n",
		"bounded body");
	rejected("--test\r\n" + QByteArray(SVC_MAX_HEADER + 1, 'x'), "bounded header");
	Observer failure;
	auto failed = stream(failure);
	check(
		feed(failed.get(),
			start() + audio(1, 0) + progress(1)
				+ part("error",
					R"({"request_id":"id","status":"failed","current":1,"total":2,"error":{"code":"test","message":"failure"}})"))
				== SVC_FAILED
			&& failure.pcm.size() == 8,
		"partial failure remains failure");

	svc_stream_config bad{sizeof(bad), SVC_ABI_VERSION + 1, 7, 3, "test", Observer::callback, &observer, 0};
	check(svc_stream_create(&bad) == nullptr, "ABI negotiation");
	bad.abi_version = SVC_ABI_VERSION;
	bad.size = 1;
	check(svc_stream_create(&bad) == nullptr, "structure size negotiation");

	const auto engine = svc_reference_engine(SVC_ABI_VERSION);
	check(engine && !svc_reference_engine(SVC_ABI_VERSION + 1), "engine negotiation");
	char error[256];
	const QByteArray capabilities(engine->capabilities(nullptr));
	check(svc_validate_capabilities(capabilities.constData(), capabilities.size(), error, sizeof(error)) == SVC_OK,
		"generic discovery");
	for (const auto& invalid : {QByteArray("{}"), QByteArray("[]"), QByteArray("invalid"),
			 QByteArray(capabilities).replace("\"schema_version\":1", "\"schema_version\":2")})
	{
		check(svc_validate_capabilities(invalid.constData(), invalid.size(), error, sizeof(error)) == SVC_INVALID
				&& error[0],
			"invalid capability/version reports error");
	}
	Input input;
	svc_request request{sizeof(request), SVC_ABI_VERSION, 7, 3, "{}", Input::read, &input, Input::callback, &input};
	{
		svc::Job job(*engine, request);
		check(static_cast<bool>(job) && input.callbacks == 0 && input.reads == 0, "start is inert");
		check(job.pump() == SVC_OK && input.callbacks == 1 && input.reads == 0, "start event");
		check(job.pump() == SVC_OK && input.reads == 1, "bounded input/output step");
		job.cancel();
		job.cancel();
		check(job.pump() == SVC_CANCELLED && input.reads == 1 && input.callbacks == 2, "job cancellation lifecycle");
	}
	input = {};
	input.cancel = true;
	{
		svc::Job job(*engine, request);
		job.pump();
		check(job.pump() == SVC_CANCELLED && job.pump() == SVC_CANCELLED && input.reads == 1,
			"callback cancellation prevents more input");
	}
	input = {};
	{
		svc::Job job(*engine, request);
		svc_status result;
		do
		{
			result = job.pump();
		} while (result == SVC_OK);
		check(result == SVC_COMPLETE && input.reads == 3 && input.callbacks == 4, "reference bounded completion");
	}
	std::puts("PASS SVC M0: all fragment sizes, all truncations, invalid protocol, provisional audio, ABI, "
			  "capabilities, cancel ownership");
	return 0;
}
