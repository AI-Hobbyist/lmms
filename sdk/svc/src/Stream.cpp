#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>
#include <limits>
#include <new>

#include "svc.h"

struct svc_stream
{
	svc_stream_config config{};
	QByteArray boundary;
	QByteArray buffer;
	QByteArray request;
	QByteArray error;
	QHash<QByteArray, QByteArray> headers;
	svc_status status = SVC_OK;
	enum State
	{
		Boundary,
		Headers,
		Body,
		Trailer,
		Closed
	} state = Boundary;
	size_t bodyLength = 0;
	uint64_t chunks = 0;
	uint64_t samples = 0;
	uint64_t total = 0;
	uint32_t rate = 0;
	bool started = false;
	bool terminal = false;
	bool awaitingProgress = false;
	bool feeding = false;

	svc_status fail(const char* reason)
	{
		error = reason;
		status = SVC_PROTOCOL;
		buffer.clear();
		return status;
	}

	static bool integer(const QJsonObject& object, const char* key, uint64_t& output)
	{
		const auto value = object.value(key);
		const auto number = value.toDouble(-1);
		if (!value.isDouble() || !std::isfinite(number) || number < 0 || number > 9007199254740991.0
			|| std::floor(number) != number)
		{
			return false;
		}
		output = static_cast<uint64_t>(number);
		return true;
	}

	bool headerInteger(const char* key, uint64_t& output)
	{
		const auto text = headers.value(key);
		if (text.isEmpty()) { return false; }
		for (const auto character : text)
		{
			if (character < '0' || character > '9') { return false; }
		}
		bool valid;
		output = text.toULongLong(&valid);
		return valid;
	}

	bool deliver(svc_event& event)
	{
		event.size = sizeof(event);
		event.generation_id = config.generation_id;
		event.segment_id = config.segment_id;
		event.request_id = request.constData();
		event.sample_rate = rate;
		event.channels = 1;
		event.total_chunks = total;
		if (config.callback(config.user, &event))
		{
			status = SVC_CANCELLED;
			buffer.clear();
			return false;
		}
		return true;
	}

	bool part(const QByteArray& bytes)
	{
		if (terminal)
		{
			fail("Part after terminal event");
			return false;
		}
		svc_event event{};
		event.bytes = reinterpret_cast<const uint8_t*>(bytes.constData());
		event.byte_count = static_cast<size_t>(bytes.size());
		const auto kind = headers.value("x-event-type");
		if (kind == "audio")
		{
			if (!started || awaitingProgress || headers.value("content-type") != "application/octet-stream"
				|| !headerInteger("x-chunk-index", event.chunk_index) || event.chunk_index != chunks + 1
				|| event.chunk_index > total || !headerInteger("x-sample-offset", event.sample_offset)
				|| event.sample_offset != samples || !headerInteger("x-sample-count", event.sample_count)
				|| !event.sample_count || event.sample_count > SVC_MAX_PART / 2
				|| event.sample_count * 2 != event.byte_count
				|| samples > std::numeric_limits<uint64_t>::max() - event.sample_count)
			{
				fail("Invalid audio index, offset, format or byte count");
				return false;
			}
			event.type = SVC_AUDIO;
			++chunks;
			samples += event.sample_count;
			awaitingProgress = true;
			return deliver(event);
		}
		if (!headers.value("content-type").startsWith("application/json"))
		{
			fail("Event must contain JSON");
			return false;
		}
		QJsonParseError parse;
		const auto document = QJsonDocument::fromJson(bytes, &parse);
		const auto object = document.object();
		const auto id = object.value("request_id").toString().toUtf8();
		if (parse.error || !document.isObject() || id.isEmpty() || id.size() > 1024 || (started && id != request))
		{
			fail("Invalid JSON or mismatched request ID");
			return false;
		}
		if (kind == "start")
		{
			uint64_t sampleRate;
			uint64_t channels;
			if (started || object.value("codec").toString() != "pcm_s16le"
				|| !integer(object, "sample_rate", sampleRate) || sampleRate < 8000 || sampleRate > 384000
				|| !integer(object, "channels", channels) || channels != 1 || !integer(object, "total", total) || !total
				|| total > 1000000 || !object.value("parameters").isObject())
			{
				fail("Invalid or duplicate start event");
				return false;
			}
			rate = static_cast<uint32_t>(sampleRate);
			request = id;
			started = true;
			event.type = SVC_START;
		}
		else
		{
			uint64_t current;
			uint64_t declaredTotal;
			if (!started || !integer(object, "current", current) || current != chunks
				|| !integer(object, "total", declaredTotal) || declaredTotal != total)
			{
				fail("Invalid event ordering or totals");
				return false;
			}
			event.chunk_index = current;
			if (kind == "progress")
			{
				if (!awaitingProgress || object.value("status").toString() != "running"
					|| object.value("stage").toString() != "infer")
				{
					fail("Invalid progress event");
					return false;
				}
				awaitingProgress = false;
				event.type = SVC_PROGRESS;
			}
			else if (kind == "done")
			{
				uint64_t declaredSamples;
				if (awaitingProgress || chunks != total || !integer(object, "samples", declaredSamples)
					|| declaredSamples != samples || object.value("status").toString() != "completed"
					|| !object.value("parameters").isObject()
					|| (config.require_cleanup && object.value("gpu_cleanup_completed") != QJsonValue(true)))
				{
					fail("Invalid done totals or cleanup");
					return false;
				}
				terminal = true;
				event.type = SVC_DONE;
				event.sample_count = samples;
			}
			else if (kind == "error")
			{
				const auto failure = object.value("error").toObject();
				if (object.value("status").toString() != "failed" || failure.value("code").toString().isEmpty()
					|| !failure.value("message").isString())
				{
					fail("Invalid error event");
					return false;
				}
				event.type = SVC_ERROR;
				terminal = true;
				if (!deliver(event)) { return false; }
				status = SVC_FAILED;
				error = failure.value("code").toString().toUtf8();
				return false;
			}
			else
			{
				fail("Unknown event type");
				return false;
			}
		}
		return deliver(event);
	}

	svc_status parse()
	{
		while (status == SVC_OK)
		{
			if (state == Boundary)
			{
				const auto prefix = QByteArray("--") + boundary;
				if (buffer.size() < prefix.size() + 2) { break; }
				if (!buffer.startsWith(prefix)) { return fail("Invalid multipart boundary"); }
				const auto suffix = buffer.mid(prefix.size(), 2);
				if (suffix == "--")
				{
					if (buffer.size() < prefix.size() + 4) { break; }
					if (!terminal || buffer.mid(prefix.size() + 2, 2) != "\r\n")
					{
						return fail("Closing boundary before done or invalid delimiter");
					}
					buffer.remove(0, prefix.size() + 4);
					state = Closed;
				}
				else if (suffix == "\r\n")
				{
					buffer.remove(0, prefix.size() + 2);
					state = Headers;
				}
				else
				{
					return fail("Invalid boundary suffix");
				}
			}
			else if (state == Headers)
			{
				const auto end = buffer.indexOf("\r\n\r\n");
				if (end < 0)
				{
					if (buffer.size() > SVC_MAX_HEADER) { return fail("Header limit exceeded"); }
					break;
				}
				if (end > SVC_MAX_HEADER) { return fail("Header limit exceeded"); }
				headers.clear();
				for (auto line : buffer.left(end).split('\n'))
				{
					if (line.endsWith('\r')) { line.chop(1); }
					const auto colon = line.indexOf(':');
					const auto key = line.left(colon).toLower();
					if (colon <= 0 || headers.contains(key)
						|| !QRegularExpression("^[a-z0-9-]+$").match(QString::fromLatin1(key)).hasMatch())
					{
						return fail("Malformed or duplicate header");
					}
					headers.insert(key, line.mid(colon + 1).trimmed());
				}
				uint64_t length;
				if (!headerInteger("content-length", length) || length > SVC_MAX_PART
					|| !headers.contains("x-event-type") || !headers.contains("content-type"))
				{
					return fail("Missing event header or invalid part length");
				}
				bodyLength = static_cast<size_t>(length);
				buffer.remove(0, end + 4);
				state = Body;
			}
			else if (state == Body)
			{
				if (static_cast<size_t>(buffer.size()) < bodyLength) { break; }
				const auto body = buffer.left(static_cast<int>(bodyLength));
				buffer.remove(0, static_cast<int>(bodyLength));
				if (!part(body)) { break; }
				state = Trailer;
			}
			else if (state == Trailer)
			{
				if (buffer.size() < 2) { break; }
				if (!buffer.startsWith("\r\n")) { return fail("Invalid part trailer"); }
				buffer.remove(0, 2);
				state = Boundary;
			}
			else
			{
				if (!buffer.isEmpty()) { return fail("Bytes after closing boundary"); }
				break;
			}
		}
		return status;
	}
};

extern "C" svc_stream* svc_stream_create(const svc_stream_config* config)
{
	if (!config || config->size < sizeof(*config) || config->abi_version != SVC_ABI_VERSION || !config->callback
		|| !config->boundary)
	{
		return nullptr;
	}
	try
	{
		const QByteArray boundary(config->boundary);
		if (boundary.isEmpty() || boundary.size() > 200
			|| !QRegularExpression("^[a-zA-Z0-9_-]+$").match(QString::fromLatin1(boundary)).hasMatch())
		{
			return nullptr;
		}
		auto stream = new svc_stream;
		stream->config = *config;
		stream->boundary = boundary;
		stream->config.boundary = nullptr;
		return stream;
	}
	catch (...)
	{
		return nullptr;
	}
}

extern "C" svc_status svc_stream_feed(svc_stream* stream, const uint8_t* bytes, size_t count)
{
	if (!stream || (!bytes && count) || count > SVC_MAX_FEED || stream->feeding) { return SVC_INVALID; }
	if (stream->status != SVC_OK) { return stream->status; }
	stream->feeding = true;
	try
	{
		stream->buffer.append(reinterpret_cast<const char*>(bytes), static_cast<int>(count));
		stream->parse();
	}
	catch (...)
	{
		stream->fail("Stream allocation or callback failure");
	}
	stream->feeding = false;
	return stream->status;
}

extern "C" svc_status svc_stream_finish(svc_stream* stream)
{
	if (!stream || stream->feeding) { return SVC_INVALID; }
	if (stream->status != SVC_OK) { return stream->status; }
	if (stream->state != svc_stream::Closed || !stream->buffer.isEmpty())
	{
		return stream->fail("Truncated stream / missing terminal or closing boundary");
	}
	stream->status = SVC_COMPLETE;
	return stream->status;
}

extern "C" void svc_stream_cancel(svc_stream* stream)
{
	if (stream && stream->status == SVC_OK)
	{
		stream->status = SVC_CANCELLED;
		stream->buffer.clear();
	}
}

extern "C" void svc_stream_destroy(svc_stream* stream)
{ delete stream; }
extern "C" const char* svc_stream_error(const svc_stream* stream)
{ return stream ? stream->error.constData() : "Null stream"; }
