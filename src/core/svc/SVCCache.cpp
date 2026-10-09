#include "SVCCache.h"

#include <QCoreApplication>

#ifdef _WIN32
#include <windows.h>

#include <bcrypt.h>
#else
#include <QRandomGenerator>
#endif

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtEndian>
#include <limits>
#include <stdexcept>

namespace lmms::svc {
namespace {
bool containsCredential(const QJsonValue& value)
{
	if (value.isObject())
	{
		const auto object = value.toObject();
		for (auto it = object.begin(); it != object.end(); ++it)
		{
			const auto key = it.key().toLower();
			if (key.contains("token") || key.contains("authorization") || key.contains("password")
				|| containsCredential(it.value()))
			{
				return true;
			}
		}
	}
	else if (value.isArray())
	{
		for (const auto& child : value.toArray())
		{
			if (containsCredential(child)) { return true; }
		}
	}
	return false;
}

QByteArray nonce()
{
	QByteArray bytes(16, 0);
#ifdef _WIN32
	if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.data()), bytes.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG)
		< 0)
	{
		throw std::runtime_error(
			QCoreApplication::translate("NativeRVC", "Operating system random generator failed").toStdString());
	}
#else
	for (int index = 0; index < bytes.size(); index += 4)
	{
		qToLittleEndian(QRandomGenerator::system()->generate(), bytes.data() + index);
	}
#endif
	return bytes;
}
} // namespace

std::unique_ptr<CachePair> CachePair::create(
	const QString& workingDirectory, const QString& engine, QIODevice& source, const QJsonObject& snapshot)
{
	if (!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$").match(engine).hasMatch()
		|| engine.compare("CON", Qt::CaseInsensitive) == 0 || engine.compare("NUL", Qt::CaseInsensitive) == 0
		|| engine.compare("PRN", Qt::CaseInsensitive) == 0 || engine.compare("AUX", Qt::CaseInsensitive) == 0
		|| QRegularExpression("^(COM|LPT)[1-9]$", QRegularExpression::CaseInsensitiveOption).match(engine).hasMatch()
		|| source.isSequential() || !source.isReadable() || containsCredential(snapshot))
	{
		throw std::invalid_argument(
			QCoreApplication::translate("NativeRVC", "Invalid cache engine, source or credential-bearing snapshot")
				.toStdString());
	}
	const auto initial = source.pos();
	QCryptographicHash digest(QCryptographicHash::Sha256);
	while (!source.atEnd())
	{
		const auto bytes = source.read(SVC_MAX_FEED);
		if (bytes.isEmpty() && !source.atEnd())
		{
			throw std::runtime_error(
				QCoreApplication::translate("NativeRVC", "Input digest read failed").toStdString());
		}
		digest.addData(bytes);
	}
	if (!source.seek(initial))
	{
		throw std::runtime_error(QCoreApplication::translate("NativeRVC", "Input cannot rewind").toStdString());
	}
	const auto root = QDir(workingDirectory).filePath("cache/svc/" + engine);
	if (!QDir().mkpath(root + "/input") || !QDir().mkpath(root + "/output"))
	{
		throw std::runtime_error(
			QCoreApplication::translate("NativeRVC", "Cannot create SVC cache directories").toStdString());
	}
	for (int attempt = 0; attempt < 16; ++attempt)
	{
		auto pair = std::unique_ptr<CachePair>(new CachePair);
		pair->m_metadata = snapshot;
		pair->m_metadata.insert("format_version", 1);
		pair->m_metadata.insert("engine_id", engine);
		pair->m_metadata.insert("nonce", QString::fromLatin1(nonce().toHex()));
		pair->m_metadata.insert("input_digest", QString::fromLatin1(digest.result().toHex()));
		pair->m_hash = QString::fromLatin1(QCryptographicHash::hash(
			QJsonDocument(pair->m_metadata).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
				.toHex());
		pair->m_input.setFileName(root + "/input/" + pair->m_hash + ".wav");
		pair->m_output.setFileName(root + "/output/" + pair->m_hash + ".wav.partial");
		// NewOnly never truncates another task's files, even on a hash collision.
		if (!pair->m_input.open(QIODevice::WriteOnly | QIODevice::NewOnly))
		{
			pair->m_terminal = true;
			if (QFile::exists(pair->inputPath())) { continue; }
			throw std::runtime_error(
				QCoreApplication::translate("NativeRVC", "Cannot reserve SVC input cache").toStdString());
		}
		if (QFile::exists(pair->outputPath()) || !pair->m_output.open(QIODevice::ReadWrite | QIODevice::NewOnly))
		{
			pair->m_terminal = true;
			pair->m_input.remove();
			if (QFile::exists(pair->outputPath()) || QFile::exists(pair->partialPath())) { continue; }
			throw std::runtime_error(
				QCoreApplication::translate("NativeRVC", "Cannot reserve SVC output cache").toStdString());
		}
		while (!source.atEnd())
		{
			const auto bytes = source.read(SVC_MAX_FEED);
			if ((bytes.isEmpty() && !source.atEnd()) || pair->m_input.write(bytes) != bytes.size())
			{
				pair->m_terminal = true;
				pair->m_input.remove();
				pair->m_output.remove();
				throw std::runtime_error(
					QCoreApplication::translate("NativeRVC", "Input cache write failed").toStdString());
			}
		}
		pair->m_input.close();
		pair->m_metadata.insert("state", "writing");
		pair->m_metadata.insert("chunks", QJsonArray());
		if (!pair->saveMetadata())
		{
			throw std::runtime_error(
				QCoreApplication::translate("NativeRVC", "Cannot save SVC cache manifest").toStdString());
		}
		return pair;
	}
	throw std::runtime_error(
		QCoreApplication::translate("NativeRVC", "Unable to allocate unique SVC cache identity").toStdString());
}

CachePair::~CachePair()
{
	if (!m_terminal) { fail("cancelled-partial"); }
}

QString CachePair::outputPath() const
{
	auto path = m_output.fileName();
	if (path.endsWith(".partial")) { path.chop(8); }
	return path;
}

QByteArray CachePair::waveHeader() const
{
	QByteArray header(44, 0);
	header.replace(0, 4, "RIFF");
	qToLittleEndian<uint32_t>(36 + static_cast<uint32_t>(m_samples * 2), header.data() + 4);
	header.replace(8, 8, "WAVEfmt ");
	qToLittleEndian<uint32_t>(16, header.data() + 16);
	qToLittleEndian<uint16_t>(1, header.data() + 20);
	qToLittleEndian<uint16_t>(1, header.data() + 22);
	qToLittleEndian<uint32_t>(m_rate, header.data() + 24);
	qToLittleEndian<uint32_t>(m_rate * 2, header.data() + 28);
	qToLittleEndian<uint16_t>(2, header.data() + 32);
	qToLittleEndian<uint16_t>(16, header.data() + 34);
	header.replace(36, 4, "data");
	qToLittleEndian<uint32_t>(static_cast<uint32_t>(m_samples * 2), header.data() + 40);
	return header;
}

bool CachePair::saveMetadata()
{
	QSaveFile file(outputPath() + ".json");
	const auto bytes = QJsonDocument(m_metadata).toJson(QJsonDocument::Compact);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

bool CachePair::append(const svc_event& event)
{
	if (m_terminal || event.type != SVC_AUDIO || event.channels != 1 || !event.bytes || !event.sample_count
		|| event.sample_count > SVC_MAX_PART / 2 || event.byte_count != event.sample_count * 2
		|| event.sample_offset != m_samples || event.chunk_index != m_chunks + 1 || event.sample_rate < 8000
		|| event.sample_rate > 384000 || (m_rate && event.sample_rate != m_rate)
		|| event.sample_count + m_samples > (std::numeric_limits<uint32_t>::max() - 36) / 2)
	{
		return false;
	}
	m_rate = event.sample_rate;
	if (!m_samples && m_output.write(waveHeader()) != 44)
	{
		fail();
		return false;
	}
	if (m_output.write(reinterpret_cast<const char*>(event.bytes), event.byte_count)
			!= static_cast<qint64>(event.byte_count)
		|| !m_output.flush())
	{
		fail();
		return false;
	}
	m_samples += event.sample_count;
	++m_chunks;
	auto chunks = m_metadata.value("chunks").toArray();
	chunks.append(QJsonObject{{"index", QString::number(event.chunk_index)},
		{"offset", QString::number(event.sample_offset)}, {"samples", QString::number(event.sample_count)}});
	m_metadata.insert("chunks", chunks);
	m_metadata.insert("sample_rate", static_cast<int>(m_rate));
	m_metadata.insert("samples", QString::number(m_samples));
	if (!saveMetadata())
	{
		fail();
		return false;
	}
	return true;
}

bool CachePair::complete(svc_status validatedTerminal)
{
	if (m_terminal || validatedTerminal != SVC_COMPLETE || !m_samples) { return false; }
	if (!m_output.seek(0) || m_output.write(waveHeader()) != 44 || !m_output.flush())
	{
		fail();
		return false;
	}
	QCryptographicHash outputDigest(QCryptographicHash::Sha256);
	if (!m_output.seek(0) || !outputDigest.addData(&m_output))
	{
		fail();
		return false;
	}
	m_metadata.insert("output_digest", QString::fromLatin1(outputDigest.result().toHex()));
	m_output.close();
	const auto final = outputPath();
	if (!QFile::rename(partialPath(), final))
	{
		fail();
		return false;
	}
	m_metadata.insert("state", "complete");
	if (!saveMetadata())
	{
		QFile::rename(final, partialPath());
		fail();
		return false;
	}
	m_terminal = true;
	return true;
}

void CachePair::fail(const QString& state)
{
	if (!m_terminal)
	{
		m_output.close();
		m_metadata.insert("state", state == "cancelled-partial" ? state : "failed-partial");
		saveMetadata();
		m_terminal = true;
	}
}
} // namespace lmms::svc
