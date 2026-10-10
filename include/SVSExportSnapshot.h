#ifndef LMMS_SVS_EXPORT_SNAPSHOT_H
#define LMMS_SVS_EXPORT_SNAPSHOT_H
#include "SVSModel.h"
#include <QObject>
#include <QPointer>
namespace lmms {
class Song;
class SVSTrack;
namespace svs {
// Owner-thread capture and completion. Workers receive only value snapshots.
class ExportSnapshot : public QObject
{
public:
	struct Region
	{
		QPointer<SVSTrack> track;
		QString trackName, clipName, diagnostic, mixContext;
		Input input;
		double position = 0, length = 0, contentOffset = 0;
		std::shared_ptr<Plugin> plugin;
		std::shared_ptr<const Audio> audio;
		std::shared_ptr<const VolumeAutomation> volume;
		bool declarationPending = false;
		bool catalogPending = false;
		QString voicePackage;
	};
	enum class State
	{
		Captured,
		Preparing,
		Ready,
		Failed,
		Cancelled
	};
	static QVector<Region> capture(Song&, uint32_t sampleRate);
	explicit ExportSnapshot(QVector<Region>, QObject* parent = nullptr);
	~ExportSnapshot() override;
	void prepare(bool ignoreFailedRegions = false);
	void cancel();
	bool activate(Song&);
	void freezeTimeline(Song&);
	void deactivate();
	State state() const { return m_state; }
	const QVector<Region>& regions() const { return m_regions; }
	QStringList diagnostics() const { return m_diagnostics; }
	std::function<void()> completed;
	std::function<void(const QString&)> invalidated;

private:
	void receive(int, std::shared_ptr<const Audio>, const QString&);
	void submit(int, Input);
	void declare(int);
	void awaitCatalog(int);
	void finish(State);
	QString locate(int, const QString&) const;
	void invalidateActive(const QString&);
	QVector<Region> m_regions;
	QVector<std::shared_ptr<RenderControl>> m_controls;
	State m_state = State::Captured;
	int m_remaining = 0;
	bool m_ignore = false;
	bool m_completionScheduled = false;
	QStringList m_diagnostics;
	QVector<QPointer<SVSTrack>> m_activeTracks;
};
}
}
#endif
