#include <QtTest>
#include <QScreen>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMenuBar>
#include <QProcess>
#include <QProcessEnvironment>
#include "AudioDummy.h"
#include "ConfigManager.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "Song.h"
#include "SongEditor.h"
#include "PatternEditor.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "MidiClip.h"
#include "SampleTrack.h"
#include "SampleClip.h"
#include "AutomationTrack.h"
#include "AutomationClip.h"
#include "PianoRoll.h"
#include "AutomationEditor.h"
#include "SVSTrack.h"
#include "SVSClip.h"
#include "SVSViews.h"
#include "SetupDialog.h"
#include "ExportProjectDialog.h"
#include "Mixer.h"
#include "MixerView.h"
#include "Effect.h"
#include "EffectControls.h"
#include "EffectControlDialog.h"
#include "Controller.h"
#include "ControllerRackView.h"
#include "PluginFactory.h"
#include "PluginView.h"
#include "SubWindow.h"

using namespace lmms;
using namespace lmms::gui;

// Capture production widgets through the native desktop, never QWidget::render.
class UiBaselineCapture : public QObject
{
 Q_OBJECT
 QTemporaryDir m_config;
 std::unique_ptr<GuiApplication> m_gui;
 QString m_output;
 QList<Plugin*> m_toolPlugins;
 void capture(QWidget* widget, const QString& name)
 {
  widget->show(); widget->raise(); widget->activateWindow();
  QVERIFY(QTest::qWaitForWindowExposed(widget->window()));
  QTest::qWait(600);
  const auto image = widget->screen()->grabWindow(widget->window()->winId());
  QVERIFY(!image.isNull());
  QVERIFY(image.save(m_output + '/' + name + ".png"));
 }
 void showEditor(QWidget* widget, const QString& name)
 {
  widget->show();
  auto* frame = qobject_cast<QMdiSubWindow*>(widget->parentWidget());
  if(frame) {frame->show();frame->setGeometry(0,0,1000,600);frame->raise();}
  capture(widget->isWindow()?widget:static_cast<QWidget*>(m_gui->mainWindow()),name);
  if(frame) frame->hide();
  else if(widget->isWindow()) widget->hide();
 }
private slots:
 void initTestCase()
 {
  QCOMPARE(QGuiApplication::platformName(), QString("windows"));
  QVERIFY(m_config.isValid());
  m_output=qEnvironmentVariable("LMMS_UI_EVIDENCE");
  QVERIFY(!m_output.isEmpty()); QVERIFY(QDir().mkpath(m_output));
  auto* config=ConfigManager::inst();
  config->loadConfigFile(m_config.filePath("ui-config.xml"));
  config->setWorkingDir(m_config.path()+'/');
  const auto stkDir=qEnvironmentVariable("LMMS_UI_STK_DIR");
  if(!stkDir.isEmpty())
  {
   QVERIFY(QFile::exists(stkDir+"/sinewave.raw"));
   config->setSTKDir(stkDir);
  }
  config->setValue("app","configured","1");
  config->setValue("audioengine","audiodev",AudioDummy::name());
  m_gui=std::make_unique<GuiApplication>();
  auto* window=m_gui->mainWindow();window->resize(1280,800);window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));
  QJsonObject environment{{"qt",qVersion()},{"platform",QGuiApplication::platformName()},
   {"scale",window->devicePixelRatioF()},{"font",window->font().toString()},
   {"theme","default"},{"windowWidth",window->width()},{"windowHeight",window->height()},
   {"language",QLocale().name()},{"simulatedScale",qEnvironmentVariable("QT_SCALE_FACTOR")}};
  QFile metadata(m_output+"/environment.json");QVERIFY(metadata.open(QIODevice::WriteOnly));
  metadata.write(QJsonDocument(environment).toJson());
 }
 void scenes()
 {
  auto* song=Engine::getSong();song->createNewProject();
  auto* midiTrack=new InstrumentTrack(song);midiTrack->setName("MIDI / 长名称乐器轨道");
  QVERIFY(midiTrack->loadInstrument("tripleoscillator"));
  midiTrack->setName("MIDI / 长名称乐器轨道");
  auto* midi=static_cast<MidiClip*>(midiTrack->createClip(0));
  for(int i=0;i<8;++i) midi->addNote(Note(TimePos(i==0?3:24),TimePos(i*24),60+i%5));
  auto* pattern=new PatternTrack(song);pattern->createClip(192);
  auto* beatTrack=new InstrumentTrack(Engine::patternStore());
  QVERIFY(beatTrack->loadInstrument("kicker"));
  auto* beat=static_cast<MidiClip*>(beatTrack->createClip(0));
  beat->setSteps(16);for(int i=0;i<16;i+=4) beat->setStep(i,true);
  auto* sampleTrack=new SampleTrack(song);
  auto* sample=static_cast<SampleClip*>(sampleTrack->createClip(0));
  const auto wav=qEnvironmentVariable("LMMS_UI_SAMPLE");QVERIFY(QFile::exists(wav));
  sample->setSampleFile(wav);
  auto* automationTrack=new AutomationTrack(song);
  auto* automation=static_cast<AutomationClip*>(automationTrack->createClip(0));
  automation->addObject(&song->tempoModel());automation->putValue(0,120);automation->putValue(96,140);
  auto* svsTrack=new SVSTrack(song);
  const auto voices=svs::Registry::instance().voices();QVERIFY(!voices.isEmpty());
  svsTrack->bindVoice(voices.first().pluginId,voices.first().id);
  auto* svsClip=static_cast<SVSClip*>(svsTrack->createClip(0));
  svs::Note svsNote;svsNote.id="ui-baseline-note";svsNote.duration=96;svsNote.pitch=60;
  svsClip->setNotes({svsNote});
  Engine::mixer()->createChannel();Engine::mixer()->createChannel();
  auto* chain=&Engine::mixer()->mixerChannel(1)->m_fxChain;
  auto* effect=Effect::instantiate("amplifier",chain,nullptr);QVERIFY(effect);chain->appendEffect(effect);
  song->addController(Controller::create(Controller::ControllerType::Lfo,song));
  const auto fixture=qEnvironmentVariable("LMMS_UI_FIXTURE");
  QVERIFY(!fixture.isEmpty());
  const auto temporaryFixture=m_config.filePath(QFileInfo(fixture).fileName());
  QVERIFY(song->saveProjectFile(temporaryFixture,true));
  const auto bundled=m_config.filePath(QFileInfo(fixture).completeBaseName()+'/'+QFileInfo(fixture).fileName());
  QVERIFY(QFile::exists(bundled));
  const auto destination=QFileInfo(fixture).absolutePath()+'/'+QFileInfo(fixture).completeBaseName();
  if(!QFile::exists(destination+'/'+QFileInfo(fixture).fileName()))
  {
   QVERIFY(QDir().mkpath(destination+"/resources"));
   QVERIFY(QFile::copy(bundled,destination+'/'+QFileInfo(fixture).fileName()));
   QVERIFY(QFile::copy(QFileInfo(bundled).absolutePath()+"/resources/tone.wav",destination+"/resources/tone.wav"));
  }
  const auto report=m_config.filePath("reopen-results.txt");
  auto environment=QProcessEnvironment::systemEnvironment();environment.insert("LMMS_UI_REOPEN",bundled);
  environment.insert("LMMS_UI_TRACK_COUNT",QString::number(song->tracks().size()));
  QProcess reopen;reopen.setProcessEnvironment(environment);
  reopen.start(QCoreApplication::applicationFilePath(),{"reopenFixture","-o",report+",txt","-o","-,txt"});
  QVERIFY(reopen.waitForStarted(5000));QVERIFY(reopen.waitForFinished(30000));
  QFile result(report);QVERIFY(result.open(QIODevice::ReadOnly));
  const auto output=result.readAll()+reopen.readAllStandardError();
  QVERIFY2(reopen.exitStatus()==QProcess::NormalExit&&reopen.exitCode()==0,output.constData());
  QFile archived(m_output+"/fixture-reopen-results.txt");QVERIFY(archived.open(QIODevice::WriteOnly));archived.write(output);
  capture(m_gui->mainWindow(),"S01-main");
  const QList<QPair<SetupDialog::ConfigTab,QString>> pages{
   {SetupDialog::ConfigTab::GeneralSettings,"general"}, {SetupDialog::ConfigTab::AudioSettings,"audio"},
   {SetupDialog::ConfigTab::PathsSettings,"paths"}, {SetupDialog::ConfigTab::VstSettings,"vst"},
   {SetupDialog::ConfigTab::SvsSettings,"svs"}};
  for(const auto& page:pages) {SetupDialog settings(page.first);capture(&settings,"S02-"+page.second);settings.close();}
  {ExportProjectDialog dialog(m_config.filePath("export.wav"),ExportProjectDialog::Mode::ExportProject);
   capture(&dialog,"S02-export");dialog.close();}
  showEditor(m_gui->songEditor(),"S03-song");showEditor(m_gui->patternEditor(),"S03-pattern");
  midiTrack=nullptr; midi=nullptr;automation=nullptr;svsClip=nullptr;
  for(auto* track:song->tracks())
  {
   if(auto* instrument=dynamic_cast<InstrumentTrack*>(track)) {midiTrack=instrument;midi=static_cast<MidiClip*>(track->getClip(0));}
   if(dynamic_cast<AutomationTrack*>(track)) automation=static_cast<AutomationClip*>(track->getClip(0));
   if(dynamic_cast<SVSTrack*>(track)) svsClip=static_cast<SVSClip*>(track->getClip(0));
  }
  QVERIFY(midi);QVERIFY(midi->notes().size()>=8);
  m_gui->pianoRoll()->setCurrentMidiClip(midi);showEditor(m_gui->pianoRoll(),"S04-piano");
  QVERIFY(automation);m_gui->automationEditor()->setCurrentClip(automation);showEditor(m_gui->automationEditor(),"S04-automation");
  QVERIFY(svsClip);auto* svsEditor=new SVSPianoRoll(svsClip);svsEditor->openIn(m_gui->mainWindow());
  showEditor(svsEditor,"S05-svs");
  {std::unique_ptr<QDialog> settings(createSVSPluginSettings(static_cast<SVSTrack*>(svsClip->getTrack())));
   capture(settings.get(),"S05-plugin");settings->close();}
  showEditor(m_gui->mixerView(),"S06-mixer");showEditor(m_gui->getControllerRackView(),"S06-controller");
  auto* effectWindow=Engine::mixer()->mixerChannel(1)->m_fxChain.effectAt(0)->controls()->createView();
  QVERIFY(effectWindow);showEditor(effectWindow,"S06-effect");
  auto* frame=qobject_cast<SubWindow*>(svsEditor->parentWidget());QVERIFY(frame);
  frame->show();capture(m_gui->mainWindow(),"S07-mdi");frame->hide();
  auto* view=m_gui->mainWindow()->findChild<InstrumentTrackView*>();QVERIFY(view);
  auto* instrumentWindow=view->getInstrumentTrackWindow();showEditor(instrumentWindow,"S08-instrument");
  song->setModified(false);
 }
 void reopenFixture()
 {
  const auto path=qEnvironmentVariable("LMMS_UI_REOPEN");
  if(path.isEmpty()) QSKIP("Round trip runs in a fresh process, as application startup does.");
  QVERIFY(QFile::exists(path));auto* song=Engine::getSong();song->loadProject(path);
  QVERIFY2(!song->hasErrors(),qPrintable(song->errorSummary()));
  QCOMPARE(int(song->tracks().size()),qEnvironmentVariableIntValue("LMMS_UI_TRACK_COUNT"));
  bool midi=false,sample=false,automation=false,svs=false;
  for(auto* track:song->tracks())
  {
   for(auto* clip:track->getClips())
   {
    if(auto* notes=dynamic_cast<MidiClip*>(clip)) midi|=notes->notes().size()>=8;
    if(auto* audio=dynamic_cast<SampleClip*>(clip)) sample|=QFile::exists(audio->sampleFile());
    if(auto* curve=dynamic_cast<AutomationClip*>(clip)) automation|=!curve->objects().empty();
    if(auto* voice=dynamic_cast<SVSClip*>(clip)) svs|=!voice->notes().isEmpty();
   }
  }
  QVERIFY(midi);QVERIFY(sample);QVERIFY(automation);QVERIFY(svs);
  QVERIFY(Engine::mixer()->numChannels()>=3);QVERIFY(!song->controllers().empty());
  QVERIFY(Engine::mixer()->mixerChannel(1)->m_fxChain.effectAt(0));
  capture(m_gui->mainWindow(),"fixture-reopen");song->setModified(false);
 }
 void pluginPanels()
 {
  if(!qEnvironmentVariableIsSet("LMMS_UI_PLUGIN_BASELINES"))
   QSKIP("Full plugin baseline is requested after the frozen targets are built.");
  Engine::getSong()->createNewProject();
  if(Engine::mixer()->numChannels()<2) Engine::mixer()->createChannel();
  QJsonArray coverage;
  for(const auto& info:PluginFactory::instance()->pluginInfos())
  {
   const auto* descriptor=info.descriptor;
   if(!descriptor || (descriptor->type!=Plugin::Type::Instrument && descriptor->type!=Plugin::Type::Effect
     && descriptor->type!=Plugin::Type::Tool)) continue;
   const auto name=QString::fromUtf8(descriptor->name);
   if(name.startsWith("carla"))
   {
    coverage.append(QJsonObject{{"plugin",name},{"status","MANUAL/PENDING"},
     {"reason","External Carla engine requires a working JACK/runtime installation; native initialization crashes in this environment"}});
    continue;
   }
   qInfo().noquote()<<"Capturing plugin"<<name;
   Plugin::Descriptor::SubPluginFeatures::KeyList keys;
   if(descriptor->subPluginFeatures) descriptor->subPluginFeatures->listSubPluginKeys(descriptor,keys);
   if(descriptor->subPluginFeatures && keys.empty())
   {
    coverage.append(QJsonObject{{"plugin",name},{"status","MANUAL/PENDING"},{"reason","No external subplugin fixture installed"}});
    continue;
   }
   auto* key=keys.empty()?nullptr:&keys.first();
   if(descriptor->type==Plugin::Type::Instrument)
   {
    auto* track=new InstrumentTrack(Engine::getSong());
    QVERIFY2(track->loadInstrument(name,key),qPrintable(name));
    QCoreApplication::processEvents();
    InstrumentTrackView* view=nullptr;
    for(auto* candidate:m_gui->mainWindow()->findChildren<InstrumentTrackView*>())
     if(candidate->model()==track) {view=candidate;break;}
    QVERIFY2(view,qPrintable(name));
    showEditor(view->getInstrumentTrackWindow(),"S08-plugin-"+name);
   }
   else if(descriptor->type==Plugin::Type::Effect)
   {
    auto* chain=&Engine::mixer()->mixerChannel(1)->m_fxChain;
    auto* effect=Effect::instantiate(name,chain,key);QVERIFY2(effect,qPrintable(name));
    chain->appendEffect(effect);auto* panel=effect->controls()->createView();QVERIFY(panel);
    showEditor(panel,"S08-plugin-"+name);
   }
   else
   {
    auto* plugin=Plugin::instantiate(name,Engine::getSong(),nullptr);QVERIFY(plugin);
    m_toolPlugins.append(plugin);
    auto* panel=plugin->createView(m_gui->mainWindow());QVERIFY(panel);
    capture(panel,"S08-plugin-"+name);panel->hide();
   }
   coverage.append(QJsonObject{{"plugin",name},{"status","BASELINE CAPTURED"}});
   QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QTest::qWait(100);
  }
  QFile report(m_output+"/plugin-panels.json");QVERIFY(report.open(QIODevice::WriteOnly));
  report.write(QJsonDocument(coverage).toJson());Engine::getSong()->setModified(false);
 }
 void cleanupTestCase()
 {
  if(m_gui) {delete static_cast<QWidget*>(m_gui->mainWindow());qDeleteAll(m_toolPlugins);m_toolPlugins.clear();m_gui.reset();}
 }
};
QTEST_MAIN(UiBaselineCapture)
#include "UiBaselineCapture.moc"
