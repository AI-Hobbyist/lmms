#ifndef LMMS_SVS_PROJECT_OUTPUT_H
#define LMMS_SVS_PROJECT_OUTPUT_H
#include <QStringList>
#include <QVector>
#include <atomic>

namespace lmms::svs {
struct ProjectOutputFile
{
	QString source, target, relative;
};
struct ProjectOutputPlan
{
	QString directory, error;
	QVector<ProjectOutputFile> files;
	QStringList overwrites;
	bool valid() const { return error.isEmpty() && !files.isEmpty(); }
};
class ProjectOutput
{
public:
	static ProjectOutputPlan prepare(
		const QStringList& files, const QString& stagingRoot, const QString& destinationDirectory);
	static bool commit(const ProjectOutputPlan&, QString& error, const std::atomic<bool>* cancelled = nullptr);
};
}
#endif
