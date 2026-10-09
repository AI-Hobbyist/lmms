#include "SVSModel.h"
#include "SVSCapabilities.h"
#include "SVSCurve.h"
#include "SVSTimeMapping.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QCryptographicHash>
#include <cmath>
#include "ConfigManager.h"
#include <cstdlib>
#include <algorithm>
#include <QDebug>
#include <QObject>
#include <QScopeGuard>
#include <QSysInfo>
#include <QUrl>
#include <QRunnable>
namespace lmms::svs {
namespace {
// Queued notifications belong to this target and expire when it is destroyed.
struct HostServices : QObject
{
	QMutex mutex;
	QMap<void*, uint64_t> buffers;
	uint64_t bytes = 0;
	std::atomic<unsigned> pending{0};
	uint64_t request = 0;
	double progress = 0;
	svs_status status = SVS_OK;
	QString diagnostic;
	~HostServices()
	{
		for (auto pointer : buffers.keys())
			std::free(pointer);
	}
	template <class F> void enqueue(F callback, bool completion = false)
	{
		if (pending.fetch_add(1) >= 128 && !completion)
		{
			--pending;
			return;
		}
		QMetaObject::invokeMethod(
			this,
			[this, callback = std::move(callback)] {
				callback();
				--pending;
			},
			Qt::QueuedConnection);
	}
};
void SVS_CALL hostLog(void* context, int32_t level, const char* text)
{
	try
	{
		auto* services = static_cast<HostServices*>(context);
		const auto message = QString::fromUtf8(text ? text : "").left(4096);
		services->enqueue([message, level] {
			if (level > 0)
				qWarning().noquote() << "SVS:" << message;
			else
				qDebug().noquote() << "SVS:" << message;
		});
	}
	catch (...)
	{
	}
}
void SVS_CALL hostProgress(void* context, uint64_t request, double progress, const char* text)
{
	if (!std::isfinite(progress))
		return;
	try
	{
		auto* services = static_cast<HostServices*>(context);
		const auto message = QString::fromUtf8(text ? text : "").left(4096);
		services->enqueue([services, request, progress, message] {
			services->request = request;
			services->progress = std::clamp(progress, 0., 1.);
			services->diagnostic = message;
		});
	}
	catch (...)
	{
	}
}
void SVS_CALL hostCompleted(void* context, uint64_t request, svs_status status, const char* text)
{
	try
	{
		auto* services = static_cast<HostServices*>(context);
		const auto message = QString::fromUtf8(text ? text : "").left(65536);
		services->enqueue(
			[services, request, status, message] {
				services->request = request;
				services->status = status;
				services->diagnostic = message;
			},
			true);
	}
	catch (...)
	{
	}
}
svs_status SVS_CALL hostAllocate(void* context, uint32_t kind, uint64_t bytes, svs_buffer* out)
{
	if (!out || out->size < sizeof(svs_buffer) || (kind != SVS_BUFFER_AUDIO && kind != SVS_BUFFER_RESOURCE)
		|| bytes > (kind == SVS_BUFFER_AUDIO ? 128u : 64u) * 1024 * 1024)
		return SVS_INVALID_INPUT;
	try
	{
		auto* services = static_cast<HostServices*>(context);
		QMutexLocker lock(&services->mutex);
		if (bytes > 128u * 1024 * 1024 - services->bytes)
			return SVS_FAILED;
		std::unique_ptr<void, decltype(&std::free)> memory(
			std::malloc(size_t(std::max(uint64_t(1), bytes))), std::free);
		if (!memory)
			return SVS_FAILED;
		services->buffers.insert(memory.get(), bytes);
		services->bytes += bytes;
		*out = {sizeof(svs_buffer), memory.release(), bytes, services};
		return SVS_OK;
	}
	catch (...)
	{
		return SVS_FAILED;
	}
}
void SVS_CALL hostRelease(void* context, svs_buffer* buffer)
{
	if (!buffer || buffer->size < sizeof(svs_buffer) || buffer->owner != context)
		return;
	try
	{
		auto* services = static_cast<HostServices*>(context);
		QMutexLocker lock(&services->mutex);
		auto it = services->buffers.find(buffer->data);
		if (it == services->buffers.end())
			return;
		services->bytes -= it.value();
		std::free(buffer->data);
		services->buffers.erase(it);
		*buffer = {sizeof(svs_buffer)};
	}
	catch (...)
	{
	}
}
}
struct Plugin::Impl
{
	QLibrary library;
	svs_api api{};
	svs_engine engine = nullptr;
	QString error, identity;
	QMutex mutex;
	HostServices services;
	explicit Impl(const QString& path)
		: library(path)
	{
	}
};
Plugin::Plugin(const QString& path)
	: m_impl(std::make_unique<Impl>(path))
{
	auto& d = *m_impl;
	auto get = reinterpret_cast<svs_get_api_fn>(d.library.resolve("svs_get_api"));
	if (!get)
	{
		d.error = d.library.errorString();
		return;
	}
	QFile binary(d.library.fileName());
	QCryptographicHash identity(QCryptographicHash::Sha256);
	if (binary.open(QIODevice::ReadOnly) && identity.addData(&binary))
		d.identity = QString::fromLatin1(identity.result().toHex());
	if (get(SVS_ABI_MAJOR, SVS_ABI_MINOR, sizeof(d.api), &d.api) != SVS_OK || d.api.major != SVS_ABI_MAJOR
		|| d.api.size < SVS_API_REQUIRED_SIZE || !d.api.create_engine || !d.api.destroy_engine || !d.api.catalog
		|| !d.api.capabilities || !d.api.release_string || !d.api.create_session || !d.api.destroy_session
		|| !d.api.submit || !d.api.render || !d.api.cancel || !d.api.release_result)
	{
		d.error = "Incompatible SVS ABI";
		return;
	}
	if (!SVS_HAS_FIELD(d.api, svs_api, pronunciation))
		d.api.pronunciation = nullptr;
	if (!SVS_HAS_FIELD(d.api, svs_api, open_resource))
		d.api.open_resource = nullptr;
	if (!SVS_HAS_FIELD(d.api, svs_api, read_resource))
		d.api.read_resource = nullptr;
	if (!SVS_HAS_FIELD(d.api, svs_api, close_resource))
		d.api.close_resource = nullptr;
	if (!SVS_HAS_FIELD(d.api, svs_api, query_ranges))
		d.api.query_ranges = nullptr;
	if (!SVS_HAS_FIELD(d.api, svs_api, query_engine_settings))
		d.api.query_engine_settings = nullptr;
	if (!SVS_HAS_FIELD(d.api, svs_api, query_catalog))
		d.api.query_catalog = nullptr;
	svs_host host{sizeof(svs_host), &d.services, hostLog, hostProgress, hostAllocate, hostRelease, hostCompleted};
	if (d.api.create_engine(&host, &d.engine) != SVS_OK || !d.engine)
		d.error = "SVS engine initialization failed";
}
Plugin::~Plugin()
{
	if (m_impl->engine)
		m_impl->api.destroy_engine(m_impl->engine);
	m_impl->library.unload();
}
bool Plugin::valid() const
{
	return m_impl->engine && m_impl->error.isEmpty();
}
QString Plugin::error() const
{
	return m_impl->error;
}
QString Plugin::identity() const
{
	return m_impl->identity;
}
QByteArray Plugin::resource(const QString& id, QString& contentType, QString& error)
{
	QMutexLocker lock(&m_impl->mutex);
	auto& d = *m_impl;
	error.clear();
	contentType.clear();
	if (!valid() || !d.api.open_resource || !d.api.read_resource || !d.api.close_resource)
	{
		error = "SVS resource API unavailable";
		return {};
	}
	const auto key = id.toUtf8();
	svs_resource handle = nullptr;
	svs_resource_info info{};
	info.size = sizeof(info);
	const auto status = d.api.open_resource(d.engine, key.constData(), &handle, &info);
	auto close = qScopeGuard([&] {
		if (handle)
			d.api.close_resource(d.engine, handle);
	});
	if (status != SVS_OK || !handle || info.size < sizeof(info) || !info.content_type || !info.sha256 || !info.id
		|| id != QString::fromUtf8(info.id) || info.byte_count > 64u * 1024 * 1024)
	{
		error = "Invalid or unavailable SVS resource";
		return {};
	}
	QByteArray bytes(qsizetype(info.byte_count), Qt::Uninitialized);
	uint64_t offset = 0;
	while (offset < info.byte_count)
	{
		uint64_t read = 0;
		const auto remaining = info.byte_count - offset;
		if (d.api.read_resource(d.engine, handle, offset, bytes.data() + offset, remaining, &read) != SVS_OK || !read
			|| read > remaining)
		{
			error = "Incomplete SVS resource";
			return {};
		}
		offset += read;
	}
	const auto hash = QByteArray(info.sha256);
	if (hash.size() != 64 || QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != hash)
	{
		error = "SVS resource hash mismatch";
		return {};
	}
	contentType = QString::fromUtf8(info.content_type);
	return bytes;
}
QJsonObject Plugin::engineSettings(const QString& fallbackVoice, const QJsonObject& context, QString& error)
{
	if (!m_impl->api.query_engine_settings)
		return capabilities(fallbackVoice, context, error);
	QMutexLocker lock(&m_impl->mutex);
	const auto request = QJsonDocument(context).toJson(QJsonDocument::Compact);
	const char* text = nullptr;
	const auto status = m_impl->api.query_engine_settings(m_impl->engine, request.constData(), &text);
	if (status == SVS_UNSUPPORTED)
	{
		if (text)
			m_impl->api.release_string(m_impl->engine, text);
		error.clear();
		return {};
	}
	if (status != SVS_OK || !text)
	{
		if (text)
			m_impl->api.release_string(m_impl->engine, text);
		error = "SVS engine settings query failed";
		return {};
	}
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(text, &parse);
	m_impl->api.release_string(m_impl->engine, text);
	if (parse.error != QJsonParseError::NoError || !document.isObject()
		|| document.object()["schemaVersion"].toInt() != 1)
	{
		error = "Invalid SVS engine settings declaration";
		return {};
	}
	error.clear();
	return document.object();
}
QJsonObject Plugin::capabilities(const QString& voice, const QJsonObject& context, QString& error)
{
	QMutexLocker lock(&m_impl->mutex);
	const char* text = nullptr;
	auto voiceBytes = voice.toUtf8(), request = QJsonDocument(context).toJson(QJsonDocument::Compact);
	if (!valid()
		|| m_impl->api.capabilities(m_impl->engine, voiceBytes.constData(), request.constData(), &text) != SVS_OK
		|| !text)
	{
		error = "SVS capability query failed";
		return {};
	}
	QJsonParseError parse;
	auto document = QJsonDocument::fromJson(text, &parse);
	m_impl->api.release_string(m_impl->engine, text);
	if (parse.error != QJsonParseError::NoError || !document.isObject())
	{
		error = "Invalid capability JSON";
		return {};
	}
	error.clear();
	return document.object();
}
QJsonObject Plugin::pronunciation(const QString& voice, const QJsonObject& context, QString& error)
{
	QMutexLocker lock(&m_impl->mutex);
	const char* text = nullptr;
	if (!valid() || !m_impl->api.pronunciation)
	{
		error = "Plugin pronunciation parser unavailable";
		return {};
	}
	auto voiceBytes = voice.toUtf8(), request = QJsonDocument(context).toJson(QJsonDocument::Compact);
	if (m_impl->api.pronunciation(m_impl->engine, voiceBytes.constData(), request.constData(), &text) != SVS_OK
		|| !text)
	{
		error = "SVS pronunciation query failed";
		return {};
	}
	QJsonParseError parse;
	auto document = QJsonDocument::fromJson(text, &parse);
	m_impl->api.release_string(m_impl->engine, text);
	if (parse.error != QJsonParseError::NoError || !document.isObject())
	{
		error = "Invalid pronunciation JSON";
		return {};
	}
	error.clear();
	return document.object();
}
bool Plugin::hasCatalogQuery() const
{
	return m_impl->api.query_catalog != nullptr;
}
QVector<Voice> Plugin::voices(const QString& package, const QString& id, const QJsonObject& context,
	QJsonObject* declaration, QString* diagnostic)
{
	if (diagnostic)
		diagnostic->clear();
	QMutexLocker lock(&m_impl->mutex);
	QVector<Voice> voices;
	const char* text = nullptr;
	const auto request = QJsonDocument(context).toJson(QJsonDocument::Compact);
	if (!valid())
		return voices;
	const auto status = m_impl->api.query_catalog && !context.isEmpty()
		? m_impl->api.query_catalog(m_impl->engine, request.constData(), &text)
		: m_impl->api.catalog(m_impl->engine, &text);
	if (status != SVS_OK || !text)
	{
		if (diagnostic)
			*diagnostic = text ? QString::fromUtf8(text) : QString("SVS catalog query failed (%1)").arg(status);
		if (text)
			m_impl->api.release_string(m_impl->engine, text);
		return voices;
	}
	QJsonParseError error;
	auto document = QJsonDocument::fromJson(text, &error);
	m_impl->api.release_string(m_impl->engine, text);
	if (error.error != QJsonParseError::NoError)
	{
		if (diagnostic)
			*diagnostic = "Invalid voice catalog JSON";
		return voices;
	}
	if (!document.isObject() || !document.object()["voices"].isArray())
	{
		if (diagnostic)
			*diagnostic = "Invalid voice catalog declaration";
		return {};
	}
	if (declaration)
		*declaration = document.object();
	QSet<QString> ids;
	for (const auto& item : document.object()["voices"].toArray())
	{
		auto v = item.toObject();
		auto voiceId = v["id"].toString();
		if (voiceId.isEmpty() || ids.contains(voiceId))
		{
			if (diagnostic)
				*diagnostic = "Duplicate or missing voice ID";
			if (declaration)
				*declaration = {};
			return {};
		}
		ids.insert(voiceId);
		auto resource = [&](const QString& key) {
			auto path = v[key].toString();
			if (path.isEmpty())
				return QString{};
			auto resolved = QFileInfo(QDir(package).filePath(path)).canonicalFilePath();
			auto root = QFileInfo(package).canonicalFilePath() + "/";
			if (!resolved.isEmpty() && resolved.startsWith(root, Qt::CaseInsensitive))
				return resolved;
			if (m_impl->api.open_resource && m_impl->api.read_resource && m_impl->api.close_resource)
				return "svs-resource:" + QString::fromLatin1(QUrl::toPercentEncoding(id)) + ":"
					+ QString::fromLatin1(QUrl::toPercentEncoding(path));
			return QString{};
		};
		voices.push_back({id, voiceId, v["name"].toString(), v["version"].toString(), v["defaultLanguage"].toString(),
			v["defaultLyric"].toString(), resource("avatar"), resource("portrait"), package, v});
	}
	return voices;
}
std::shared_ptr<const Audio> Plugin::render(
	const Input& input, QString& error, const std::shared_ptr<RenderControl>& control)
{
	if (control && control->cancelled)
	{
		error = "Cancelled";
		return {};
	}
	QMutexLocker lock(&m_impl->mutex);
	auto& d = *m_impl;
	if (!valid())
	{
		error = d.error;
		return {};
	}
	if (control && control->cancelled)
	{
		error = "Cancelled";
		return {};
	}
	auto voice = input.voiceId.toUtf8();
	svs_session session = nullptr;
	TimeMapping inputMapping;
	if (!readTimeMapping(input.document, input.secondsPerTick, inputMapping, error))
		return {};
	if (d.api.create_session(d.engine, voice.constData(), &session) != SVS_OK || !session)
	{
		error = "Voice session unavailable";
		return {};
	}
	struct Strings
	{
		QByteArray id, lyric, language, pronunciation, phonemes, parameters;
	};
	std::vector<Strings> strings;
	strings.reserve(input.notes.size());
	std::vector<svs_note> notes;
	notes.reserve(input.notes.size());
	for (const auto& n : input.notes)
	{
		strings.push_back({n.id.toUtf8(), n.lyric.toUtf8(), n.language.toUtf8(), n.pronunciation.toUtf8(),
			QJsonDocument(n.phonemes).toJson(QJsonDocument::Compact),
			QJsonDocument(n.parameters).toJson(QJsonDocument::Compact)});
		auto& s = strings.back();
		notes.push_back({sizeof(svs_note), s.id.constData(), n.tick, n.duration, inputMapping.localSeconds(n.tick),
			inputMapping.localSeconds(n.tick + n.duration) - inputMapping.localSeconds(n.tick), n.pitch,
			s.lyric.constData(), s.language.constData(), s.pronunciation.constData(), s.phonemes.constData(),
			s.parameters.constData()});
	}
	auto document = input.document;
	Capabilities cap;
	if (document["queryCapabilities"].toBool())
	{
		auto context = document;
		const auto parameters = context["trackParameters"].toObject();
		for (auto i = parameters.begin(); i != parameters.end(); ++i)
			context[i.key()] = i.value();
		QJsonArray contextNotes;
		for (const auto& note : input.notes)
			contextNotes.append(QJsonObject{{"id", note.id}, {"tick", note.tick}, {"duration", note.duration},
				{"pitch", note.pitch}, {"lyric", note.lyric}, {"language", note.language},
				{"pronunciation", note.pronunciation}, {"parameters", note.parameters}, {"phonemes", note.phonemes}});
		context["notes"] = contextNotes;
		context["noteCount"] = input.notes.size();
		const auto json = QJsonDocument(context).toJson(QJsonDocument::Compact);
		const char* schema = nullptr;
		auto status = d.api.capabilities(d.engine, voice.constData(), json.constData(), &schema);
		QJsonParseError parse;
		const auto queried = schema ? QJsonDocument::fromJson(schema, &parse) : QJsonDocument{};
		if (schema)
			d.api.release_string(d.engine, schema);
		if (status != SVS_OK || queried.isNull() || !queried.isObject() || parse.error != QJsonParseError::NoError)
		{
			error = "Snapshot capability query failed";
			d.api.destroy_session(session);
			return {};
		}
		document["capabilities"] = queried.object();
	}
	if (!Capabilities::parse(document["capabilities"].toObject(), cap, error))
	{
		d.api.destroy_session(session);
		return {};
	}
	// Send effective values through the existing ABI; cache/source data stay unshifted.
	auto effectiveCurves = document["curves"].toObject();
	const auto globals = document["globalParameters"].toObject();
	for (const auto& p : cap.parameters)
	{
		if (!globalParameter(p))
			continue;
		const auto base
			= parameterBase(p, document["trackParameters"].toObject(), document["clipParameters"].toObject(), globals);
		if (effectiveCurves.contains(p.id) && (p.type == "float" || p.type == "int"))
		{
			Curve curve;
			QString reason;
			if (!Curve::fromJson(effectiveCurves[p.id].toObject(), curve, reason, &p))
			{
				error = reason;
				d.api.destroy_session(session);
				return {};
			}
			effectiveCurves[p.id] = withParameterBase(curve, p, base).toJson();
		}
		if (p.scope == "note" && globals.contains(p.id))
			for (size_t index = 0; index < strings.size(); ++index)
			{
				auto values = input.notes[int(index)].parameters;
				if (p.type == "float" || p.type == "int")
				{
					const auto original = values.value(p.id).toDouble(p.defaultValue.toDouble());
					values[p.id]
						= std::clamp(original + base.toDouble() - p.defaultValue.toDouble(), p.minimum, p.maximum);
				}
				else
					values[p.id] = base;
				// Preserve changes made for other parameters in this same snapshot.
				auto prepared = QJsonDocument::fromJson(strings[index].parameters).object();
				prepared[p.id] = values[p.id];
				strings[index].parameters = QJsonDocument(prepared).toJson(QJsonDocument::Compact);
				notes[index].parameters_json = strings[index].parameters.constData();
			}
	}
	document["curves"] = effectiveCurves;
	if (cap.pitchInput == "offset")
	{
		auto curves = document["curves"].toObject();
		Curve reference;
		if (!Curve::fromJson(cap.original["pitch"].toObject()["referencePitch"].toObject(), reference, error))
		{
			d.api.destroy_session(session);
			return {};
		}
		if (curves.contains("svs.pitch"))
		{
			Curve absolute, offset;
			if (!Curve::fromJson(curves["svs.pitch"].toObject(), absolute, error)
				|| !absolutePitchToOffset(absolute, reference, offset, error))
			{
				d.api.destroy_session(session);
				return {};
			}
			curves["svs.pitch"] = offset.toJson();
		}
		curves["svs.referencePitch"] = reference.toJson();
		document["curves"] = curves;
	}
	QVector<Dictionary> voiceDictionaries, projectDictionaries;
	for (const auto& item : document["voiceDictionaries"].toArray())
	{
		auto value = item.toObject();
		Dictionary dictionary;
		dictionary.id = value["id"].toString();
		dictionary.version = value["version"].toString();
		dictionary.hash = value["hash"].toString();
		dictionary.language = value["language"].toString();
		dictionary.phonemeSet = value["phonemeSet"].toString();
		dictionary.entries = value["entries"].toObject();
		voiceDictionaries.push_back(dictionary);
	}
	QJsonArray dictionaryDiagnostics;
	for (const auto& item : document["projectDictionaries"].toArray())
	{
		Dictionary dictionary;
		QString diagnostic;
		if (Dictionary::parse(
				QJsonDocument(item.toObject()).toJson(QJsonDocument::Compact), cap.phonemeSet, dictionary, diagnostic)
			&& dictionary.phonemeSet == cap.phonemeSetId && cap.languages.contains(dictionary.language))
			projectDictionaries.push_back(dictionary);
		else
			dictionaryDiagnostics.append(QJsonObject{{"dictionaryId", item.toObject()["id"]},
				{"message",
					diagnostic.isEmpty() ? QString("Dictionary language/phoneme set incompatible with voice")
										 : diagnostic}});
	}
	QVector<Note> ordered = input.notes;
	std::stable_sort(ordered.begin(), ordered.end(), [](const Note& a, const Note& b) { return a.tick < b.tick; });
	QJsonObject pronunciations;
	Pronunciation previousResult;
	for (int i = 0; i < ordered.size(); ++i)
	{
		const auto& note = ordered[i];
		auto resolved = resolvePronunciation(note, cap, voiceDictionaries, projectDictionaries,
			document["language"].toString(cap.defaultLanguage), i ? &ordered[i - 1] : nullptr,
			i ? &previousResult : nullptr);
		auto value = resolved.toJson();
		if (!resolved.generated && resolved.source != "continuation" && !note.phonemes.contains("symbols")
			&& cap.languages.contains(note.language.isEmpty() ? document["language"].toString() : note.language)
			&& d.api.pronunciation)
		{
			auto request = QJsonDocument(
				QJsonObject{{"lyric", note.lyric}, {"pronunciation", note.pronunciation},
					{"language", note.language.isEmpty() ? document["language"].toString() : note.language}})
							   .toJson(QJsonDocument::Compact);
			const char* text = nullptr;
			if (d.api.pronunciation(d.engine, voice.constData(), request.constData(), &text) == SVS_OK && text)
			{
				auto parsed = QJsonDocument::fromJson(text).object();
				d.api.release_string(d.engine, text);
				if (!parsed.isEmpty())
				{
					value = parsed;
					if (!note.pronunciation.isEmpty())
						value["source"] = "manualPronunciation";
				}
			}
		}
		if (!value.contains("phonemeSet"))
			value["phonemeSet"] = cap.phonemeSetId;
		pronunciations[note.id] = value;
		previousResult = resolved;
		previousResult.generated = value["generated"].toBool();
		previousResult.continuation = value["continuation"].toBool();
		previousResult.text = value["text"].toString();
		previousResult.diagnostic = value["diagnostic"].toString();
		previousResult.phonemes.clear();
		for (const auto& symbol : value["phonemes"].toArray())
			previousResult.phonemes << symbol.toString();
	}
	document["pronunciations"] = pronunciations;
	document["originSeconds"] = inputMapping.globalSeconds(0);
	auto id = input.clipId.toUtf8();
	auto json = QJsonDocument(document).toJson(QJsonDocument::Compact);
	svs_snapshot snapshot{sizeof(svs_snapshot), id.constData(), input.generation, input.revision, input.request,
		voice.constData(), input.rate, notes.data(), uint32_t(notes.size()), input.duration, json.constData()};
	auto status = d.api.submit(session, &snapshot);
	svs_result result{};
	result.size = sizeof(result);
	QJsonArray ranges;
	if (status == SVS_OK && d.api.query_ranges)
	{
		const char* text = nullptr;
		status = d.api.query_ranges(session, &text);
		QJsonParseError parse;
		const auto declaration = text ? QJsonDocument::fromJson(text, &parse) : QJsonDocument{};
		if (text)
			d.api.release_string(d.engine, text);
		if (status != SVS_OK || parse.error != QJsonParseError::NoError || !declaration.isObject()
			|| !declaration.object()["ranges"].isArray())
		{
			d.api.destroy_session(session);
			error = "Invalid SVS synthesis range declaration";
			return {};
		}
		ranges = declaration.object()["ranges"].toArray();
		QSet<QString> ids;
		double previous = -INFINITY;
		if (ranges.isEmpty() || ranges.size() > 100000)
		{
			d.api.destroy_session(session);
			error = "Invalid SVS synthesis range count";
			return {};
		}
		for (const auto& item : ranges)
		{
			const auto range = item.toObject();
			const auto id = range["id"].toString();
			const auto start = range["startTick"].toDouble(NAN), end = range["endTick"].toDouble(NAN);
			if (id.isEmpty() || ids.contains(id) || !std::isfinite(start) || !std::isfinite(end) || start < previous
				|| end <= start)
			{
				d.api.destroy_session(session);
				error = "Invalid SVS synthesis range boundaries or ID";
				return {};
			}
			ids.insert(id);
			previous = end;
		}
	}
	if (status == SVS_OK)
	{
		if (control)
			control->attach([&d, session] { d.api.cancel(session); });
		if (control && control->cancelled)
			status = SVS_CANCELLED;
		else
			status = d.api.render(session, &result);
	}
	if (control)
		control->detach();
	std::shared_ptr<Audio> audio;
	if ((!control || !control->cancelled) && status == SVS_OK && result.size >= sizeof(result) && result.channels == 2
		&& result.sample_rate >= 8000 && result.sample_rate <= 192000 && result.frame_count <= 16 * 1024 * 1024
		&& std::isfinite(result.start_seconds) && input.secondsPerTick > 0 && (!result.frame_count || result.audio))
	{
		audio = std::make_shared<Audio>();
		audio->rate = result.sample_rate;
		audio->revision = input.revision;
		if (result.frame_count)
			audio->samples.assign(result.audio, result.audio + result.frame_count * 2);
		audio->feedback = QJsonDocument::fromJson(result.feedback_json ? result.feedback_json : "{}").object();
		audio->feedback["pronunciations"] = pronunciations;
		audio->feedback["dictionaryDiagnostics"] = dictionaryDiagnostics;
		if (!ranges.isEmpty())
			audio->feedback["ranges"] = ranges;
		audio->mapping = inputMapping;
		audio->startSeconds = result.start_seconds;
		audio->startTick = inputMapping.resultStartTick(result.start_seconds);
		for (float sample : audio->samples)
			if (!std::isfinite(sample))
			{
				error = "Non-finite SVS audio";
				audio.reset();
				break;
			}
		if (audio)
			audio->waveform.build(audio->samples);
	}
	else
	{
		error = QString("SVS synthesis failed (%1)").arg(status);
		if (result.error_json)
		{
			const auto diagnostic = QJsonDocument::fromJson(result.error_json).object();
			const auto message = diagnostic["message"].toString();
			if (!message.isEmpty())
				error += ": " + message;
			for (const auto* key : {"noteId", "parameterId", "rangeId"})
				if (diagnostic[key].isString())
					error += " [" + QString::fromLatin1(key) + "=" + diagnostic[key].toString() + "]";
		}
	}
	d.api.release_result(session, &result);
	d.api.destroy_session(session);
	return audio;
}
Registry::Registry()
{
	m_catalogPool.setMaxThreadCount(1);
}
Registry::~Registry()
{
	for (const auto& generation : m_catalogGenerations)
		++*generation;
	m_catalogPool.clear();
	m_catalogPool.waitForDone();
}
Registry& Registry::instance()
{
	static Registry registry;
	return registry;
}
const QVector<Voice>& Registry::voices()
{
	if (m_scanned)
		return m_voices;
	m_scanned = true;
	QStringList roots{QCoreApplication::applicationDirPath() + "/svs",
		QCoreApplication::applicationDirPath() + "/../svs", ConfigManager::inst()->dataDir() + "svs"};
	if (qEnvironmentVariableIsSet("LMMS_SVS_PLUGIN_DIR"))
		roots.prepend(qEnvironmentVariable("LMMS_SVS_PLUGIN_DIR"));
	for (const auto& root : roots)
		for (const auto& folder : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
		{
			QString package = QDir(root).filePath(folder);
			QFile file(package + "/manifest.json");
			if (!file.open(QIODevice::ReadOnly))
				continue;
			auto manifest = QJsonDocument::fromJson(file.read(65537)).object();
			auto id = manifest["id"].toString();
			auto entry = manifest["entry"].toString();
			if (id.isEmpty() || manifest["category"] != "Singing Voice Synthesis"
				|| manifest["apiMajor"].toInt() != SVS_ABI_MAJOR || entry.contains('/') || entry.contains('\\')
				|| entry.contains(".."))
			{
				m_diagnostics << "Invalid SVS manifest: " + package;
				continue;
			}
			auto architecture = QSysInfo::currentCpuArchitecture();
			if (architecture == "x86_64")
				architecture = "x64";
			else if (architecture == "i386")
				architecture = "x86";
			else if (architecture == "aarch64")
				architecture = "arm64";
			if (manifest["architecture"].toString() != architecture)
			{
				m_diagnostics << QString("SVS architecture mismatch: %1 declares %2; host %3")
									 .arg(package, manifest["architecture"].toString(), architecture);
				continue;
			}
#if defined(Q_OS_WIN)
			const auto platform = QStringLiteral("windows");
			const auto pathSensitivity = Qt::CaseInsensitive;
#elif defined(Q_OS_MACOS)
			const auto platform = QStringLiteral("macos");
			const auto pathSensitivity = Qt::CaseSensitive;
#else
			const auto platform = QStringLiteral("linux");
			const auto pathSensitivity = Qt::CaseSensitive;
#endif
			if (manifest["platform"].toString() != platform)
			{
				m_diagnostics << QString("SVS platform mismatch: %1 declares %2; host %3")
									 .arg(package, manifest["platform"].toString(), platform);
				continue;
			}
			if (m_plugins.contains(id))
			{
				const auto existing = std::find_if(
					m_engines.begin(), m_engines.end(), [&](const auto& engine) { return engine.id == id; });
				if (existing == m_engines.end()
					|| QFileInfo(existing->package)
							.canonicalFilePath()
							.compare(QFileInfo(package).canonicalFilePath(), pathSensitivity)
						!= 0)
					m_diagnostics << "Duplicate SVS plugin ID: " + id + " in " + package;
				continue;
			}
			auto plugin = std::make_shared<Plugin>(package + "/" + entry);
			if (!plugin->valid())
			{
				m_diagnostics << id + ": " + plugin->error();
				continue;
			}
			m_plugins[id] = plugin;
			m_engines.push_back(
				{id, manifest["name"].toString(id), manifest["engineType"].toString(), package, manifest});
			const auto settings = QJsonDocument::fromJson(
				ConfigManager::inst()
					->value("svsEngineSettings", "engine_" + QString::fromLatin1(id.toUtf8().toHex()))
					.toUtf8())
									  .object();
			refreshCatalogAsync(id, settings, false);
		}
	return m_voices;
}
std::shared_ptr<Plugin> Registry::plugin(const QString& id)
{
	voices();
	return m_plugins.value(id);
}
QJsonObject Registry::catalogContext(const QString& id, const QJsonObject& settings, bool rescan) const
{
	const auto key = "engine_" + QString::fromLatin1(id.toUtf8().toHex());
	auto* config = ConfigManager::inst();
	const auto registry = QJsonDocument::fromJson(config->value("svsInstallations", key).toUtf8()).array();
	return {{"engineSettings", settings}, {"installations", registry}, {"rescan", rescan},
		{"defaultVoicebankDirectory", config->workingDir() + "voicebanks/DiffSinger"}};
}
bool Registry::refreshCatalog(const QString& id, const QJsonObject& settings, QString& error, bool rescan)
{
	voices();
	const auto plugin = m_plugins.value(id);
	const auto found
		= std::find_if(m_engines.begin(), m_engines.end(), [&](const auto& engine) { return engine.id == id; });
	if (!plugin || found == m_engines.end())
	{
		error = "SVS engine unavailable";
		return false;
	}
	auto& generation = m_catalogGenerations[id];
	if (!generation)
		generation = std::make_shared<std::atomic<uint64_t>>(0);
	++*generation;
	QJsonObject declaration;
	auto next = plugin->voices(found->package, id, catalogContext(id, settings, rescan), &declaration, &error);
	m_scanning.remove(id);
	if (error.isEmpty() && !declaration.contains("voices"))
		error = "SVS catalog query failed";
	if (error.isEmpty())
		publishCatalog(id, std::move(next), declaration);
	emit catalogScanFinished(id, error);
	return error.isEmpty();
}
void Registry::refreshCatalogAsync(const QString& id, const QJsonObject& settings, bool rescan)
{
	voices();
	const auto plugin = m_plugins.value(id);
	const auto found
		= std::find_if(m_engines.begin(), m_engines.end(), [&](const auto& engine) { return engine.id == id; });
	if (!plugin || found == m_engines.end())
	{
		emit catalogScanFinished(id, "SVS engine unavailable");
		return;
	}
	auto& current = m_catalogGenerations[id];
	if (!current)
		current = std::make_shared<std::atomic<uint64_t>>(0);
	const auto generation = current;
	const auto request = ++*generation;
	const auto package = found->package;
	const auto context = catalogContext(id, settings, rescan);
	m_scanning.insert(id);
	emit catalogScanStarted(id);
	m_catalogPool.start(QRunnable::create([this, plugin, id, package, context, generation, request] {
		if (generation->load() != request)
			return;
		QString error;
		QJsonObject declaration;
		QVector<Voice> next;
		try
		{
			next = plugin->voices(package, id, context, &declaration, &error);
			if (error.isEmpty() && !declaration.contains("voices"))
				error = "SVS catalog query failed";
		}
		catch (...)
		{
			error = "SVS catalog worker exception";
		}
		QMetaObject::invokeMethod(
			this,
			[this, id, generation, request, next = std::move(next), declaration, error]() mutable {
				if (generation->load() != request)
					return;
				m_scanning.remove(id);
				if (error.isEmpty())
					publishCatalog(id, std::move(next), declaration);
				else
					m_diagnostics << id + ": " + error;
				emit catalogScanFinished(id, error);
			},
			Qt::QueuedConnection);
	}));
}
void Registry::publishCatalog(const QString& id, QVector<Voice> next, const QJsonObject& declaration)
{
	const auto found
		= std::find_if(m_engines.begin(), m_engines.end(), [&](const auto& engine) { return engine.id == id; });
	if (found == m_engines.end())
		return;
	for (auto& voice : next)
	{
		voice.metadata["pluginVersion"] = found->manifest["version"];
		voice.metadata["pluginName"] = found->name;
		voice.metadata["engineType"] = found->type;
	}
	for (const auto& diagnostic : declaration["diagnostics"].toArray())
	{
		m_diagnostics << id + ": " + diagnostic.toObject()["message"].toString();
	}
	if (declaration["installations"].isArray())
		ConfigManager::inst()->setValue("svsInstallations", "engine_" + QString::fromLatin1(id.toUtf8().toHex()),
			QString::fromUtf8(QJsonDocument(declaration["installations"].toArray()).toJson(QJsonDocument::Compact)));
	QVector<Voice> previous;
	for (const auto& voice : m_voices)
		if (voice.pluginId == id)
			previous.append(voice);
	m_voices.erase(
		std::remove_if(m_voices.begin(), m_voices.end(), [&](const auto& voice) { return voice.pluginId == id; }),
		m_voices.end());
	m_voices += next;
	bool changed = previous.size() != next.size();
	if (!changed)
		for (int index = 0; index < next.size(); ++index)
			if (previous[index].metadata != next[index].metadata)
			{
				changed = true;
				break;
			}
	if (changed)
		emit catalogChanged(id);
}
}
