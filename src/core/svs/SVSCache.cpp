#include "SVSCache.h"
#include "ConfigManager.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDateTime>
#include <QtEndian>
#include <cmath>
#include <algorithm>
namespace lmms::svs {
namespace {
QJsonValue feedbackIds(const QJsonValue& value, const QMap<QString, QString>& ids)
{
	if (value.isArray())
	{
		QJsonArray array;
		for (const auto& item : value.toArray())
			array.append(feedbackIds(item, ids));
		return array;
	}
	if (!value.isObject())
		return value;
	auto object = value.toObject();
	if (object.contains("noteId"))
		object["noteId"] = ids.value(object["noteId"].toString(), object["noteId"].toString());
	for (auto i = object.begin(); i != object.end(); ++i)
	{
		if (i.key() == "pronunciations")
		{
			QJsonObject mapped;
			const auto source = i.value().toObject();
			for (auto j = source.begin(); j != source.end(); ++j)
				mapped[ids.value(j.key(), j.key())] = feedbackIds(j.value(), ids);
			i.value() = mapped;
		}
		else if (i.key() != "noteId")
			i.value() = feedbackIds(i.value(), ids);
	}
	return object;
}
QMap<QString, QString> noteIds(const Input& input, bool restore)
{
	QMap<QString, QString> result;
	for (int i = 0; i < input.notes.size(); ++i)
	{
		const auto stable = "@note" + QString::number(i);
		if (restore)
			result[stable] = input.notes[i].id;
		else
			result[input.notes[i].id] = stable;
	}
	return result;
}
std::shared_ptr<const Audio> bindCachedAudio(
	const std::shared_ptr<const Audio>& source, const Input& input, const QString& key)
{
	auto audio = std::make_shared<Audio>(*source);
	QString error;
	if (!readTimeMapping(input.document, input.secondsPerTick, audio->mapping, error))
		return {};
	audio->revision = input.revision;
	audio->feedback = feedbackIds(source->feedback, noteIds(input, true)).toObject();
	audio->cacheKey = key;
	audio->cacheInputHash = Cache::editableKey(input);
	return audio;
}
constexpr qint64 MaximumFile = 136 * 1024 * 1024, MaximumMetadata = 4 * 1024 * 1024;
bool validKey(const QString& key)
{
	if (key.size() != 64)
		return false;
	for (const auto ch : key)
		if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
			return false;
	return true;
}
QByteArray waveBytes(const Audio& audio)
{
	if (audio.samples.size() > 32 * 1024 * 1024 || audio.samples.size() % 2 || audio.rate < 8000 || audio.rate > 192000)
		return {};
	QByteArray bytes;
	QDataStream stream(&bytes, QIODevice::WriteOnly);
	stream.setByteOrder(QDataStream::LittleEndian);
	stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
	stream.writeRawData("RIFF", 4);
	stream << quint32(36 + audio.samples.size() * 4);
	stream.writeRawData("WAVEfmt ", 8);
	stream << quint32(16) << quint16(3) << quint16(2) << quint32(audio.rate) << quint32(audio.rate * 8) << quint16(8)
		   << quint16(32);
	stream.writeRawData("data", 4);
	stream << quint32(audio.samples.size() * 4);
	for (float sample : audio.samples)
	{
		if (!std::isfinite(sample))
			return {};
		stream << sample;
	}
	return stream.status() == QDataStream::Ok ? bytes : QByteArray{};
}
bool readWave(const QByteArray& bytes, Audio& audio)
{
	if (bytes.size() < 44 || bytes.size() > MaximumFile || bytes.mid(0, 4) != "RIFF" || bytes.mid(8, 8) != "WAVEfmt "
		|| bytes.mid(36, 4) != "data")
		return false;
	auto u32 = [&](int offset) { return qFromLittleEndian<quint32>(bytes.constData() + offset); };
	auto u16 = [&](int offset) { return qFromLittleEndian<quint16>(bytes.constData() + offset); };
	const auto size = u32(40), rate = u32(24);
	if (u32(4) != bytes.size() - 8 || u32(16) != 16 || u16(20) != 3 || u16(22) != 2 || rate < 8000 || rate > 192000
		|| u32(28) != rate * 8 || u16(32) != 8 || u16(34) != 32 || size % 8 || size != bytes.size() - 44
		|| size > 128 * 1024 * 1024)
		return false;
	audio.rate = rate;
	audio.samples.resize(size / 4);
	QDataStream stream(bytes.mid(44));
	stream.setByteOrder(QDataStream::LittleEndian);
	stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
	for (auto& value : audio.samples)
	{
		stream >> value;
		if (!std::isfinite(value))
			return false;
	}
	return stream.status() == QDataStream::Ok;
}
struct DiskFiles
{
	QFileInfoList indices;
	QMap<QString, QString> audioByIndex;
	QMap<QString, int> references;
	qint64 bytes = 0;
};
DiskFiles diskFiles(const QString& root)
{
	DiskFiles result;
	for (const auto& directory : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks))
	{
		for (const auto& file : QDir(directory.absoluteFilePath())
				 .entryInfoList({"*.svsmeta", "*.svscache"}, QDir::Files | QDir::NoSymLinks))
		{
			if (!validKey(file.completeBaseName()))
				continue;
			result.indices.append(file);
			result.bytes += file.size();
			if (file.suffix() != "svsmeta" || file.size() > MaximumMetadata)
				continue;
			QFile index(file.absoluteFilePath());
			if (!index.open(QIODevice::ReadOnly))
				continue;
			const auto sha = QJsonDocument::fromJson(index.readAll()).object()["audioSHA256"].toString();
			if (!validKey(sha))
				continue;
			const auto audio = QDir(directory.absoluteFilePath()).filePath(sha + ".wav");
			const QFileInfo info(audio);
			if (!info.isFile() || info.isSymLink())
				continue;
			result.audioByIndex[file.absoluteFilePath()] = audio;
			if (result.references[audio]++ == 0)
				result.bytes += info.size();
		}
	}
	return result;
}
}
Cache& Cache::instance()
{
	static Cache cache(ConfigManager::inst()->workingDir() + "cache/SVS", 128 * 1024 * 1024, 512 * 1024 * 1024,
		QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/svs-v1");
	return cache;
}
Cache::Cache(QString directory, qint64 memoryLimit, qint64 diskLimit, QString legacyDirectory)
	: m_directory(QDir(directory).absolutePath())
	, m_legacyDirectory(std::move(legacyDirectory))
	, m_memoryLimit(std::max(qint64(0), memoryLimit))
	, m_diskLimit(std::max(qint64(0), diskLimit))
{
	QDir().mkpath(m_directory);
	trimDisk();
}
QString Cache::engineDirectory(const QString& pluginId) const
{
	const auto folder = pluginId == "org.lmms.svs.diffsinger" ? QString("DiffSinger")
		: pluginId == "org.lmms.svs.example"				  ? QString("SVSExample")
		: pluginId.isEmpty()								  ? QString("Unknown")
															  : "Engine-"
			+ QString::fromLatin1(QCryptographicHash::hash(pluginId.toUtf8(), QCryptographicHash::Sha256).toHex());
	return QDir(m_directory).filePath(folder);
}
QString Cache::path(const QString& key, const Input& input) const
{
	return validKey(key) ? QDir(engineDirectory(input.document["pluginId"].toString())).filePath(key + ".svsmeta")
						 : QString{};
}
QString Cache::key(const Input& input, const QString& identity)
{
	auto document = input.document;
	document.remove("clipId");
	document.remove("queryCapabilities");
	document.remove("cacheDirectory");
	if (document["pluginId"].toString() == "org.lmms.svs.diffsinger")
	{
		auto settings = document["engineSettings"].toObject();
		settings.remove("diffsinger.voicebankDirectories");
		settings.remove("diffsinger.showPhonemeLanguagePrefix");
		document["engineSettings"] = settings;
	}
	QJsonArray notes;
	for (const auto& note : input.notes)
		notes.append(QJsonObject{{"tick", note.tick}, {"duration", note.duration}, {"pitch", note.pitch},
			{"lyric", note.lyric}, {"language", note.language}, {"pronunciation", note.pronunciation},
			{"parameters", note.parameters}, {"phonemes", note.phonemes}});
	document["notes"] = notes;
	document["pluginIdentity"] = identity;
	document["voiceId"] = input.voiceId;
	document["sampleRate"] = int(input.rate);
	document["channels"] = 2;
	document["format"] = "float32";
	document["secondsPerTick"] = input.secondsPerTick;
	document["durationSeconds"] = input.duration;
	return QString::fromLatin1(
		QCryptographicHash::hash(QJsonDocument(document).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
			.toHex());
}
QString Cache::editableKey(const Input& source)
{
	auto input = source;
	for (const auto& field : QStringList{"capabilities", "voiceDictionaries", "tempoMap", "cacheOnlyKey"})
		input.document.remove(field);
	input.duration = 0;
	return key(input, {});
}
void Cache::remember(const QString& key, std::shared_ptr<const Audio> audio)
{
	const auto cost = qint64(audio->samples.size() * sizeof(float) + audio->waveform.bytes()
		+ QJsonDocument(audio->feedback).toJson(QJsonDocument::Compact).size()
		+ (audio->mapping.tempo ? audio->mapping.tempo->bytes() : 0));
	if (cost > m_memoryLimit)
		return;
	if (m_entries.contains(key))
	{
		m_memoryBytes -= m_entries[key].cost;
		m_entries.remove(key);
	}
	while (m_memoryBytes + cost > m_memoryLimit && !m_entries.isEmpty())
	{
		auto oldest = m_entries.begin();
		for (auto i = m_entries.begin(); i != m_entries.end(); ++i)
			if (i->access < oldest->access)
				oldest = i;
		m_memoryBytes -= oldest->cost;
		m_entries.erase(oldest);
	}
	m_entries[key] = {std::move(audio), cost, ++m_access};
	m_memoryBytes += cost;
}
std::shared_ptr<const Audio> Cache::get(const QString& key, const Input& input)
{
	QMutexLocker lock(&m_mutex);
	if (!validKey(key))
		return {};
	auto cached = m_entries.find(key);
	if (cached != m_entries.end())
	{
		if (cached->audio->cacheInputHash != editableKey(input))
			return {};
		cached->access = ++m_access;
		return bindCachedAudio(cached->audio, input, key);
	}
	QFile index(path(key, input));
	if (index.open(QIODevice::ReadOnly))
	{
		if (index.size() > MaximumMetadata)
			return {};
		QJsonParseError parse;
		const auto document = QJsonDocument::fromJson(index.readAll(), &parse);
		const auto info = document.object();
		const auto sha = info["audioSHA256"].toString();
		if (parse.error != QJsonParseError::NoError || !document.isObject() || info["key"].toString() != key
			|| info["editableHash"].toString() != editableKey(input) || !validKey(sha))
			return {};
		const auto audioPath = QDir(engineDirectory(input.document["pluginId"].toString())).filePath(sha + ".wav");
		QFile file(audioPath);
		if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > MaximumFile)
			return {};
		const auto bytes = file.read(MaximumFile);
		if (QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != sha)
		{
			file.close();
			QFile::remove(audioPath);
			index.close();
			QFile::remove(index.fileName());
			return {};
		}
		auto audio = std::make_shared<Audio>();
		if (!readWave(bytes, *audio) || audio->rate != info["rate"].toInt())
			return {};
		audio->startSeconds = info["startSeconds"].toDouble(NAN);
		audio->startTick = info["startTick"].toDouble(NAN);
		audio->feedback = info["feedback"].toObject();
		audio->cacheInputHash = info["editableHash"].toString();
		if (!std::isfinite(audio->startSeconds) || !std::isfinite(audio->startTick))
			return {};
		audio->waveform.build(audio->samples);
		remember(key, audio);
		file.close();
		if (file.open(QIODevice::ReadWrite))
			file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
		return bindCachedAudio(audio, input, key);
	}
	// Read the previous combined codec in place; never bulk-delete legacy data.
	QFile file(QDir(engineDirectory(input.document["pluginId"].toString())).filePath(key + ".svscache"));
	if (!file.open(QIODevice::ReadOnly))
	{
		if (m_legacyDirectory.isEmpty())
			return {};
		file.setFileName(QDir(m_legacyDirectory).filePath(key + ".svscache"));
		if (!file.open(QIODevice::ReadOnly))
			return {};
	}
	if (file.size() < 40 || file.size() > MaximumFile)
		return {};
	const auto hash = file.read(32), payload = file.read(MaximumFile);
	if (QCryptographicHash::hash(payload, QCryptographicHash::Sha256) != hash)
	{
		file.close();
		QFile::remove(file.fileName());
		return {};
	}
	QDataStream stream(payload);
	stream.setVersion(QDataStream::Qt_6_0);
	stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
	quint32 magic = 0, metadataSize = 0;
	stream >> magic >> metadataSize;
	if (magic != 0x53565331 || metadataSize > MaximumMetadata || qint64(metadataSize) + 16 > payload.size())
		return {};
	QByteArray metadata(int(metadataSize), Qt::Uninitialized);
	if (stream.readRawData(metadata.data(), metadata.size()) != metadata.size())
		return {};
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(metadata, &parse);
	auto info = document.object();
	quint64 samples = 0;
	stream >> samples;
	if (parse.error != QJsonParseError::NoError || !document.isObject() || info["key"].toString() != key
		|| samples > 32 * 1024 * 1024 || samples % 2 || stream.device()->bytesAvailable() != qint64(samples) * 4)
		return {};
	if (info["editableHash"].toString() != editableKey(input))
		return {};
	auto audio = std::make_shared<Audio>();
	audio->rate = info["rate"].toInt();
	audio->startSeconds = info["startSeconds"].toDouble();
	audio->startTick = info["startTick"].toDouble();
	audio->feedback = info["feedback"].toObject();
	audio->cacheInputHash = info["editableHash"].toString();
	if (audio->rate < 8000 || audio->rate > 192000 || !std::isfinite(audio->startSeconds)
		|| !std::isfinite(audio->startTick))
		return {};
	audio->samples.resize(size_t(samples));
	for (auto& value : audio->samples)
	{
		stream >> value;
		if (!std::isfinite(value))
			return {};
	}
	if (stream.status() != QDataStream::Ok)
		return {};
	audio->waveform.build(audio->samples);
	remember(key, audio);
	file.close();
	if (file.open(QIODevice::ReadWrite))
		file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
	return bindCachedAudio(audio, input, key);
}
void Cache::put(const QString& key, const Input& input, const std::shared_ptr<const Audio>& source)
{
	if (!source || !validKey(key))
		return;
	QMutexLocker lock(&m_mutex);
	auto audio = std::make_shared<Audio>(*source);
	audio->revision = 0;
	audio->feedback = feedbackIds(audio->feedback, noteIds(input, false)).toObject();
	audio->cacheInputHash = editableKey(input);
	remember(key, audio);
	const auto bytes = waveBytes(*audio);
	if (bytes.isEmpty())
		return;
	const auto sha = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
	const auto metadata
		= QJsonDocument(QJsonObject{{"key", key}, {"audioSHA256", sha}, {"editableHash", audio->cacheInputHash},
							{"rate", int(audio->rate)}, {"startSeconds", audio->startSeconds},
							{"startTick", audio->startTick}, {"feedback", audio->feedback}})
			  .toJson(QJsonDocument::Compact);
	if (metadata.size() > MaximumMetadata || bytes.size() + metadata.size() > std::min(m_diskLimit, MaximumFile))
		return;
	const auto directory = engineDirectory(input.document["pluginId"].toString());
	if (!QDir().mkpath(directory))
		return;
	const auto audioPath = QDir(directory).filePath(sha + ".wav");
	if (!QFileInfo::exists(audioPath))
	{
		QSaveFile file(audioPath);
		if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
			return;
	}
	QSaveFile index(path(key, input));
	if (index.open(QIODevice::WriteOnly))
	{
		if (index.write(metadata) != metadata.size())
			index.cancelWriting();
		else
			index.commit();
	}
	trimDisk();
}
void Cache::trimDisk()
{
	auto files = diskFiles(m_directory);
	std::sort(files.indices.begin(), files.indices.end(),
		[](const auto& a, const auto& b) { return a.lastModified() < b.lastModified(); });
	for (const auto& file : files.indices)
	{
		if (files.bytes <= m_diskLimit)
			break;
		const auto index = file.absoluteFilePath();
		if (!QFile::remove(index))
			continue;
		files.bytes -= file.size();
		const auto audio = files.audioByIndex.value(index);
		if (!audio.isEmpty() && --files.references[audio] == 0)
		{
			const auto size = QFileInfo(audio).size();
			if (QFile::remove(audio))
				files.bytes -= size;
		}
	}
}
qint64 Cache::memoryBytes() const
{
	QMutexLocker lock(&m_mutex);
	return m_memoryBytes;
}
qint64 Cache::diskBytes() const
{
	QMutexLocker lock(&m_mutex);
	return diskFiles(m_directory).bytes;
}
void Cache::clearMemory()
{
	QMutexLocker lock(&m_mutex);
	m_entries.clear();
	m_memoryBytes = 0;
}
}
