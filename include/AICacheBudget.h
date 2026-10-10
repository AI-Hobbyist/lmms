#ifndef LMMS_AI_CACHE_BUDGET_H
#define LMMS_AI_CACHE_BUDGET_H

#include <QString>

namespace lmms::aiCache {
enum class Scope
{
	All,
	SVC,
	SVS
};
qint64 bytes(const QString& workingDirectory, Scope scope);
qint64 limit();
void setLimit(qint64 bytes);
void setLegacyDirectory(const QString& workingDirectory, const QString& directory);
void trim(const QString& workingDirectory);
bool clear(const QString& workingDirectory, Scope scope);
void protectPath(const QString& path);
void releasePath(const QString& path);

// Protect files being published until the cache writer has finished.
class Use
{
public:
	Use(QString path, QString workingDirectory);
	~Use();
	Use(const Use&) = delete;
	Use& operator=(const Use&) = delete;

private:
	QString m_path;
	QString m_workingDirectory;
};
} // namespace lmms::aiCache

#endif
