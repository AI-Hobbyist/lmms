#include <QtTest>
#include <QJsonDocument>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include "SVSParameterPanel.h"
#include <QDomDocument>
#include <QFileInfo>
#include "Mixer.h"
#include <cmath>
#include "Engine.h"
#include "AudioEngine.h"
#include "AudioDummy.h"
#include "ConfigManager.h"
#include <QTemporaryDir>
#include <QJsonDocument>
#include "Song.h"
#include "SVSTrack.h"
#include "SVSClip.h"
#include "SVSViews.h"
#include "PatternStore.h"
#include "SampleFrame.h"
using namespace lmms;
class SVSIntegrationTest : public QObject {
 Q_OBJECT
 QTemporaryDir m_configuration;
private slots:
 void initTestCase() { QVERIFY(m_configuration.isValid()); ConfigManager::inst()->loadConfigFile(m_configuration.filePath("svs-test-config.xml")); Engine::init(true); bool available=false; Engine::audioEngine()->setAudioDevice(new AudioDummy(available,Engine::audioEngine()),false); }
 void cleanupTestCase() { Engine::destroy(); }
 void parameterPanelStateAndFocus() {
  const auto& voice=svs::Registry::instance().voices()[0]; auto plugin=svs::Registry::instance().plugin(voice.pluginId);
  QString error; svs::Capabilities cap; QVERIFY(svs::Capabilities::parse(plugin->capabilities("full",{},error),cap,error));
  gui::SVSParameterPanel panel; panel.resize(300,300); QString changed; QJsonValue committed;
  auto setter=[&](const QString& id,const QJsonValue& value){changed=id; committed=value;};
  panel.refresh(cap.parameters,"note",{QJsonObject{},QJsonObject{}},{},setter);
  auto* label=panel.findChild<QLineEdit*>("svsParameter.note.example.label"); QVERIFY(label); QCOMPARE(label->placeholderText(),QString("Unset"));
  auto* soft=panel.findChild<QCheckBox*>("svsParameter.note.example.soft"); QVERIFY(soft); QCOMPARE(soft->checkState(),Qt::PartiallyChecked);
  panel.refresh(cap.parameters,"note",{QJsonObject{{"example.label","one"}},QJsonObject{{"example.label","two"}}},{},setter); QCOMPARE(label->placeholderText(),QString("Mixed"));
  panel.show(); panel.activateWindow(); label->setFocus(); QTRY_VERIFY(label->hasFocus()); QTest::keyClicks(label,"pending");
  panel.refresh(cap.parameters,"note",{QJsonObject{{"example.label","old"}}},{},setter); QCOMPARE(panel.findChild<QLineEdit*>("svsParameter.note.example.label"),label); QCOMPARE(label->text(),QString("pending"));
  QTest::keyClick(label,Qt::Key_Return); QCOMPARE(changed,QString("example.label")); QCOMPARE(committed.toString(),QString("pending"));
  gui::SVSParameterPanel readOnly; readOnly.refresh(cap.feedbackParameters,"clip",{QJsonObject{{"example.energy",42}}},{},setter); auto* energy=readOnly.findChild<QWidget*>("svsParameter.clip.example.energy"); QVERIFY(energy); QVERIFY(!energy->isEnabled());
 }
 void capabilitiesAndDictionaries() {
  const auto& voices=svs::Registry::instance().voices(); QVERIFY(!voices.isEmpty());
  auto plugin=svs::Registry::instance().plugin(voices[0].pluginId); QVERIFY(plugin);
  QString error; svs::Capabilities full, minimal;
  QVERIFY2(svs::Capabilities::parse(plugin->capabilities("full",{},error),full,error),qPrintable(error));
  QVERIFY2(svs::Capabilities::parse(plugin->capabilities("minimal",{},error),minimal,error),qPrintable(error));
  QCOMPARE(full.languages.size(),3); QCOMPARE(minimal.languages.size(),1); QVERIFY(full.phonemeTiming); QVERIFY(!minimal.phonemeTiming);
  auto gain=full.parameter("example.gain","track"); QVERIFY(gain); QVERIFY(gain->accepts(1)); QVERIFY(!gain->accepts(0));
  auto mode=full.parameter("example.mode","track"); QVERIFY(mode); QVERIFY(mode->accepts("advanced")); QVERIFY(!mode->accepts(1));
  auto breath=full.parameter("example.breath","clip"); QVERIFY(breath); QVERIFY(!breath->isVisible({})); QVERIFY(breath->isVisible({{"example.mode","advanced"}}));
  QVERIFY(!full.feedbackParameters.isEmpty()); QVERIFY(!full.feedbackParameters[0].writable);
  auto schema=full.original; auto parameters=schema["parameters"].toArray(); parameters.append(parameters[0]); schema["parameters"]=parameters; svs::Capabilities invalid; QVERIFY(!svs::Capabilities::parse(schema,invalid,error));
  auto resources=full.original; auto resourceParameters=resources["parameters"].toArray(); resourceParameters.append(QJsonObject{{"id","test.resource"},{"name","Resource"},{"scope","clip"},{"type","string"},{"default","first"},{"resourceIds",QJsonArray{"first","second"}}}); resources["parameters"]=resourceParameters; svs::Capabilities constrained; QVERIFY(svs::Capabilities::parse(resources,constrained,error)); auto resource=constrained.parameter("test.resource","clip"); QVERIFY(resource); QVERIFY(resource->accepts("first")); QVERIFY(!resource->accepts("../external"));
  gui::SVSParameterPanel resourcePanel; resourcePanel.refresh(constrained.parameters,"clip",{QJsonObject{}},{},{}); auto* selector=resourcePanel.findChild<QComboBox*>("svsParameter.clip.test.resource"); QVERIFY(selector); QCOMPARE(selector->itemData(1).toString(),QString("second"));
  QCOMPARE(svs::selectedValue({QJsonObject{},QJsonObject{}} ,"x").state,svs::ValueState::Unset);
  QCOMPARE(svs::selectedValue({QJsonObject{{"x",1}},QJsonObject{{"x",1}}},"x").state,svs::ValueState::Common);
  QCOMPARE(svs::selectedValue({QJsonObject{{"x",1}},QJsonObject{}},"x").state,svs::ValueState::Mixed);
  QFile dictionaryFile(voices[0].package+"/zh.json"); QVERIFY(dictionaryFile.open(QIODevice::ReadOnly)); auto bytes=dictionaryFile.readAll(); svs::Dictionary dictionary;
  QVERIFY2(svs::Dictionary::parse(bytes,full.phonemeSet,dictionary,error),qPrintable(error));
  svs::Note note; note.id="reading"; note.lyric=QString::fromUtf8("重"); note.language="zh";
  auto result=svs::resolvePronunciation(note,full,{dictionary},{},"en"); QVERIFY(result.generated); QCOMPARE(result.candidates.size(),2); QCOMPARE(result.source,QString("voiceDictionary"));
  note.pronunciation="chong"; result=svs::resolvePronunciation(note,full,{dictionary},{},"en"); QCOMPARE(result.text,QString("chong")); QCOMPARE(result.source,QString("manualPronunciation"));
  note.phonemes={{"symbols",QJsonArray{"l","a"}}}; result=svs::resolvePronunciation(note,full,{dictionary},{},"en"); QCOMPARE(result.source,QString("manualPhonemes"));
  auto projectDictionary=dictionary; projectDictionary.id="project.zh"; projectDictionary.entries[note.lyric]=QJsonArray{QJsonObject{{"reading","project"},{"phonemes",QJsonArray{"h","ao"}}}}; note.phonemes={}; note.pronunciation.clear(); result=svs::resolvePronunciation(note,full,{dictionary},{projectDictionary},"en"); QCOMPARE(result.source,QString("projectDictionary")); QCOMPARE(result.text,QString("project"));
  QFile japaneseFile(voices[0].package+"/ja.json"); QVERIFY(japaneseFile.open(QIODevice::ReadOnly)); svs::Dictionary japanese; QVERIFY(svs::Dictionary::parse(japaneseFile.readAll(),full.phonemeSet,japanese,error)); note.language="ja"; note.lyric=QString::fromUtf8("ら"); result=svs::resolvePronunciation(note,full,{japanese},{},"en"); QVERIFY(result.generated); QCOMPARE(result.text,QString("ra")); note.language="zh";
  auto corrupt=QJsonDocument::fromJson(bytes).object(); auto entries=corrupt["entries"].toArray(); entries.append(entries[0]); corrupt["entries"]=entries; QVERIFY(!svs::Dictionary::parse(QJsonDocument(corrupt).toJson(),full.phonemeSet,dictionary,error)); QVERIFY(error.contains("entries["));
  note.phonemes={}; note.pronunciation.clear(); note.lyric="unknown"; result=svs::resolvePronunciation(note,full,{},{},"en"); QVERIFY(!result.generated); QCOMPARE(result.text,note.lyric); QVERIFY(!result.diagnostic.isEmpty());
  note.lyric="-"; result=svs::resolvePronunciation(note,full,{},{},"en"); QVERIFY(!result.continuation); QVERIFY(!result.diagnostic.isEmpty());
  auto fallback=plugin->pronunciation("full",{{"lyric","l a"}},error); QVERIFY(fallback["generated"].toBool()); QCOMPARE(fallback["phonemes"].toArray().size(),2);
 }
 void verticalSlice() {
  QCOMPARE(int(Track::Type::Instrument),0); QCOMPARE(int(Track::Type::HiddenAutomation),6); QCOMPARE(int(Track::Type::SVS),7);
  QVERIFY(!Track::create(Track::Type::SVS,Engine::patternStore()));
  const auto& voices=svs::Registry::instance().voices(); QVERIFY2(voices.size()>=2,qPrintable(svs::Registry::instance().diagnostics().join('\n')));
  auto* track=static_cast<SVSTrack*>(Track::create(Track::Type::SVS,Engine::getSong())); track->bindVoice(voices[0].pluginId,voices[0].id); QCOMPARE(track->name(),voices[0].name); QVERIFY(QFileInfo::exists(track->voice().avatar));
  auto* clip=static_cast<SVSClip*>(track->createClip(192)); { gui::SVSPianoRoll editor(clip,nullptr); auto* canvas=editor.findChild<QWidget*>("svsNoteCanvas"); QVERIFY(canvas); QTest::mouseDClick(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(108,270)); QCOMPARE(clip->notes().size(),1); QCOMPARE(clip->notes()[0].tick,24.); QCOMPARE(clip->notes()[0].pitch,64.); QCOMPARE(clip->notes()[0].lyric,voices[0].defaultLyric); } svs::Note note; note.id="note-fixture"; note.tick=24; note.duration=96; note.pitch=64; note.lyric=QString::fromUtf8("测试"); note.parameters={{"unknown.parameter",42}}; clip->setNotes({note});
  QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000); auto audio=clip->audio(); QVERIFY(!audio->samples.empty()); double energy=0; for(float sample:audio->samples) { QVERIFY(std::isfinite(sample)); energy+=sample*sample; } QVERIFY(energy>1);
  track->setName("Custom SVS"); track->bindVoice(voices[1].pluginId,voices[1].id); QCOMPARE(track->name(),QString("Custom SVS"));
  track->bindVoice(voices[0].pluginId,voices[0].id); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000);
  QDomDocument doc; auto parent=doc.createElement("test"); doc.appendChild(parent); track->saveState(doc,parent); auto* restored=static_cast<SVSTrack*>(Track::create(parent.firstChildElement(),Engine::getSong()));
  QCOMPARE(restored->name(),QString("Custom SVS")); QCOMPARE(restored->voiceId(),track->voiceId()); QCOMPARE(restored->numOfClips(),1); auto* saved=static_cast<SVSClip*>(restored->getClip(0)); QCOMPARE(saved->startPosition(),TimePos(192)); QCOMPARE(saved->notes().size(),1); QCOMPARE(saved->notes()[0].lyric,note.lyric); QCOMPARE(saved->notes()[0].parameters,note.parameters);
  auto* copy=static_cast<SVSClip*>(clip->clone()); QVERIFY(copy->id()!=clip->id()); QVERIFY(copy->notes()[0].id!=clip->notes()[0].id); copy->movePosition(960); QCOMPARE(clip->startPosition(),TimePos(192)); QCOMPARE(copy->notes()[0].tick,note.tick);
  delete copy; delete restored;
  auto* mixer = Engine::mixer(); mixer->createChannel(); mixer->createChannel(); track->mixerChannelModel()->setRange(0,2,1); track->mixerChannelModel()->setValue(2); QVERIFY(mixer->isChannelInUse(2)); mixer->moveChannelLeft(2); QCOMPARE(track->mixerChannelModel()->value(),1); mixer->deleteChannel(1); QCOMPARE(track->mixerChannelModel()->value(),0);
  Engine::getSong()->getTimeline(Song::PlayMode::Song).setTicks(192); Engine::getSong()->playSong(); double mixedEnergy=0;
  for(int period=0;period<220;++period) { auto buffer=Engine::audioEngine()->renderNextPeriod(); for(const auto& sample:buffer) mixedEnergy+=sample[0]*sample[0]+sample[1]*sample[1]; }
  Engine::getSong()->stop(); QVERIFY2(mixedEnergy>0.01,"SVS PCM did not reach LMMS mixed output"); delete track;
 }
 void renderedParametersAndPersistence() {
  const auto& voice=svs::Registry::instance().voices()[0];
  auto* track=static_cast<SVSTrack*>(Track::create(Track::Type::SVS,Engine::getSong())); track->bindVoice(voice.pluginId,"full");
  QVERIFY2(track->capabilityDiagnostics().isEmpty(),qPrintable(track->capabilityDiagnostics().join('\n')));
  auto* clip=static_cast<SVSClip*>(track->createClip(0)); svs::Note note; note.id="parameter-note"; note.lyric="la"; clip->setNotes({note});
  QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000); auto first=clip->audio(); double firstEnergy=0; for(float value:first->samples) firstEnergy+=value*value;
  QVERIFY(track->setParameter("example.gain",.5)); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000); auto quiet=clip->audio(); double quietEnergy=0; for(float value:quiet->samples) quietEnergy+=value*value;
  QVERIFY(std::abs(quietEnergy/firstEnergy-.25)<.01); QVERIFY(!track->setParameter("example.gain",9)); QVERIFY(!clip->setParameter("example.energy",1));
  QVERIFY(clip->setParameter("example.breath",.7)); QVERIFY(track->setParameter("example.mode","basic")); QCOMPARE(clip->parameters()["example.breath"].toDouble(),.7);
  QVERIFY(clip->setNoteParameter({note.id},"example.phonemeGain",.5,true)); QVERIFY(!clip->setNoteParameter({note.id},"example.phonemeGain",3,true));
  track->bindVoice(voice.pluginId,"minimal"); QCOMPARE(clip->parameters()["example.breath"].toDouble(),.7); QCOMPARE(track->parameters()["example.gain"].toDouble(),.5); QVERIFY(!track->capabilities().parameter("example.breath","clip")); QVERIFY(!clip->setParameter("example.breath",.9));
  track->bindVoice(voice.pluginId,"full"); QCOMPARE(clip->parameters()["example.breath"].toDouble(),.7);
  QDomDocument doc; auto root=doc.createElement("test"); doc.appendChild(root); track->saveState(doc,root); auto* restored=static_cast<SVSTrack*>(Track::create(root.firstChildElement(),Engine::getSong()));
  QCOMPARE(restored->parameters(),track->parameters()); QCOMPARE(static_cast<SVSClip*>(restored->getClip(0))->parameters(),clip->parameters());
  QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000); auto reading=clip->audio()->feedback["pronunciations"].toObject()[note.id].toObject(); QVERIFY(reading["generated"].toBool()); QCOMPARE(reading["source"].toString(),QString("voiceDictionary"));
  delete restored; delete track;
 }
};
QTEST_MAIN(SVSIntegrationTest)
#include "SVSIntegrationTest.moc"
