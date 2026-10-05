#include <QtTest>
#include <QJsonDocument>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QLayout>
#include <QSpinBox>
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
#include "SVSCanvas.h"
#include "SVSCurve.h"
#include "SVSResultStrip.h"
#include "SVSLyricEditor.h"
#include "SVSImageLoader.h"
#include "SVSNoteOperations.h"
#include <QSlider>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QInputMethodEvent>
#include <QHelpEvent>
#include <QMimeData>
#include <QClipboard>
#include "ProjectJournal.h"
#include <QScopeGuard>
#include "../../src/gui/editors/svs/operations/SVSCurveGesture.h"
#include "PatternStore.h"
#include "SampleFrame.h"
using namespace lmms;
class SVSIntegrationTest : public QObject {
 Q_OBJECT
 QTemporaryDir m_configuration;
private slots:
 void initTestCase() { QVERIFY(m_configuration.isValid()); ConfigManager::inst()->loadConfigFile(m_configuration.filePath("svs-test-config.xml")); Engine::init(true); bool available=false; Engine::audioEngine()->setAudioDevice(new AudioDummy(available,Engine::audioEngine()),false); }
 void cleanupTestCase() { Engine::destroy(); }
 void finalEditorInteractionGates() {
  const auto voice=svs::Registry::instance().voices().first(); auto* track=static_cast<SVSTrack*>(Track::create(Track::Type::SVS,Engine::getSong())); track->bindVoice(voice.pluginId,"full"); auto* clip=static_cast<SVSClip*>(track->createClip(0)); svs::Note note; note.id="hover-note"; note.tick=48; note.duration=96; note.lyric="la"; clip->setNotes({note}); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000);
  gui::SVSCanvas canvas(clip); canvas.resize(900,500); const auto point=canvas.noteRect(note).center().toPoint(); QHelpEvent help(QEvent::ToolTip,point,canvas.mapToGlobal(point)); QApplication::sendEvent(&canvas,&help); QVERIFY(canvas.toolTip().contains("voiceDictionary")); QVERIFY(canvas.toolTip().contains("Phonemes"));
  const auto* mode=track->capabilities().parameter("example.mode","track"); QVERIFY(mode); gui::SVSCanvas lane(clip); lane.resize(900,200); lane.setParameterLane(*mode); lane.setTool(gui::SVSCanvas::Tool::Anchor); svs::Curve curve; curve.id=mode->id; curve.type="enum"; curve.interpolation="step"; curve.evaluator.interpolation=svs_sdk::Interpolation::Step; curve.insert(48,"basic"); curve.insert(96,"advanced"); clip->setEditorData(clip->notes(),{{curve.id,curve}});
  const auto anchor=lane.curvePointAt(96,1).toPoint(); QTest::mousePress(&lane,Qt::LeftButton,Qt::NoModifier,anchor+QPoint(-12,-2)); QTest::mouseMove(&lane,anchor+QPoint(12,10)); QTest::mouseRelease(&lane,Qt::LeftButton,Qt::NoModifier,anchor+QPoint(12,10)); lane.deleteSelection(); QCOMPARE(clip->curves()[curve.id].evaluator.points.size(),size_t(1)); QCOMPARE(clip->curves()[curve.id].evaluator.points[0].valueId,std::string("basic"));
  auto* mime=new QMimeData; mime->setData("application/x-lmms-svs-curve",QJsonDocument(QJsonObject{{"curve",curve.toJson()},{"length",48.}}).toJson()); QApplication::clipboard()->setMimeData(mime); const auto before=clip->curves(); auto readOnly=*mode; readOnly.writable=false; lane.setParameterLane(readOnly); lane.pasteSelection(300); QCOMPARE(clip->curves(),before); canvas.pasteSelection(300); QCOMPARE(clip->curves(),before);
  gui::SVSResultStrip strip(clip); auto* journal=Engine::projectJournal(); const auto enabled=journal->isJournalling(); auto reset=qScopeGuard([journal,enabled]{journal->setJournalling(enabled);}); journal->setJournalling(true); clip->setJournalling(true); auto notes=clip->notes(); notes[0].lyric="hello"; clip->setNotes(notes); QTest::keyClick(&strip,Qt::Key_Z,Qt::ControlModifier); QCOMPARE(clip->notes()[0].lyric,QString("la")); QTest::keyClick(&strip,Qt::Key_Y,Qt::ControlModifier); QCOMPARE(clip->notes()[0].lyric,QString("hello")); delete track;
 }
 void relativePitchUsesDeclaredReference() {
  svs::Curve edited; edited.id="svs.pitch"; edited.unit="semitone"; edited.mode="absolute"; edited.interpolation="hermite"; edited.evaluator.interpolation=svs_sdk::Interpolation::Hermite; for(auto point:QVector<QPointF>{{0,60},{110.25,66},{240.5,63},{410.75,71},{600,68}}) edited.insert(point.x(),point.y());
  auto reference=edited; reference.id="svs.referencePitch"; reference.evaluator.points.clear(); for(auto point:QVector<QPointF>{{0,64},{180.5,60},{360.25,70},{600,64}}) reference.insert(point.x(),point.y()); svs::Curve offset; QString error; QVERIFY2(svs::absolutePitchToOffset(edited,reference,offset,error),qPrintable(error));
  for(double tick=0;tick<=600;tick+=.25) {auto relative=offset.valueAt(tick); QVERIFY(relative); QVERIFY(std::abs(relative->toDouble()+reference.valueAt(tick)->toDouble()-edited.valueAt(tick)->toDouble())<1e-9);}
  svs::Curve restored; QVERIFY(svs::Curve::fromJson(offset.toJson(),restored,error)); QCOMPARE(restored.toJson(),offset.toJson());
  auto broken=reference; broken.erase(140,240); QVERIFY(!svs::absolutePitchToOffset(edited,broken,offset,error)); QVERIFY(error.contains("Reference"));
  auto stepped=reference; stepped.interpolation="step"; stepped.evaluator.interpolation=svs_sdk::Interpolation::Step; QVERIFY(svs::absolutePitchToOffset(edited,stepped,offset,error)); for(double tick=0;tick<=600;tick+=.25) QVERIFY(std::abs(offset.valueAt(tick)->toDouble()+stepped.valueAt(tick)->toDouble()-edited.valueAt(tick)->toDouble())<1e-9);
  const auto voice=svs::Registry::instance().voices().first(); auto plugin=svs::Registry::instance().plugin(voice.pluginId); auto schema=plugin->capabilities("full",{},error); auto relativeSchema=schema; auto pitch=relativeSchema["pitch"].toObject(); pitch["input"]="offset"; pitch["referencePitch"]=reference.toJson(); relativeSchema["pitch"]=pitch; svs::Capabilities capabilities; QVERIFY2(svs::Capabilities::parse(relativeSchema,capabilities,error),qPrintable(error));
  auto missing=relativeSchema; pitch.remove("referencePitch"); missing["pitch"]=pitch; QVERIFY(!svs::Capabilities::parse(missing,capabilities,error));
  svs::Input input; input.clipId="offset-test"; input.voiceId="full"; input.revision=1; input.rate=48000; input.secondsPerTick=.002; input.duration=600*input.secondsPerTick; svs::Note note; note.id="pitch-note"; note.tick=0; note.duration=600; input.notes={note}; input.document={{"capabilities",schema},{"language","en"},{"secondsPerTick",input.secondsPerTick},{"curves",svs::curvesToJson({{edited.id,edited}})}};
  auto absolute=plugin->render(input,error); QVERIFY2(absolute!=nullptr,qPrintable(error)); input.document["capabilities"]=relativeSchema; const auto saved=input.document["curves"]; auto converted=plugin->render(input,error); QVERIFY2(converted!=nullptr,qPrintable(error)); QCOMPARE(input.document["curves"],saved); QCOMPARE(absolute->samples.size(),converted->samples.size()); double difference=0; for(size_t i=0;i<absolute->samples.size();++i) difference=std::max(difference,std::abs(double(absolute->samples[i]-converted->samples[i]))); QVERIFY2(difference<1e-5,qPrintable(QString::number(difference,'g',17)));
  const auto values=converted->feedback["pitch"].toArray(); QVERIFY(!values.isEmpty()); for(const auto& value:values) {const auto sample=value.toObject(); const auto tick=sample["startSeconds"].toDouble()/input.secondsPerTick; QVERIFY(std::abs(sample["value"].toDouble()-edited.valueAt(tick)->toDouble())<1e-9);}
 }
 void continuationChainAndNavigation() {
  auto* track=static_cast<SVSTrack*>(Track::create(Track::Type::SVS,Engine::getSong())); const auto voice=svs::Registry::instance().voices().first(); track->bindVoice(voice.pluginId,"full"); auto* clip=static_cast<SVSClip*>(track->createClip(0));
  svs::Note root; root.id="chain-root"; root.tick=0; root.duration=48; root.lyric="la"; auto held=root; held.id="chain-held"; held.tick=48; held.lyric=track->capabilities().continuation; auto second=held; second.id="chain-second"; second.tick=96; auto last=root; last.id="chain-last"; last.tick=144;
  clip->setNotes({last,second,root,held}); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000); const auto chained=clip->audio(); auto pronunciations=chained->feedback["pronunciations"].toObject(); for(const auto& id:QStringList{held.id,second.id}) {QVERIFY(pronunciations[id].toObject()["continuation"].toBool()); QVERIFY(pronunciations[id].toObject()["diagnostic"].toString().isEmpty()); QCOMPARE(pronunciations[id].toObject()["text"],pronunciations[root.id].toObject()["text"]);}
  QVERIFY(gui::editorPronunciation(clip,second).continuation);
  gui::SVSCanvas canvas(clip); canvas.resize(900,500); canvas.beginLyric(root.id); auto* text=canvas.findChild<QLineEdit*>("svsInlineLyric"); QVERIFY(text); QTest::keyClick(text,Qt::Key_Tab); QVERIFY(canvas.selectedNotes().contains(last.id)); QTest::keyClick(text,Qt::Key_Tab,Qt::ShiftModifier); QVERIFY(canvas.selectedNotes().contains(root.id)); QTest::keyClick(text,Qt::Key_Escape);
  root.duration=144; clip->setNotes({root,last}); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000); const auto sustained=clip->audio(); QCOMPARE(chained->samples.size(),sustained->samples.size()); double difference=0; for(size_t i=0;i<chained->samples.size();++i) difference=std::max(difference,std::abs(double(chained->samples[i]-sustained->samples[i]))); QVERIFY2(difference<1e-6,qPrintable(QString::number(difference,'g',17)));
  root.duration=48; clip->setNotes({root,held,second,last}); QTest::keyClick(&canvas,Qt::Key_A,Qt::ControlModifier); canvas.splitSelection(120); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000); pronunciations=clip->audio()->feedback["pronunciations"].toObject(); for(const auto& note:clip->notes()) if(note.lyric==held.lyric) QVERIFY(pronunciations[note.id].toObject()["continuation"].toBool());
  held.tick=49; clip->setNotes({root,held,second}); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000); pronunciations=clip->audio()->feedback["pronunciations"].toObject(); QVERIFY(!pronunciations[held.id].toObject()["continuation"].toBool()); QVERIFY(!pronunciations[second.id].toObject()["continuation"].toBool()); QVERIFY(!pronunciations[held.id].toObject()["diagnostic"].toString().isEmpty()); delete track;
 }
 void noteSplitReparsesPhonemes() {
  auto* journal=Engine::projectJournal(); const auto enabled=journal->isJournalling(); auto reset=qScopeGuard([journal,enabled]{journal->setJournalling(enabled);}); journal->setJournalling(true);
  auto* track=static_cast<SVSTrack*>(Track::create(Track::Type::SVS,Engine::getSong())); const auto voice=svs::Registry::instance().voices().first(); track->bindVoice(voice.pluginId,"full"); auto* clip=static_cast<SVSClip*>(track->createClip(0)); clip->setJournalling(true);
  svs::Note note; note.id="split-phoneme"; note.tick=48; note.duration=192; note.lyric="la"; note.pronunciation="la"; note.parameters={{"example.power",80}}; note.phonemes={{"symbols",QJsonArray{"l","a"}},{"segments",QJsonArray{QJsonObject{{"symbol","l"},{"startTick",-12},{"durationTicks",120}},QJsonObject{{"symbol","a"},{"startTick",108},{"durationTicks",84}}}}}; clip->setNotes({note}); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000);
  svs::Curve curve; curve.id="svs.pitch"; curve.mode="absolute"; curve.insert(0,60.); curve.insert(300,64.); clip->setEditorData(clip->notes(),{{curve.id,curve}});
  gui::SVSCanvas canvas(clip); canvas.resize(900,500); QTest::keyClick(&canvas,Qt::Key_A,Qt::ControlModifier); const auto original=clip->notes(); const auto curves=clip->curves(); const auto depth=journal->undoDepth(); canvas.splitSelection(144); QCOMPARE(clip->notes().size(),2); QCOMPARE(journal->undoDepth(),depth+1); QCOMPARE(clip->notes()[0].duration,96.); QCOMPARE(clip->notes()[1].tick,144.); QCOMPARE(clip->notes()[1].duration,96.); QVERIFY(clip->notes()[1].id!=note.id);
  for(const auto& result:clip->notes()) {QVERIFY(result.phonemes.isEmpty()); QCOMPARE(result.lyric,note.lyric); QCOMPARE(result.pronunciation,note.pronunciation); QCOMPARE(result.parameters,note.parameters);} QCOMPARE(clip->curves(),curves); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000); auto parsed=clip->audio()->feedback["pronunciations"].toObject(); for(const auto& result:clip->notes()) QVERIFY(parsed[result.id].toObject()["generated"].toBool());
  const auto divided=clip->notes(); journal->undo(); QCOMPARE(clip->notes(),original); journal->redo(); QCOMPARE(clip->notes(),divided); QDomDocument doc; auto root=doc.createElement("test"); doc.appendChild(root); track->saveState(doc,root); auto* restored=static_cast<SVSTrack*>(Track::create(root.firstChildElement(),Engine::getSong())); QCOMPARE(static_cast<SVSClip*>(restored->getClip(0))->notes(),divided); delete restored;
  QVERIFY(!svs::splitNoteForReparse(note,note.tick)); QVERIFY(!svs::splitNoteForReparse(note,note.tick+note.duration)); delete track;
 }
 void portraitResourcesAndPreferences() {
  QTemporaryDir package; QVERIFY(package.isValid()); QImage source(160,320,QImage::Format_ARGB32); source.fill(QColor(220,20,40,255)); source.setPixelColor(0,0,Qt::transparent); auto path=package.filePath("portrait.png"); QVERIFY(source.save(path));
  gui::SVSImageLoader loader; int completed=0; loader.changed=[&]{++completed;}; loader.request(package.path(),path,{80,80}); QVERIFY(loader.image().isNull()); QTRY_VERIFY_WITH_TIMEOUT(!loader.image().isNull(),5000); QCOMPARE(loader.image().size(),QSize(40,80)); QVERIFY(loader.image().hasAlphaChannel()); const auto cached=gui::SVSImageLoader::cacheBytes(); QVERIFY(cached<=32*1024*1024);
  auto duplicate=package.filePath("duplicate.png"); QVERIFY(QFile::copy(path,duplicate)); loader.request(package.path(),duplicate,{80,80}); QTRY_VERIFY_WITH_TIMEOUT(!loader.image().isNull(),5000); QCOMPARE(gui::SVSImageLoader::cacheBytes(),cached);
  loader.request(package.path(),path,{100,100}); loader.request(package.path(),package.filePath("missing.png"),{100,100}); QTRY_VERIFY_WITH_TIMEOUT(!loader.diagnostic().isEmpty(),5000); QVERIFY(loader.image().isNull());
  QFile broken(package.filePath("broken.png")); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("broken image"); broken.close(); loader.request(package.path(),broken.fileName(),{80,80}); QTRY_VERIFY_WITH_TIMEOUT(!loader.diagnostic().isEmpty(),5000); QVERIFY(loader.image().isNull());
  loader.request(package.path(),path,{80,80}); QTRY_VERIFY_WITH_TIMEOUT(!loader.image().isNull(),5000);
  for(const auto& variable:QStringList{"LMMS_SVS_TEST_AVATAR","LMMS_SVS_TEST_PORTRAIT"}) { const auto fixture=qEnvironmentVariable(variable.toUtf8().constData()); if(fixture.isEmpty()) continue; auto copy=package.filePath(variable+".png"); QVERIFY(QFile::copy(fixture,copy)); loader.request(package.path(),copy,{400,700}); QTRY_VERIFY_WITH_TIMEOUT(!loader.image().isNull()||!loader.diagnostic().isEmpty(),5000); QVERIFY2(!loader.image().isNull(),qPrintable(loader.diagnostic())); QVERIFY(loader.image().width()<=400&&loader.image().height()<=700); }
  loader.request(package.path(),path,{80,80}); QTRY_VERIFY_WITH_TIMEOUT(!loader.image().isNull(),5000);
  ConfigManager::inst()->setValue("svs","portraitVisible","0"); ConfigManager::inst()->setValue("svs","portraitTransparency","50");
  auto* track=static_cast<SVSTrack*>(Track::create(Track::Type::SVS,Engine::getSong())); QCOMPARE(track->portraitSettings()["transparency"].toInt(),50); QVERIFY(!track->portraitSettings()["visible"].toBool());
  const auto voice=svs::Registry::instance().voices().first(); track->bindVoice(voice.pluginId,"full"); auto* clip=static_cast<SVSClip*>(track->createClip(0)); svs::Note note; note.id="portrait-note"; note.tick=48; clip->setNotes({note}); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000); const auto audio=clip->audio();
  gui::SVSPianoRoll editor(clip); auto* portrait=dynamic_cast<gui::SVSImageLoader*>(editor.findChild<QObject*>("svsPortraitLoader")); QVERIFY(portrait); QTRY_VERIFY_WITH_TIMEOUT(!portrait->image().isNull()||!portrait->diagnostic().isEmpty(),5000); QVERIFY2(!portrait->image().isNull(),qPrintable(portrait->diagnostic())); auto* visible=editor.findChild<QCheckBox*>("svsPortraitVisible"); auto* value=editor.findChild<QSpinBox*>("svsPortraitTransparencyValue"); auto* slider=editor.findChild<QSlider*>("svsPortraitTransparency"); QVERIFY(visible&&value&&slider); visible->setChecked(true); value->setValue(0); QCOMPARE(slider->value(),0); QCOMPARE(clip->audio(),audio);
  auto settings=track->portraitSettings(); settings["x"]=.25; settings["y"]=.5; track->setPortraitSettings(settings); const auto preferences=track->portraitSettings(); track->bindVoice(voice.pluginId,"minimal"); QCOMPARE(track->portraitSettings(),preferences);
  editor.setStyleSheet("lmms--gui--SVSPianoRoll { qproperty-backgroundColor: #ffffff; }"); QCOMPARE(track->portraitSettings(),preferences);
  QDomDocument doc; auto root=doc.createElement("test"); doc.appendChild(root); track->saveState(doc,root); ConfigManager::inst()->setValue("svs","portraitTransparency","99"); auto* restored=static_cast<SVSTrack*>(Track::create(root.firstChildElement(),Engine::getSong())); QCOMPARE(restored->portraitSettings(),preferences); delete restored;
  gui::SVSCanvas canvas(clip); canvas.resize(500,300); canvas.setScroll(0,66); canvas.setThemeColors({{"backgroundColor",QColor("#101010")},{"gridLineColor",QColor("#101010")}}); source.fill(QColor(220,20,40));
  auto rendered=[&](int transparency){canvas.setPortrait(source,true,transparency); QImage image(canvas.size(),QImage::Format_ARGB32); canvas.render(&image);return image.pixelColor(490,290);};
  QCOMPARE(rendered(0),QColor(220,20,40)); const auto half=rendered(50); QVERIFY(std::abs(half.red()-118)<=1); QCOMPARE(rendered(100),QColor("#101010")); canvas.setPortrait(source,true,0); const auto click=canvas.noteRect(note).center().toPoint(); QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,click); QVERIFY(canvas.selectedNotes().contains(note.id));
  auto* defaults=editor.findChild<QPushButton*>("svsPortraitDefaults"); QVERIFY(defaults); value->setValue(50); defaults->click(); auto* newTrack=static_cast<SVSTrack*>(Track::create(Track::Type::SVS,Engine::getSong())); QCOMPARE(newTrack->portraitSettings()["transparency"].toInt(),50); delete newTrack;
  auto* reset=editor.findChild<QPushButton*>("svsPortraitReset"); QVERIFY(reset); reset->click(); QCOMPARE(track->portraitSettings()["transparency"].toInt(),70); QCOMPARE(track->portraitSettings()["x"].toDouble(),1.); delete track;
  ConfigManager::inst()->deleteValue("svs","portraitVisible"); ConfigManager::inst()->deleteValue("svs","portraitTransparency");
 }
 void batchLyricsAndPronunciation() {
  auto* track=static_cast<SVSTrack*>(Track::create(Track::Type::SVS,Engine::getSong())); auto voice=svs::Registry::instance().voices().first(); track->bindVoice(voice.pluginId,"full");
  QTRY_VERIFY_WITH_TIMEOUT(!track->capabilities().languages.isEmpty(),10000);
  auto* clip=static_cast<SVSClip*>(track->createClip(0)); svs::Note first; first.id="a"; first.tick=0; first.duration=48; first.lyric="la"; first.pronunciation="retained"; first.phonemes={{"symbols",QJsonArray{"a"}}};
  auto held=first; held.id="b"; held.tick=48; held.lyric=track->capabilities().continuation; held.pronunciation.clear(); held.phonemes={}; auto last=held; last.id="c"; last.tick=96; last.lyric="la"; clip->setNotes({first,held,last}); const auto original=clip->notes();
  QCOMPARE(gui::SVSLyricEditor::splitLyrics(QString::fromUtf8("hello 世界 きゃ -")),QStringList({"hello",QString::fromUtf8("世"),QString::fromUtf8("界"),QString::fromUtf8("きゃ"),"-"}));
  { gui::SVSLyricEditor dialog(clip,{"a","b","c"}); auto* text=dialog.findChild<QPlainTextEdit*>("svsBatchLyricText"); QVERIFY(text); text->setPlainText(QString::fromUtf8("你 好")); QCOMPARE(clip->notes(),original); auto* table=dialog.findChild<QTableWidget*>("svsBatchLyricPreview"); QCOMPARE(table->item(0,2)->text(),QString::fromUtf8("你")); QCOMPARE(table->item(1,2)->text(),held.lyric); QCOMPARE(table->item(2,2)->text(),QString::fromUtf8("好")); dialog.reject(); QCOMPARE(clip->notes(),original); }
  Engine::projectJournal()->setJournalling(true); clip->setJournalling(true);
  { gui::SVSLyricEditor dialog(clip,{"a","b","c"}); auto* text=dialog.findChild<QPlainTextEdit*>(); text->setPlainText("one two"); QInputMethodEvent composing(QString::fromUtf8("未"),{}); QApplication::sendEvent(text,&composing); dialog.accept(); QCOMPARE(clip->notes(),original); QInputMethodEvent commit; commit.setCommitString(""); QApplication::sendEvent(text,&commit); dialog.accept(); QCOMPARE(clip->notes()[0].lyric,QString("one")); QCOMPARE(clip->notes()[2].lyric,QString("two")); QCOMPARE(clip->notes()[0].phonemes,first.phonemes); QCOMPARE(clip->notes()[0].pronunciation,first.pronunciation); }
  Engine::projectJournal()->undo(); QCOMPARE(clip->notes(),original); Engine::projectJournal()->redo(); QCOMPARE(clip->notes()[2].lyric,QString("two"));
  gui::SVSCanvas canvas(clip); canvas.setPronunciation("c","manual"); QCOMPARE(clip->notes()[2].pronunciation,QString("manual")); canvas.setPronunciation("c",{}); QVERIFY(clip->notes()[2].pronunciation.isEmpty());
  bool foundCandidates=false; for(const auto& dictionary:track->dictionaries()) for(auto entry=dictionary.entries.begin();entry!=dictionary.entries.end();++entry) if(!foundCandidates&&entry.value().toArray().size()>1) {
   auto notes=clip->notes(); notes[2].lyric=entry.key(); notes[2].language=dictionary.language; clip->setNotes(notes); const auto candidates=gui::editorPronunciation(clip,clip->notes()[2],true).candidates; QVERIFY(candidates.size()>1);
   const auto choice=candidates.last().toObject()["reading"].toString(); canvas.setPronunciation("c",choice); const auto resolved=gui::editorPronunciation(clip,clip->notes()[2]); QCOMPARE(resolved.text,choice); QCOMPARE(resolved.source,QString("manualPronunciation")); QVERIFY(resolved.generated); foundCandidates=true;
  } QVERIFY(foundCandidates);
  { gui::SVSLyricEditor dialog(clip,{"a"}); auto newer=clip->notes(); newer[0].lyric="changed elsewhere"; clip->setNotes(newer); dialog.accept(); QCOMPARE(clip->notes(),newer); QCOMPARE(dialog.result(),0); }
  Engine::projectJournal()->setJournalling(false); delete track;
 }
 void continuousCurves() {
  svs::Curve curve; curve.id="pitch"; curve.unit="semitone"; curve.interpolation="hermite";
  curve.evaluator.interpolation=svs_sdk::Interpolation::Hermite;
  for(const auto& point:QVector<QPointF>{{0,60},{120.5,64},{260.25,67},{410.5,69},{600,72}}) curve.insert(point.x(),point.y());
  for(double tick:{120.5,192.,260.25,384.,410.5,576.}) {
   auto left=curve.evaluator.evaluate(tick-1e-5),right=curve.evaluator.evaluate(tick+1e-5);
   QVERIFY(left.covered&&right.covered); QVERIFY(std::abs(left.value-right.value)<1e-5); QVERIFY(std::abs(left.derivative-right.derivative)<1e-5);
  }
  const auto copied=curve.slice(150.25,550.5);
  for(double tick=150.25;tick<=550.5;tick+=.75) QVERIFY(std::abs(curve.valueAt(tick)->toDouble()-copied.valueAt(tick-150.25)->toDouble())<1e-8);
  curve.erase(205.5,300.25); QVERIFY(!curve.valueAt(250)); QVERIFY(curve.valueAt(205.5));
  auto bytes=QJsonDocument(curve.toJson()).toJson(); svs::Curve restored; QString error;
  QVERIFY2(svs::Curve::fromJson(QJsonDocument::fromJson(bytes).object(),restored,error),qPrintable(error));
  QVERIFY(!restored.valueAt(250)); QCOMPARE(restored.toJson(),curve.toJson());
  restored.connect(220,280); QVERIFY(restored.valueAt(250)); QVERIFY(!restored.valueAt(210)); QVERIFY(!restored.valueAt(290));
  svs::Curve discrete; discrete.id="enum"; discrete.type="enum"; discrete.interpolation="step"; discrete.evaluator.interpolation=svs_sdk::Interpolation::Step;
  discrete.insert(0,"first"); discrete.insert(12.5,"second"); QCOMPARE(discrete.valueAt(12)->toString(),QString("first")); QCOMPARE(discrete.valueAt(12.5)->toString(),QString("second"));
 }
 void waveformPyramid() {
  std::vector<float> samples(8192*2); for(size_t frame=0;frame<8192;++frame) { samples[frame*2]=float(std::sin(frame*.04)); samples[frame*2+1]=float(std::cos(frame*.07)); }
  svs::Waveform waveform; waveform.build(samples); QVERIFY(waveform.bytes()<samples.size()*sizeof(float)/16);
  for(size_t first=0;first<8192;first+=512) { auto peak=waveform.peak(first,first+512); float low=1,high=-1; for(size_t frame=first;frame<first+512;++frame) { low=std::min({low,samples[frame*2],samples[frame*2+1]}); high=std::max({high,samples[frame*2],samples[frame*2+1]}); } QCOMPARE(peak.minimum,low); QCOMPARE(peak.maximum,high); }
  waveform.build({}); QCOMPARE(waveform.bytes(),size_t(0)); QCOMPARE(waveform.peak(0,64).maximum,0.f);
 }
 void phonemeBoundariesAndAttributes() {
  const auto& voice=svs::Registry::instance().voices()[0]; auto* track=static_cast<SVSTrack*>(Track::create(Track::Type::SVS,Engine::getSong())); track->bindVoice(voice.pluginId,"full"); auto* clip=static_cast<SVSClip*>(track->createClip(192)); svs::Note note; note.id="phoneme-note"; note.tick=48; note.duration=96; note.lyric="la"; clip->setNotes({note}); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000);
  gui::SVSResultStrip strip(clip); strip.resize(900,110); const auto initial=clip->notes(); QTest::mousePress(&strip,Qt::LeftButton,Qt::NoModifier,QPoint(252,75)); QTest::mouseMove(&strip,QPoint(276,75)); QCOMPARE(clip->notes(),initial); QTest::mouseRelease(&strip,Qt::LeftButton,Qt::NoModifier,QPoint(276,75)); auto segments=clip->notes()[0].phonemes["segments"].toArray(); QCOMPARE(segments.size(),2); QCOMPARE(segments[0].toObject()["durationTicks"].toDouble(),60.); QCOMPARE(segments[1].toObject()["startTick"].toDouble(),60.); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000);
  QTest::mousePress(&strip,Qt::LeftButton,Qt::NoModifier,QPoint(156,75)); QTest::mouseRelease(&strip,Qt::LeftButton,Qt::NoModifier,QPoint(132,75)); QCOMPARE(clip->notes()[0].phonemes["segments"].toArray()[0].toObject()["startTick"].toDouble(),-12.); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000);
  const auto audio=clip->audio(); const double secondsPerTick=60./(Engine::getSong()->getTempo()*(DefaultTicksPerBar/4)); auto energy=[secondsPerTick](const auto& audio,double from,double to){double result=0; for(size_t frame=size_t(from*secondsPerTick*audio->rate);frame<size_t(to*secondsPerTick*audio->rate);++frame) result+=audio->samples[frame*2]*audio->samples[frame*2]; return result;}; QVERIFY(energy(audio,40,44)>1e-3);
  QVERIFY(strip.setSelectedParameter("example.phonemeGain",0)); QVERIFY(!strip.setSelectedParameter("example.phonemeGain",3)); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000); QCOMPARE(energy(clip->audio(),60,80),0.); QVERIFY(energy(clip->audio(),120,140)>1);
  const auto before=clip->notes(); QTest::mousePress(&strip,Qt::LeftButton,Qt::NoModifier,QPoint(276,75)); QTest::mouseMove(&strip,QPoint(300,75)); QTest::keyClick(&strip,Qt::Key_Escape); QCOMPARE(clip->notes(),before);
  QDomDocument doc; auto root=doc.createElement("test"); doc.appendChild(root); track->saveState(doc,root); auto* restored=static_cast<SVSTrack*>(Track::create(root.firstChildElement(),Engine::getSong())); QCOMPARE(static_cast<SVSClip*>(restored->getClip(0))->notes()[0].phonemes,before[0].phonemes); delete restored;
  track->bindVoice(voice.pluginId,"minimal"); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000); QTest::mousePress(&strip,Qt::LeftButton,Qt::NoModifier,QPoint(252,75)); QTest::mouseRelease(&strip,Qt::LeftButton,Qt::NoModifier,QPoint(300,75)); QCOMPARE(clip->notes(),before); QVERIFY(!strip.setSelectedParameter("example.phonemeGain",1)); delete track;
 }
 void canvasThemeProperties() {
  QImage placeholder("data:/themes/default/svs_track.svg"); QVERIFY(!placeholder.isNull()); QCOMPARE(placeholder.size(),QSize(24,24));
  auto* track=static_cast<SVSTrack*>(Track::create(Track::Type::SVS,Engine::getSong())); auto* clip=static_cast<SVSClip*>(track->createClip(0));
  gui::SVSPianoRoll editor(clip); auto* canvas=editor.findChild<gui::SVSCanvas*>(); QVERIFY(canvas);
  auto renderColor=[canvas]{ canvas->window()->layout()->activate(); QImage image(canvas->size(),QImage::Format_ARGB32); image.fill(Qt::transparent); canvas->render(&image); return image.pixelColor(image.width()-5,image.height()-5); };
  editor.setStyleSheet("lmms--gui--SVSPianoRoll { qproperty-backgroundColor: #132435; qproperty-noteColor: #456789; }"); editor.ensurePolished();
  QCOMPARE(editor.backgroundColor(),QColor("#132435")); QCOMPARE(editor.noteColor(),QColor("#456789")); QCOMPARE(renderColor(),QColor("#132435"));
  editor.setStyleSheet("lmms--gui--SVSPianoRoll { qproperty-backgroundColor: #abcdef; }"); editor.ensurePolished(); QCOMPARE(renderColor(),QColor("#abcdef"));
  editor.setStyleSheet(""); editor.ensurePolished(); QVERIFY(!editor.backgroundColor().isValid()); QCOMPARE(renderColor(),canvas->palette().base().color());
  delete track;
 }
 void curveRangeTransactions() {
  svs::Curve original; original.id="svs.pitch"; original.mode="absolute"; original.interpolation="hermite"; original.evaluator.interpolation=svs_sdk::Interpolation::Hermite;
  for(const auto& point:QVector<QPointF>{{0,60},{100,65},{250,67},{400,70},{600,72}}) original.insert(point.x(),point.y());
  gui::SVSCurveGesture gesture; gesture.begin(original,150.25,64.,gui::SVSCurveGesture::Kind::Line); gesture.update(350.75,68.);
  for(double tick=0;tick<150.25;tick+=.75) QVERIFY(std::abs(original.valueAt(tick)->toDouble()-gesture.preview.valueAt(tick)->toDouble())<1e-8);
  for(double tick=351;tick<=600;tick+=.75) QVERIFY(std::abs(original.valueAt(tick)->toDouble()-gesture.preview.valueAt(tick)->toDouble())<1e-8);
  QCOMPARE(gesture.preview.valueAt(250.5)->toDouble(),66.);
  auto piece=gesture.preview.slice(175,325); for(double tick=175;tick<=325;tick+=.25) QVERIFY(std::abs(piece.valueAt(tick-175)->toDouble()-gesture.preview.valueAt(tick)->toDouble())<1e-8);
  gesture.begin(original,150.25,64.,gui::SVSCurveGesture::Kind::Smooth); gesture.update(350.75,68.); QVERIFY(std::abs(gesture.preview.derivativeAt(150.25))<1e-8);
  gesture.begin(original,192.5,64.,gui::SVSCurveGesture::Kind::Erase); gesture.update(384.25,68.); QVERIFY(!gesture.preview.valueAt(250));
  for(double tick=0;tick<192.5;tick+=.75) QVERIFY(std::abs(original.valueAt(tick)->toDouble()-gesture.preview.valueAt(tick)->toDouble())<1e-8);
 }
 void pitchDrawingAndPluginEvaluation() {
  const auto& voice=svs::Registry::instance().voices()[0]; auto* track=static_cast<SVSTrack*>(Track::create(Track::Type::SVS,Engine::getSong())); track->bindVoice(voice.pluginId,"full");
  auto* clip=static_cast<SVSClip*>(track->createClip(192)); QVector<svs::Note> notes;
  for(int i=0;i<4;++i) { svs::Note note; note.id=QString("pitch-%1").arg(i); note.tick=i==3?432:i*120; note.duration=i==3?144:96; note.pitch=60+i*3; note.lyric="la"; notes.push_back(note); }
  clip->setNotes(notes); QVERIFY(clip->setParameter("example.breath",0)); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000); auto baseline=clip->audio();
  gui::SVSCanvas canvas(clip); canvas.resize(1400,500); canvas.setTool(gui::SVSCanvas::Tool::Freehand);
  auto begin=canvas.curvePointAt(0,64).toPoint(),end=canvas.curvePointAt(600.25,70).toPoint();
  QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,begin); QVERIFY(clip->curves().isEmpty());
  for(auto point:QVector<QPointF>{{120.25,65},{260.5,67},{410.25,68}}) QTest::mouseMove(&canvas,canvas.curvePointAt(point.x(),point.y()).toPoint());
  QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,end); QVERIFY(clip->curves().contains("svs.pitch"));
  const auto drawn=clip->curves()["svs.pitch"]; QVERIFY(drawn.valueAt(192)); QVERIFY(drawn.valueAt(384)); QVERIFY(drawn.valueAt(576)); QCOMPARE(clip->notes(),notes);
  QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000); const double secondsPerTick=60./(Engine::getSong()->getTempo()*(DefaultTicksPerBar/4));
  for(const auto& item:clip->audio()->feedback["pitch"].toArray()) { const auto sample=item.toObject(); const auto tick=sample["startSeconds"].toDouble()/secondsPerTick; auto value=drawn.valueAt(tick); QVERIFY(value); QVERIFY(std::abs(sample["value"].toDouble()-value->toDouble())<5e-4); }
  canvas.setTool(gui::SVSCanvas::Tool::Erase); const auto before=clip->curves(); QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,canvas.curvePointAt(200,66).toPoint()); QTest::mouseMove(&canvas,canvas.curvePointAt(300,67).toPoint()); QTest::keyClick(&canvas,Qt::Key_Escape); QCOMPARE(clip->curves(),before);
  auto curves=clip->curves(); auto constant=drawn; constant.evaluator.points.clear(); constant.evaluator.gaps.clear(); constant.insert(0,72.); constant.insert(600,72.); curves["svs.pitch"]=constant; clip->setEditorData(notes,curves); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000); auto octave=clip->audio();
  auto crossings=[](const std::shared_ptr<const svs::Audio>& audio) { int count=0; for(size_t frame=size_t(audio->rate*.1);frame<size_t(audio->rate*.4);++frame) if(audio->samples[(frame-1)*2]<=0&&audio->samples[frame*2]>0) ++count; return count; };
  QVERIFY(std::abs(double(crossings(octave))/crossings(baseline)-2)<.05);
  svs::Curve power; power.id="example.power"; power.type="int"; power.interpolation="step"; power.evaluator.interpolation=svs_sdk::Interpolation::Step; power.insert(0,50); power.insert(600,50); curves[power.id]=power; clip->setEditorData(notes,curves); QTRY_VERIFY_WITH_TIMEOUT(clip->audio()!=nullptr,10000);
  auto energy=[](const auto& audio){double result=0; for(auto value:audio->samples) result+=value*value; return result;}; QVERIFY(std::abs(energy(clip->audio())/energy(octave)-.25)<1e-6);
  QDomDocument doc; auto root=doc.createElement("test"); doc.appendChild(root); track->saveState(doc,root); auto* restored=static_cast<SVSTrack*>(Track::create(root.firstChildElement(),Engine::getSong())); QCOMPARE(static_cast<SVSClip*>(restored->getClip(0))->curves(),curves);
  delete restored; delete track;
 }
 void curveUndoAndSelectionMove() {
  auto* journal=Engine::projectJournal(); const auto wasJournalling=journal->isJournalling(); auto restoreJournal=qScopeGuard([journal,wasJournalling]{journal->setJournalling(wasJournalling);}); journal->setJournalling(true);
  const auto& voice=svs::Registry::instance().voices()[0]; auto* track=static_cast<SVSTrack*>(Track::create(Track::Type::SVS,Engine::getSong())); track->bindVoice(voice.pluginId,"full"); auto* clip=static_cast<SVSClip*>(track->createClip(0)); clip->setJournalling(true);
  svs::Note note; note.id="undo-note"; note.tick=48; note.duration=96; note.pitch=64; clip->setNotes({note}); gui::SVSCanvas canvas(clip); canvas.resize(900,500); canvas.setTool(gui::SVSCanvas::Tool::Line); const auto depth=journal->undoDepth();
  QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,canvas.curvePointAt(0,60).toPoint()); QTest::mouseMove(&canvas,canvas.curvePointAt(192,66).toPoint()); QCOMPARE(journal->undoDepth(),depth); QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,canvas.curvePointAt(384,72).toPoint()); QCOMPARE(journal->undoDepth(),depth+1); const auto drawn=clip->curves();
  QTest::keyClick(&canvas,Qt::Key_Z,Qt::ControlModifier); QVERIFY(clip->curves().isEmpty()); QTest::keyClick(&canvas,Qt::Key_Z,Qt::ControlModifier|Qt::ShiftModifier); QCOMPARE(clip->curves(),drawn);
  canvas.setTool(gui::SVSCanvas::Tool::Notes); auto body=canvas.noteRect(note).center().toPoint(); QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,body); QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,body+QPoint(48,0)); QCOMPARE(clip->curves(),drawn);
  note=clip->notes()[0]; const auto from=note.tick,to=note.tick+note.duration; auto shape=drawn["svs.pitch"].slice(from,to); canvas.setMoveCurves(true); body=canvas.noteRect(note).center().toPoint(); const auto beforeMove=journal->undoDepth(); QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,body); QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,body+QPoint(48,0)); QCOMPARE(journal->undoDepth(),beforeMove+1);
  for(double tick=0;tick<=note.duration;tick+=.5) QVERIFY(std::abs(shape.valueAt(tick)->toDouble()-clip->curves()["svs.pitch"].valueAt(from+24+tick)->toDouble())<1e-8);
  QTest::keyClick(&canvas,Qt::Key_Z,Qt::ControlModifier); QCOMPARE(clip->curves(),drawn); QCOMPARE(clip->notes()[0].tick,from); delete track;
 }
 void noteTransactionsAndInlineLyrics() {
  const auto& voice=svs::Registry::instance().voices()[0];
  auto* track=static_cast<SVSTrack*>(Track::create(Track::Type::SVS,Engine::getSong())); track->bindVoice(voice.pluginId,"full");
  auto* clip=static_cast<SVSClip*>(track->createClip(192)); gui::SVSCanvas canvas(clip); canvas.resize(900,500);
  const auto position=canvas.pointAt(24,64)+QPointF(0,6);
  QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,position.toPoint()); QVERIFY(clip->notes().isEmpty());
  QTest::mouseDClick(&canvas,Qt::LeftButton,Qt::NoModifier,position.toPoint()); QVERIFY(clip->notes().isEmpty());
  QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,position.toPoint()); QCOMPARE(clip->notes().size(),1);
  auto note=clip->notes()[0]; note.duration=96; clip->setNotes({note}); const auto original=clip->notes();
  const auto body=canvas.noteRect(note).center().toPoint();
  QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,body); QTest::mouseMove(&canvas,body+QPoint(48,0)); QCOMPARE(clip->notes(),original);
  QTest::keyClick(&canvas,Qt::Key_Escape); QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,body+QPoint(48,0)); QCOMPARE(clip->notes(),original);
  QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,body); QTest::mouseMove(&canvas,body+QPoint(48,0)); canvas.setTool(gui::SVSCanvas::Tool::Anchor); QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,body+QPoint(48,0)); QCOMPARE(clip->notes(),original);
  canvas.setTool(gui::SVSCanvas::Tool::Notes); QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,body); QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,body+QPoint(48,0)); QCOMPARE(clip->notes()[0].tick,48.); QCOMPARE(clip->notes()[0].duration,96.);
  canvas.beginLyric(note.id); auto* lyric=canvas.findChild<QLineEdit*>("svsInlineLyric"); QVERIFY(lyric); lyric->setText("edited"); QCOMPARE(clip->notes()[0].lyric,note.lyric); QTest::keyClick(lyric,Qt::Key_Return); QCOMPARE(clip->notes()[0].lyric,QString("edited"));
  canvas.beginLyric(note.id); lyric->setText("cancelled"); QTest::keyClick(lyric,Qt::Key_Escape); QCOMPARE(clip->notes()[0].lyric,QString("edited"));
  canvas.copySelection(); canvas.pasteSelection(384); QCOMPARE(clip->notes().size(),2); QCOMPARE(clip->notes()[1].tick,384.); QCOMPARE(clip->notes()[1].pitch,note.pitch); QVERIFY(clip->notes()[1].id!=note.id);
  QVERIFY(int(clip->length())>=480); delete track;
 }
 void parameterLaneDrawingAndPersistence() {
  const auto& voice=svs::Registry::instance().voices()[0]; auto* track=static_cast<SVSTrack*>(Track::create(Track::Type::SVS,Engine::getSong())); track->bindVoice(voice.pluginId,"full"); auto* clip=static_cast<SVSClip*>(track->createClip(0));
  svs::Note note; note.id="lane-note"; clip->setNotes({note}); gui::SVSPianoRoll editor(clip);
  auto* main=editor.findChild<gui::SVSCanvas*>("svsNoteCanvas"); QVERIFY(main);
  auto* tension=editor.findChild<gui::SVSCanvas*>("svsParameterLane.example.tension.input"); QVERIFY(tension); tension->resize(900,110); tension->setTool(gui::SVSCanvas::Tool::Line);
  QTest::mousePress(tension,Qt::LeftButton,Qt::NoModifier,tension->curvePointAt(0,.2).toPoint()); QVERIFY(clip->curves().isEmpty()); QTest::mouseRelease(tension,Qt::LeftButton,Qt::NoModifier,tension->curvePointAt(300.25,.8).toPoint()); QVERIFY(clip->curves().contains("example.tension")); auto value=clip->curves()["example.tension"].valueAt(150); QVERIFY(value); QVERIFY(std::abs(value->toDouble()-.5)<.02);
  auto* soft=editor.findChild<gui::SVSCanvas*>("svsParameterLane.example.soft.input"); QVERIFY(soft); soft->resize(900,110); soft->setTool(gui::SVSCanvas::Tool::Freehand); QTest::mousePress(soft,Qt::LeftButton,Qt::NoModifier,soft->curvePointAt(0,0).toPoint()); QTest::mouseRelease(soft,Qt::LeftButton,Qt::NoModifier,soft->curvePointAt(300,1).toPoint()); QCOMPARE(clip->curves()["example.soft"].type,QString("bool")); QCOMPARE(clip->curves()["example.soft"].valueAt(150)->toBool(),false); QCOMPARE(clip->curves()["example.soft"].valueAt(300)->toBool(),true);
  auto* mode=editor.findChild<gui::SVSCanvas*>("svsParameterLane.example.mode.input"); QVERIFY(mode); mode->resize(900,110); mode->setTool(gui::SVSCanvas::Tool::Line); QTest::mousePress(mode,Qt::LeftButton,Qt::NoModifier,mode->curvePointAt(0,0).toPoint()); QTest::mouseRelease(mode,Qt::LeftButton,Qt::NoModifier,mode->curvePointAt(300,1).toPoint()); QCOMPARE(clip->curves()["example.mode"].valueAt(150)->toString(),QString("basic")); QCOMPARE(clip->curves()["example.mode"].valueAt(300)->toString(),QString("advanced"));
  auto* result=editor.findChild<gui::SVSCanvas*>("svsParameterLane.example.energy.feedback"); QVERIFY(result); const auto before=clip->curves(); result->setTool(gui::SVSCanvas::Tool::Freehand); QTest::mousePress(result,Qt::LeftButton,Qt::NoModifier,QPoint(100,50)); QTest::mouseRelease(result,Qt::LeftButton,Qt::NoModifier,QPoint(200,70)); QCOMPARE(clip->curves(),before);
  const auto topPitch=main->topPitch(); main->setScroll(84,topPitch); QCOMPARE(tension->scrollTick(),84.); tension->setZoom(1.5,1); QCOMPARE(main->horizontalZoom(),1.5); QCOMPARE(main->topPitch(),topPitch); QCOMPARE(clip->editorState()["topPitch"].toDouble(),topPitch);
  auto* visible=editor.findChild<QCheckBox*>("svsParameterVisible.input:example.tension"); auto* height=editor.findChild<QSpinBox*>("svsParameterHeight.input:example.tension"); QVERIFY(visible&&height); visible->setChecked(false); height->setValue(160); QCOMPARE(clip->editorState()["lanes"].toObject()["input:example.tension"].toObject()["visible"].toBool(),false); QCOMPARE(clip->editorState()["lanes"].toObject()["input:example.tension"].toObject()["height"].toInt(),160);
  track->bindVoice(voice.pluginId,"minimal"); QVERIFY(tension->parentWidget()->isHidden()); QCOMPARE(clip->curves(),before); track->bindVoice(voice.pluginId,"full"); QCOMPARE(editor.findChild<gui::SVSCanvas*>("svsParameterLane.example.tension.input"),tension); QVERIFY(!visible->isChecked()); QCOMPARE(height->value(),160);
  QDomDocument doc; auto root=doc.createElement("test"); doc.appendChild(root); track->saveState(doc,root); auto* restored=static_cast<SVSTrack*>(Track::create(root.firstChildElement(),Engine::getSong())); auto* saved=static_cast<SVSClip*>(restored->getClip(0)); QCOMPARE(saved->editorState(),clip->editorState()); QCOMPARE(saved->curves(),before); delete restored; delete track;
 }
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
  auto* clip=static_cast<SVSClip*>(track->createClip(192)); { gui::SVSPianoRoll editor(clip,nullptr); auto* canvas=editor.findChild<QWidget*>("svsNoteCanvas"); QVERIFY(canvas); QTest::mouseDClick(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(108,222)); QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(108,222)); QCOMPARE(clip->notes().size(),1); QCOMPARE(clip->notes()[0].tick,24.); QCOMPARE(clip->notes()[0].pitch,64.); QCOMPARE(clip->notes()[0].lyric,voices[0].defaultLyric); } svs::Note note; note.id="note-fixture"; note.tick=24; note.duration=96; note.pitch=64; note.lyric=QString::fromUtf8("测试"); note.parameters={{"unknown.parameter",42}}; clip->setNotes({note});
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
