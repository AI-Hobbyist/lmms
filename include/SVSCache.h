#ifndef LMMS_SVS_CACHE_H
#define LMMS_SVS_CACHE_H
#include "SVSModel.h"
#include <QMutex>
#include <QMap>
namespace lmms::svs {
class Cache
{
public:
	static Cache& instance();
	Cache(QString directory, qint64 memoryLimit = 128 * 1024 * 1024, qint64 diskLimit = 512 * 1024 * 1024,
		QString legacyDirectory = {});
	QString engineDirectory(const QString& pluginId) const;
	static QString key(const Input&, const QString& pluginIdentity);
	// Content that can still be verified when a saved engine is unavailable.
	static QString editableKey(const Input&);
	std::shared_ptr<const Audio> get(const QString&, const Input&);
	void put(const QString&, const Input&, const std::shared_ptr<const Audio>&);
	qint64 memoryBytes() const;
	qint64 diskBytes() const;
	void clearMemory();

private:
	struct Entry
	{
		std::shared_ptr<const Audio> audio;
		qint64 cost = 0;
		quint64 access = 0;
	};
	void remember(const QString&, std::shared_ptr<const Audio>);
	void trimDisk();
	QString path(const QString&, const Input&) const;
	QString m_directory;
	QString m_legacyDirectory;
	qint64 m_memoryLimit, m_diskLimit, m_memoryBytes = 0;
	quint64 m_access = 0;
	mutable QMutex m_mutex;
	QMap<QString, Entry> m_entries;
};
}
#endif
