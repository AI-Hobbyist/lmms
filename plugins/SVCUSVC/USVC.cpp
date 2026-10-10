#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QUrlQuery>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <memory>
#include <samplerate.h>
#include <stdexcept>
#include <vector>

#include "../SVCRVC/Http.h"
#include "svc_plugin.h"

namespace svc_usvc {
namespace {
constexpr qint64 MaxOutputBytes = 600LL * 192000 * 2 + SVC_MAX_HEADER;
struct Context
{
	QString address;
	QByteArray token, capabilities, error;
};

QString optionId(const QJsonValue& value)
{ return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)); }

QJsonObject option(const QJsonValue& value, const QString& name)
{ return {{"id", optionId(value)}, {"name", name}, {"value", value}, {"available", true}}; }

QJsonDocument get(Context& context, const QUrl& url)
{
	svc_rvc::Http http(url, context.token, false, 120000);
	QByteArray body;
	while (http.pump({}, [&](const uint8_t* data, size_t count) {
		if (body.size() + count > SVC_MAX_PART) { return false; }
		body.append(reinterpret_cast<const char*>(data), count);
		return true;
	}))
	{
	}
	if (!http.error().isEmpty() || http.status() != 200)
	{
		throw std::runtime_error(QString("HTTP %1: %2 %3")
				.arg(http.status())
				.arg(http.error(), QString::fromUtf8(body.left(SVC_MAX_HEADER)))
				.toStdString());
	}
	QJsonParseError error;
	const auto result = QJsonDocument::fromJson(body, &error);
	if (error.error) { throw std::runtime_error("Invalid USVC JSON response"); }
	return result;
}

QJsonArray parameters(const QJsonObject& settings)
{
	QJsonArray result;
	for (const auto& entry : settings.value("parameters").toArray())
	{
		const auto spec = entry.toObject();
		const auto key = spec.value("key").toString();
		const auto type = spec.value("type").toString();
		if (type == "speaker") { continue; }
		if (key.isEmpty()) { throw std::runtime_error("Missing USVC parameter key"); }
		QJsonObject parameter{{"id", key}, {"name", key}, {"unit", ""}, {"scope", spec.value("stage")},
			{"default", spec.value("default")}, {"description", spec.value("description")}, {"usvc_spec", spec},
			{"available", true}};
		if (type == "number" || type == "integer")
		{
			parameter.insert("type", type);
			parameter.insert("nullable", spec.value("nullable"));
			parameter.insert("step", type == "integer" ? 1. : .01);
			parameter.insert("control", "slider");
			for (const auto& bound : {"minimum", "maximum"})
			{
				if (spec.value(bound).isDouble()) { parameter.insert(bound, spec.value(bound)); }
			}
		}
		else
		{
			QJsonArray options;
			if (spec.value("nullable").toBool()) { options.append(option(QJsonValue::Null, "Default")); }
			if (type == "boolean")
			{
				options.append(option(false, "False"));
				options.append(option(true, "True"));
			}
			else if (type == "resource")
			{
				if (key == "index") { options.append(option(false, "Disabled")); }
				const auto list = key == "index" ? "indexes"
					: key == "cluster"			 ? "clusters"
					: key == "vocoder"			 ? "vocoders"
												 : "enhancers";
				for (const auto& resource : settings.value(list).toArray())
				{
					const auto id = resource.toString();
					if (key == "diffusion_model" && !id.startsWith("diffusion/")) { continue; }
					if (key == "reflow_model" && !id.startsWith("ddsp6/") && !id.startsWith("reflow_shallow/"))
					{
						continue;
					}
					options.append(option(resource, id));
				}
			}
			else if (type == "string" && spec.value("enum").isArray())
			{
				for (const auto& value : spec.value("enum").toArray())
				{
					options.append(option(value, value.toString()));
				}
			}
			else
			{
				throw std::runtime_error("Unsupported USVC parameter type");
			}
			parameter.insert("type", "enum");
			parameter.insert("options", options);
			parameter.insert("default", optionId(spec.value("default")));
		}
		const auto condition = spec.value("condition").toString();
		QJsonObject when;
		if (condition == "target_loudness is set")
		{
			when.insert("target_loudness", QJsonObject{{"not", QJsonValue::Null}});
		}
		else if (condition == "diffusion enhancer active" || condition == "reflow enhancer active")
		{
			when.insert("shallow_diffusion", optionId(true));
			when.insert(condition == "diffusion enhancer active" ? "diffusion_model" : "reflow_model",
				QJsonObject{{"not", optionId(QJsonValue::Null)}});
		}
		else if (condition == "available enhancer resource")
		{
			parameter.insert("available", !settings.value("enhancers").toArray().isEmpty());
		}
		else if (condition.startsWith("CUDA"))
		{
			bool available = settings.value("device").toString().startsWith("cuda");
			const auto backend = settings.value("backend").toString();
			if (condition == "CUDA and non-diffusion model") { available &= backend != "diffusion"; }
			if (condition == "CUDA sm80+ and RVC/SoVITS") { available &= backend == "rvc" || backend == "sovits"; }
			// CUDA compute capability is not exposed; the service validates sm80+ on submission.
			parameter.insert("available", available);
		}
		parameter.insert("enabled_when", when);
		parameter.insert("reason", spec.value("description").toString() + " (" + condition + ")");
		result.append(parameter);
	}
	return result;
}

const char* capabilities(void* opaque)
{
	if (!opaque) { return nullptr; }
	auto& context = *static_cast<Context*>(opaque);
	try
	{
		const auto document = get(context, svc_rvc::endpoint(context.address, "/model"));
		if (!document.isArray() || document.array().size() > 1024)
		{
			throw std::runtime_error("Invalid or oversized USVC model list");
		}
		QJsonObject root{{"schema_version", 1}, {"engine_id", "USVC"}, {"parameters", QJsonArray{}},
			{"limits", QJsonObject{{"min_seconds", .001}, {"max_seconds", 600}, {"max_upload_bytes", 67108864}}}};
		QJsonArray models;
		for (const auto& entry : document.array())
		{
			auto model = entry.toObject();
			const auto id = model.value("id").toString();
			const auto backend = model.value("backend").toString();
			if (id.isEmpty() || backend.isEmpty()) { throw std::runtime_error("Invalid USVC model identity"); }
			auto url = svc_rvc::endpoint(context.address, "/settings");
			url.setQuery(QString::fromLatin1(
							 "backend=" + QUrl::toPercentEncoding(backend) + "&model=" + QUrl::toPercentEncoding(id)),
				QUrl::StrictMode);
			const auto settings = get(context, url).object();
			if (settings.value("model") != id || settings.value("backend") != backend
				|| !settings.value("parameters").isArray() || !settings.value("speakers").isArray())
			{
				throw std::runtime_error("Mismatched USVC settings document");
			}
			QJsonArray speakers;
			for (const auto& entry : settings.value("speakers").toArray())
			{
				auto speaker = entry.toObject();
				if (!speaker.value("id").isDouble() || speaker.value("id").toDouble() < 0
					|| speaker.value("id").toDouble() != speaker.value("id").toInt())
				{
					throw std::runtime_error("Invalid USVC speaker ID");
				}
				speaker.insert("id", QString::number(speaker.value("id").toInt()));
				speakers.append(speaker);
			}
			model.insert("speakers", speakers);
			model.insert("category_path", QJsonArray{backend});
			model.insert("available", model.value("status") == "ready");
			model.insert("parameters", parameters(settings));
			model.insert("usvc_settings", settings);
			if (speakers.size() == 1) { model.insert("fixed_speaker_id", speakers.first().toObject().value("id")); }
			// Validate per-model parameters using the same public schema validator.
			auto check = root;
			check.insert("models", QJsonArray{model});
			check.insert("parameters", model.value("parameters"));
			const auto bytes = QJsonDocument(check).toJson(QJsonDocument::Compact);
			char error[512]{};
			if (svc_validate_capabilities(bytes.constData(), bytes.size(), error, sizeof(error)) != SVC_OK)
			{
				throw std::runtime_error(error);
			}
			models.append(model);
		}
		root.insert("models", models);
		context.capabilities = QJsonDocument(root).toJson(QJsonDocument::Compact);
		if (context.capabilities.size() > SVC_MAX_PART) { throw std::runtime_error("USVC capabilities exceed limit"); }
		context.error.clear();
		return context.capabilities.constData();
	}
	catch (const std::exception& error)
	{
		context.error = error.what();
	}
	catch (...)
	{
		context.error = "USVC discovery failed";
	}
	if (!context.token.isEmpty()) { context.error.replace(context.token, "[redacted]"); }
	return nullptr;
}

struct Job
{
	svc_request request{};
	std::unique_ptr<svc_rvc::Http> http;
	QTemporaryFile wave;
	QTemporaryFile normalized;
	QFile* audio = &wave;
	QByteArray token, errorBody, inputHeader;
	uint64_t inputBytes = 0;
	uint32_t outputRate = 0;
	svc_status terminal = SVC_OK;
	uint32_t rate = 0;
	uint64_t samples = 0, offset = 0, chunk = 0, dataStart = 0;
	uint64_t reportedBytes = UINT64_MAX;
	bool decoded = false;

	double inputDuration() const
	{
		if (inputHeader.size() < 12 || inputHeader.left(4) != "RIFF" || inputHeader.mid(8, 4) != "WAVE"
			|| uint64_t(qFromLittleEndian<uint32_t>(inputHeader.constData() + 4)) + 8 != inputBytes)
		{
			throw std::runtime_error("Invalid USVC input WAV length");
		}
		uint32_t inputRate = 0, alignment = 0;
		for (uint64_t at = 12; at + 8 <= uint64_t(inputHeader.size());)
		{
			const auto* header = inputHeader.constData() + at;
			const auto length = qFromLittleEndian<uint32_t>(header + 4);
			if (QByteArray(header, 4) == "fmt " && length >= 16 && at + 24 <= uint64_t(inputHeader.size()))
			{
				inputRate = qFromLittleEndian<uint32_t>(header + 12);
				alignment = qFromLittleEndian<uint16_t>(header + 20);
			}
			if (QByteArray(header, 4) == "data" && inputRate && alignment && length && length % alignment == 0
				&& at + 8 + length <= inputBytes)
			{
				return double(length / alignment) / inputRate;
			}
			at += 8ULL + length + (length & 1);
		}
		throw std::runtime_error("Missing USVC input WAV duration");
	}

	void normalize()
	{
		const auto duration = inputDuration();
		const auto expected = uint64_t(std::llround(duration * rate));
		// Feature/vocoder frames quantize the endpoint. Correct only a bounded tail;
		// keep the start and all interior samples in place, and reject larger mismatches.
		if (!expected || std::abs(double(samples) - expected) > std::ceil(rate * .020) + 1)
		{
			throw std::runtime_error("USVC output duration differs from input by more than 20 ms");
		}
		const auto targetRate = outputRate ? outputRate : rate;
		if (samples == expected && targetRate == rate) { return; }
		if (!normalized.open()) { throw std::runtime_error("Cannot prepare normalized USVC audio"); }
		int error = 0;
		std::unique_ptr<SRC_STATE, decltype(&src_delete)> converter(
			targetRate == rate ? nullptr : src_new(SRC_SINC_FASTEST, 1, &error), src_delete);
		if (targetRate != rate && !converter) { throw std::runtime_error(src_strerror(error)); }
		const auto targetSamples = uint64_t(std::llround(duration * targetRate));
		uint64_t inputOffset = 0, written = 0;
		std::vector<float> output(SVC_MAX_FEED / 2);
		const auto write = [&](const float* values, uint64_t count) {
			count = std::min(count, targetSamples - written);
			QByteArray pcm(count * 2, 0);
			for (uint64_t index = 0; index < count; ++index)
			{
				qToLittleEndian<int16_t>(int16_t(std::clamp(std::llround(values[index] * 32768.), -32768LL, 32767LL)),
					pcm.data() + index * 2);
			}
			if (normalized.write(pcm) != pcm.size()) { throw std::runtime_error("Cannot write normalized USVC audio"); }
			written += count;
		};
		while (inputOffset < expected)
		{
			const auto count = std::min<uint64_t>(SVC_MAX_FEED / 2, expected - inputOffset);
			const auto available = inputOffset < samples ? std::min(count, samples - inputOffset) : 0;
			const auto bytes = wave.read(available * 2);
			if (uint64_t(bytes.size()) != available * 2) { throw std::runtime_error("Cannot read USVC audio tail"); }
			std::vector<float> input(count, 0);
			for (uint64_t index = 0; index < available; ++index)
			{
				input[index] = qFromLittleEndian<int16_t>(bytes.constData() + index * 2) / 32768.f;
			}
			inputOffset += count;
			if (!converter)
			{
				write(input.data(), count);
				continue;
			}
			long consumed = 0;
			for (;;)
			{
				SRC_DATA data{};
				data.data_in = input.data() + consumed;
				data.input_frames = long(count) - consumed;
				data.data_out = output.data();
				data.output_frames = long(output.size());
				data.src_ratio = double(targetRate) / rate;
				data.end_of_input = inputOffset == expected;
				if (const auto result = src_process(converter.get(), &data))
				{
					throw std::runtime_error(src_strerror(result));
				}
				consumed += data.input_frames_used;
				write(output.data(), data.output_frames_gen);
				if (consumed == long(count) && (!data.end_of_input || !data.output_frames_gen)) { break; }
				if (!data.input_frames_used && !data.output_frames_gen)
				{
					throw std::runtime_error("USVC resampler stalled");
				}
			}
		}
		if (written < targetSamples)
		{
			if (targetSamples - written > 1) { throw std::runtime_error("USVC resampler length mismatch"); }
			const float zero = 0;
			write(&zero, 1);
		}
		if (!normalized.flush() || !normalized.seek(0))
		{
			throw std::runtime_error("Cannot rewind normalized USVC audio");
		}
		rate = targetRate;
		samples = targetSamples;
		audio = &normalized;
	}

	bool deliver(svc_event_type type, const QByteArray& bytes = {}, uint64_t count = 0)
	{
		svc_event event{};
		event.size = sizeof(event);
		event.type = type;
		event.generation_id = request.generation_id;
		event.segment_id = request.segment_id;
		event.request_id = "usvc";
		event.chunk_index = chunk;
		event.total_chunks = (samples * 2 + SVC_MAX_FEED - 1) / SVC_MAX_FEED;
		event.sample_offset = offset;
		event.sample_count = count;
		event.sample_rate = rate;
		event.channels = 1;
		event.bytes = reinterpret_cast<const uint8_t*>(bytes.constData());
		event.byte_count = bytes.size();
		if (request.callback(request.callback_user, &event))
		{
			terminal = SVC_CANCELLED;
			http->cancel();
			return false;
		}
		return true;
	}

	void fail(QString message)
	{
		terminal = SVC_FAILED;
		if (!token.isEmpty()) { message.replace(QString::fromUtf8(token), "[redacted]"); }
		const auto json = QJsonDocument(QJsonObject{{"message", message}, {"http_status", http->status()}})
							  .toJson(QJsonDocument::Compact);
		deliver(SVC_ERROR, json);
		wave.close();
	}

	void decode()
	{
		if (!wave.flush() || !wave.seek(0)) { throw std::runtime_error("Cannot rewind USVC WAV"); }
		const auto riff = wave.read(12);
		if (riff.size() != 12 || riff.left(4) != "RIFF" || riff.mid(8, 4) != "WAVE"
			|| uint64_t(qFromLittleEndian<uint32_t>(riff.constData() + 4)) + 8 != uint64_t(wave.size()))
		{
			throw std::runtime_error("Invalid or truncated USVC WAV");
		}
		bool format = false, data = false;
		for (int chunks = 0; wave.pos() < wave.size() && chunks < 1024; ++chunks)
		{
			const auto header = wave.read(8);
			if (header.size() != 8) { throw std::runtime_error("Truncated WAV chunk"); }
			const auto length = qFromLittleEndian<uint32_t>(header.constData() + 4);
			const auto start = wave.pos();
			const auto next = start + uint64_t(length) + (length & 1);
			if (next > uint64_t(wave.size())) { throw std::runtime_error("Invalid WAV chunk size"); }
			if (header.left(4) == "fmt ")
			{
				if (format || length < 16) { throw std::runtime_error("Invalid WAV format chunk"); }
				const auto bytes = wave.read(16);
				rate = qFromLittleEndian<uint32_t>(bytes.constData() + 4);
				if (qFromLittleEndian<uint16_t>(bytes.constData()) != 1
					|| qFromLittleEndian<uint16_t>(bytes.constData() + 2) != 1
					|| qFromLittleEndian<uint16_t>(bytes.constData() + 14) != 16
					|| qFromLittleEndian<uint16_t>(bytes.constData() + 12) != 2
					|| qFromLittleEndian<uint32_t>(bytes.constData() + 8) != rate * 2 || rate < 8000 || rate > 192000)
				{
					throw std::runtime_error("USVC WAV must be mono PCM16 with a supported sample rate");
				}
				format = true;
			}
			else if (header.left(4) == "data")
			{
				if (data || !length || length % 2) { throw std::runtime_error("Invalid WAV audio chunk"); }
				dataStart = start;
				samples = length / 2;
				data = true;
			}
			if (!wave.seek(next)) { throw std::runtime_error("Cannot seek USVC WAV"); }
		}
		bool validRate;
		const auto reportedRate = http->header("x-usvc-sample-rate").toUInt(&validRate);
		if (!format || !data || !validRate || rate != reportedRate || wave.pos() != wave.size()
			|| samples > uint64_t(rate) * 600 || !wave.seek(dataStart))
		{
			throw std::runtime_error("USVC WAV metadata mismatch");
		}
		normalize();
		decoded = true;
		deliver(SVC_START,
			QJsonDocument(QJsonObject{{"codec", "pcm_s16le"}, {"sample_rate", int(rate)}, {"channels", 1},
							  {"test_fixture", http->header("x-usvc-test-fixture") == "true"}})
				.toJson(QJsonDocument::Compact));
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
		QJsonParseError parse;
		const auto selectionDocument = QJsonDocument::fromJson(request->selection_json, &parse);
		if (parse.error || !selectionDocument.isObject()) { return nullptr; }
		const auto selection = selectionDocument.object();
		QJsonObject model;
		for (const auto& entry : QJsonDocument::fromJson(context.capabilities).object().value("models").toArray())
		{
			if (entry.toObject().value("id") == selection.value("model_id")) { model = entry.toObject(); }
		}
		if (model.isEmpty() || !model.value("available").toBool()) { return nullptr; }
		QJsonObject params;
		const auto saved = selection.value("parameters").toObject();
		for (auto it = saved.begin(); it != saved.end(); ++it)
		{
			QJsonObject definition;
			for (const auto& entry : model.value("parameters").toArray())
			{
				if (entry.toObject().value("id") == it.key()) { definition = entry.toObject(); }
			}
			if (definition.isEmpty()) { return nullptr; }
			const auto spec = definition.value("usvc_spec").toObject();
			QJsonValue value = it.value();
			if (definition.value("type") == "enum")
			{
				bool found = false;
				for (const auto& entry : definition.value("options").toArray())
				{
					const auto choice = entry.toObject();
					if (choice.value("id") != value || !choice.value("available").toBool()) { continue; }
					value = choice.value("value");
					found = true;
					break;
				}
				if (!found) { return nullptr; }
			}
			else if (!(value.isNull() && spec.value("nullable").toBool())
				&& (!value.isDouble() || !std::isfinite(value.toDouble())
					|| (spec.value("type") == "integer" && value.toDouble() != std::floor(value.toDouble()))
					|| (spec.value("minimum").isDouble() && value.toDouble() < spec.value("minimum").toDouble())
					|| (spec.value("maximum").isDouble() && value.toDouble() > spec.value("maximum").toDouble())))
			{
				return nullptr;
			}
			// The server owns defaults and capability checks. Never submit untouched defaults.
			if (value != spec.value("default")) { params.insert(it.key(), value); }
		}
		const auto speakerId = selection.value("speaker_id").toString();
		if (!speakerId.isEmpty())
		{
			bool found = false;
			for (const auto& entry : model.value("speakers").toArray())
			{
				found |= entry.toObject().value("id") == speakerId;
			}
			if (!found) { return nullptr; }
			params.insert("speaker", speakerId.toInt());
		}
		else if (model.value("multi_speaker").toBool() && model.value("default_speaker").isNull()) { return nullptr; }
		// Conditional values retained in a project are omitted while their condition is inactive.
		if (params.value("target_loudness").isNull() || !params.contains("target_loudness"))
		{
			params.remove("restore_loudness");
		}
		for (const auto& key : {"k_step", "diffusion_method", "diffusion_speedup", "t_start", "reflow_steps"})
		{
			const bool diffusion = QByteArray(key) != "t_start" && QByteArray(key) != "reflow_steps";
			if (!params.value("shallow_diffusion").toBool()
				|| !params.value(diffusion ? "diffusion_model" : "reflow_model").isString())
			{
				params.remove(key);
			}
		}
		auto url = svc_rvc::endpoint(context.address, "/infer");
		url.setQuery(
			QString::fromLatin1("model=" + QUrl::toPercentEncoding(model.value("id").toString()) + "&params="
				+ QUrl::toPercentEncoding(QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact)))),
			QUrl::StrictMode);
		auto job = std::make_unique<Job>();
		job->request = *request;
		job->request.selection_json = nullptr;
		job->token = context.token;
		if (selection.contains("output_sample_rate"))
		{
			const auto value = selection.value("output_sample_rate");
			if (!value.isDouble() || value.toDouble() != value.toInt() || value.toInt() < 8000
				|| value.toInt() > 192000)
			{
				return nullptr;
			}
			job->outputRate = value.toInt();
		}
		if (!job->wave.open()) { return nullptr; }
		job->http = std::make_unique<svc_rvc::Http>(url, context.token, true, 600000);
		return job.release();
	}
	catch (...)
	{
		return nullptr;
	}
}

svc_status pump(void* opaque)
{
	if (!opaque) { return SVC_INVALID; }
	auto& job = *static_cast<Job*>(opaque);
	if (job.terminal != SVC_OK) { return job.terminal; }
	try
	{
		if (!job.decoded)
		{
			job.http->pump(
				[&](uint8_t* data, size_t count) {
					const auto read = job.request.read(job.request.input_user, data, count);
					if (read > 0 && uint64_t(read) <= count)
					{
						job.inputBytes += read;
						job.inputHeader.append(reinterpret_cast<const char*>(data),
							std::min<qint64>(read, SVC_MAX_HEADER - job.inputHeader.size()));
					}
					return read > 0 && job.http->sent() + read > 67108864 ? int64_t(-1) : read;
				},
				[&](const uint8_t* data, size_t count) {
					if (job.http->status() != 200)
					{
						if (job.errorBody.size() + count > SVC_MAX_HEADER) { return false; }
						job.errorBody.append(reinterpret_cast<const char*>(data), count);
						return true;
					}
					if (job.http->header("content-type").split(';').first().trimmed().toLower() != "audio/wav"
						|| job.wave.size() + count > MaxOutputBytes)
					{
						return false;
					}
					return job.wave.write(reinterpret_cast<const char*>(data), count) == qint64(count);
				});
			if (!job.http->error().isEmpty()) { job.fail(job.http->error()); }
			else if (job.http->finished())
			{
				if (job.http->status() != 200)
				{
					job.fail(QString("HTTP %1: %2").arg(job.http->status()).arg(QString::fromUtf8(job.errorBody)));
				}
				else
				{
					job.decode();
				}
			}
			else if (job.reportedBytes != job.http->sent())
			{
				job.reportedBytes = job.http->sent();
				job.deliver(SVC_PROGRESS,
					QJsonDocument(QJsonObject{{"stage", job.http->uploaded() ? "preprocessing" : "upload"},
									  {"uploaded_bytes", double(job.reportedBytes)}})
						.toJson(QJsonDocument::Compact));
			}
			return job.terminal;
		}
		if (job.offset < job.samples)
		{
			const auto count = std::min<uint64_t>(SVC_MAX_FEED / 2, job.samples - job.offset);
			const auto bytes = job.audio->read(count * 2);
			if (uint64_t(bytes.size()) != count * 2) { throw std::runtime_error("Cannot read USVC audio"); }
			++job.chunk;
			if (job.deliver(SVC_AUDIO, bytes, count)) { job.offset += count; }
		}
		else
		{
			if (job.deliver(SVC_DONE)) { job.terminal = SVC_COMPLETE; }
			job.wave.close();
		}
	}
	catch (const std::exception& error)
	{
		try
		{
			job.fail(QString::fromUtf8(error.what()));
		}
		catch (...)
		{
			job.terminal = SVC_FAILED;
		}
	}
	catch (...)
	{
		job.terminal = SVC_FAILED;
	}
	return job.terminal;
}

void cancel(void* opaque)
{
	if (!opaque) { return; }
	auto& job = *static_cast<Job*>(opaque);
	if (job.terminal == SVC_OK)
	{
		job.terminal = SVC_CANCELLED;
		job.http->cancel();
		job.wave.close();
	}
}
void destroy(void* opaque)
{ delete static_cast<Job*>(opaque); }
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
void destroyContext(void* opaque)
{ delete static_cast<Context*>(opaque); }
const char* error(void* opaque)
{ return opaque ? static_cast<Context*>(opaque)->error.constData() : "Invalid context"; }
const svc_engine engine{sizeof(svc_engine), SVC_ABI_VERSION, "USVC", capabilities, start, pump, cancel, destroy};
const svc_plugin plugin{sizeof(svc_plugin), SVC_ABI_VERSION, "USVC", "http://127.0.0.1:8001", &engine, createContext,
	destroyContext, error};
} // namespace
} // namespace svc_usvc

#ifdef _WIN32
#define SVC_EXPORT __declspec(dllexport)
#else
#define SVC_EXPORT __attribute__((visibility("default")))
#endif
extern "C" SVC_EXPORT const svc_plugin* svc_plugin_entry_v1(uint32_t version)
{ return version == SVC_ABI_VERSION ? &svc_usvc::plugin : nullptr; }

#ifdef SVC_LMMS_MODULE
#include "Plugin.h"
extern "C" {
SVC_EXPORT lmms::Plugin::Descriptor svcusvc_plugin_descriptor = {"svcusvc", "USVC", "USVC singing voice conversion API",
	"LMMS contributors", 0x0100, lmms::Plugin::Type::SVC, nullptr, nullptr, nullptr};
SVC_EXPORT lmms::Plugin* lmms_plugin_main(lmms::Model*, void*)
{ return nullptr; }
}
#endif
