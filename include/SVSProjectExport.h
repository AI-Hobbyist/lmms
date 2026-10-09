#ifndef LMMS_SVS_PROJECT_EXPORT_H
#define LMMS_SVS_PROJECT_EXPORT_H

#include "SVSCurve.h"
#include "SVSModel.h"
#include "SVSTempoSnapshot.h"
#include <QStringList>
#include <atomic>
#include <memory>

namespace lmms {
class Song;
class SampleBuffer;
namespace svs {
// Owner-thread capture. Worker operations retain values and immutable buffers only.
struct ProjectExportClip
{
	QString name;
	double position = 0, contentOffset = 0, length = 0;
	bool muted = false;
	QVector<Note> notes;
	Curves curves;
};
struct ProjectExportAudio
{
	QString name, sourcePath;
	double position = 0, startOffset = 0, length = 0;
	bool muted = false, reversed = false;
	float amplification = 1;
	std::shared_ptr<const SampleBuffer> buffer;
};
struct ProjectExportTrack
{
	QString name;
	bool singing = false, muted = false, solo = false;
	double volume = 1, pan = 0;
	QVector<ProjectExportClip> clips;
	QVector<ProjectExportAudio> audio;
};
struct ProjectExportSnapshot
{
	QString error;
	QStringList losses;
	QVector<ProjectExportTrack> tracks;
	int numerator = 4, denominator = 4, lastTick = 0;
	std::shared_ptr<const TempoSnapshot> tempo;
	bool valid() const { return error.isEmpty() && !tracks.isEmpty() && tempo; }
};
struct ProjectExportAudioFile
{
	int trackIndex = 0;
	ProjectExportAudio audio;
	qint64 firstFrame = 0, frameCount = 0;
	QString fileName;
};
struct ProjectExportData
{
	QJsonObject project;
	QString error;
	QStringList losses;
	QVector<ProjectExportAudioFile> audioFiles;
	bool valid() const { return error.isEmpty() && !project.isEmpty(); }
};
class ProjectExport
{
public:
	static ProjectExportSnapshot capture(Song& song);
	static ProjectExportData build(const ProjectExportSnapshot&, const std::atomic<bool>* cancelled = nullptr);
	static bool writeAudioFiles(const ProjectExportData&, const QString& directory, QString& error,
		const std::atomic<bool>* cancelled = nullptr);
};
}
}
#endif
