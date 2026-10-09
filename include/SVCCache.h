#pragma once

#include <QFile>
#include <QJsonObject>
#include <QString>
#include <memory>

#include "svc.h"

namespace lmms::svc {
class CachePair
{
public:
	// Source must be a seekable WAV stream. Snapshot must never contain credentials.
	static std::unique_ptr<CachePair> create(
		const QString& workingDirectory, const QString& engine, QIODevice& source, const QJsonObject& snapshot);
	~CachePair();
	CachePair(const CachePair&) = delete;
	CachePair& operator=(const CachePair&) = delete;
	const QString& hash() const { return m_hash; }
	QString inputPath() const { return m_input.fileName(); }
	QString outputPath() const;
	QString partialPath() const { return m_output.fileName(); }
	QJsonObject metadata() const { return m_metadata; }
	bool append(const svc_event& event);
	bool complete(svc_status validatedTerminal);
	void fail(const QString& state = "failed-partial");

private:
	CachePair() = default;
	bool saveMetadata();
	QByteArray waveHeader() const;
	QString m_hash;
	QFile m_input;
	QFile m_output;
	QJsonObject m_metadata;
	uint64_t m_samples = 0;
	uint64_t m_chunks = 0;
	uint32_t m_rate = 0;
	bool m_terminal = false;
};
} // namespace lmms::svc
