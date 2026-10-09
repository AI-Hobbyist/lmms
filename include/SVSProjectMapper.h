#ifndef LMMS_SVS_PROJECT_MAPPER_H
#define LMMS_SVS_PROJECT_MAPPER_H

#include <QDomDocument>
#include <QJsonObject>
#include <QMap>
#include <QStringList>

namespace lmms::svs {
struct ProjectVoice
{
	QString pluginId, voiceId, name, language;
};
// Decoding and durable resource preparation happen before the Song is touched.
struct ProjectAudio
{
	QString path;
	double lengthTicks = 0;
	double durationSeconds = -1;
};
struct ProjectImport
{
	QDomDocument document;
	QString error;
	QStringList losses;
	bool valid() const { return error.isEmpty() && !document.isNull(); }
};
class ProjectMapper
{
public:
	static ProjectImport prepareImport(
		const QJsonObject& project, const ProjectVoice& voice, const QMap<QString, ProjectAudio>& audio);
	static double audioLengthTicks(const QJsonObject& project, double position, double seconds);
};
}
#endif
