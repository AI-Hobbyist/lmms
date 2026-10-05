#ifndef LMMS_SVS_MODEL_H
#define LMMS_SVS_MODEL_H
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QStringList>
#include <memory>
#include <vector>
#include "svs.h"
namespace lmms::svs {
struct Note {
 QString id, lyric="la", language, pronunciation;
 double tick=0, duration=48, pitch=60;
 QJsonObject parameters;
 QJsonObject phonemes;
};
struct Voice { QString pluginId,id,name,version,language,defaultLyric,avatar,portrait,package; QJsonObject metadata; };
struct Audio { std::vector<float> samples; uint32_t rate=48000; uint64_t revision=0; QJsonObject feedback; };
struct Input { QString clipId,voiceId; uint64_t generation=0,revision=0,request=0; QVector<Note> notes; double secondsPerTick=0,duration=0; uint32_t rate=48000; QJsonObject document; };
class Plugin {
public:
 explicit Plugin(const QString& library);
 ~Plugin();
 Plugin(const Plugin&)=delete;
 bool valid() const;
 QString error() const;
 QVector<Voice> voices(const QString& package,const QString& id);
 std::shared_ptr<const Audio> render(const Input&,QString& error);
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
