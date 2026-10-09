#ifndef LMMS_SVS_TEMPO_SOURCE_H
#define LMMS_SVS_TEMPO_SOURCE_H
#include "SVSTempoSnapshot.h"
#include <QObject>
#include <QVector>
#include <atomic>
namespace lmms::svs {
class TempoSource : public QObject
{
	Q_OBJECT
public:
	static TempoSource& forSong(Song&);
	std::shared_ptr<const TempoSnapshot> snapshot();
signals:
	void changed();

private:
	explicit TempoSource(Song&);
	void schedule();
	void refresh();
	Song& m_song;
	int m_baseTempo;
	bool m_pending = false;
	std::atomic<int> m_observedTempo;
	std::shared_ptr<const TempoSnapshot> m_snapshot;
	QJsonObject m_signature;
	QVector<QMetaObject::Connection> m_watches;
};
}
#endif
