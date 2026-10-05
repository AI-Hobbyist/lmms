#include <QtTest>
#include <QJsonDocument>
#include <QDomDocument>
#include <QFileInfo>
#include "Mixer.h"
#include <cmath>
#include "Engine.h"
#include "AudioEngine.h"
#include "AudioDummy.h"
#include "ConfigManager.h"
#include <QTemporaryDir>
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
};
QTEST_MAIN(SVSIntegrationTest)
#include "SVSIntegrationTest.moc"
