#ifndef LMMS_SVS_MODEL_H
#define LMMS_SVS_MODEL_H
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QStringList>
#include <memory>
#include <vector>
#include <atomic>
#include <functional>
#include <mutex>
#include "svs.h"
#include "SVSWaveform.h"
#include "SVSTimeMapping.h"
namespace lmms::svs {
struct TempoSnapshot;
struct Note {
 QString id, lyric="la", language, pronunciation;
 double tick=0, duration=48, pitch=60;
 QJsonObject parameters;
 QJsonObject phonemes;
 QJsonObject xmlExtras;
 bool operator==(const Note&) const = default;
};
struct Voice { QString pluginId,id,name,version,language,defaultLyric,avatar,portrait,package; QJsonObject metadata; };
struct Audio { std::vector<float> samples; Waveform waveform; uint32_t rate=48000; uint64_t revision=0; double startSeconds=0,startTick=0; TimeMapping mapping; QString cacheKey,cacheInputHash; QJsonObject feedback; };
struct ExportAudioRegion {double position=0,end=0,contentOffset=0;std::shared_ptr<const Audio> audio;};
struct Input { QString clipId,voiceId; uint64_t generation=0,revision=0,request=0; QVector<Note> notes; double secondsPerTick=0,duration=0; uint32_t rate=48000; QJsonObject document;std::shared_ptr<const TempoSnapshot> tempoSnapshot; };
// The session cancel callback is installed only while its plugin session exists.
// Cancellation never needs the engine/declaration mutex held by render().
class RenderControl {
public:
 void cancel() { cancelled=true; std::lock_guard lock(m_mutex); if(m_cancel) m_cancel(); }
 void attach(std::function<void()> callback) { std::lock_guard lock(m_mutex); m_cancel=std::move(callback); if(cancelled&&m_cancel) m_cancel(); }
 void detach() { std::lock_guard lock(m_mutex); m_cancel={}; }
 std::atomic<bool> cancelled{false};
private:
 std::mutex m_mutex;
 std::function<void()> m_cancel;
};
class Plugin {
public:
 explicit Plugin(const QString& library);
 ~Plugin();
 Plugin(const Plugin&)=delete;
 bool valid() const;
 QString error() const;
 QString identity() const;
 QVector<Voice> voices(const QString& package,const QString& id);
 QJsonObject capabilities(const QString& voice, const QJsonObject& context, QString& error);
 QJsonObject pronunciation(const QString& voice, const QJsonObject& request, QString& error);
 std::shared_ptr<const Audio> render(const Input&,QString& error,const std::shared_ptr<RenderControl>& control={});
private:
 struct Impl; std::unique_ptr<Impl> m_impl;
};
class Registry {
public:
 static Registry& instance();
 const QVector<Voice>& voices();
 std::shared_ptr<Plugin> plugin(const QString& id);
 QStringList diagnostics() const { return m_diagnostics; }
private:
 bool m_scanned=false;
 QVector<Voice> m_voices;
 QMap<QString,std::shared_ptr<Plugin>> m_plugins;
 QStringList m_diagnostics;
};
}
#endif
