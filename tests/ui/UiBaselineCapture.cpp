#include <QCheckBox>
#include <QComboBox>
#include <QDomDocument>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMdiArea>
#include <QMenuBar>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTranslator>
#include <QtTest>

#include "AudioDummy.h"
#include "AutomationClip.h"
#include "AutomationEditor.h"
#include "AutomationTrack.h"
#include "ConfigManager.h"
#include "Controller.h"
#include "ControllerRackView.h"
#include "DummyEffect.h"
#include "Effect.h"
#include "EffectControlDialog.h"
#include "EffectControls.h"
#include "ExportProjectDialog.h"
#include "GuiApplication.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "LedCheckBox.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "MixerView.h"
#include "NativePluginTranslation.h"
#include "PatternEditor.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "PianoRoll.h"
#include "PluginFactory.h"
#include "PluginView.h"
#include "SVSCanvas.h"
#include "SVSClip.h"
#include "SVSParameterPanel.h"
#include "SVSProjectImportDialog.h"
#include "SVSSettingsPage.h"
#include "SVSTrack.h"
#include "SVSViews.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "SetupDialog.h"
#include "Song.h"
#include "SongEditor.h"
#include "SubWindow.h"
#include "TabWidget.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

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
		widget->show();
		widget->raise();
		widget->activateWindow();
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
		if (frame)
		{
			frame->show();
			frame->setGeometry(
				QRect(QPoint(0, 0), QSize(1000, 600).boundedTo(m_gui->mainWindow()->workspace()->viewport()->size())));
			frame->raise();
		}
		QVERIFY(widget->isVisible());
		QVERIFY(!widget->visibleRegion().isEmpty());
		capture(widget->isWindow() ? widget : static_cast<QWidget*>(m_gui->mainWindow()), name);
		if (frame)
			frame->hide();
		else if (widget->isWindow())
			widget->hide();
	}
private slots:
	void initTestCase()
	{
		QCOMPARE(QGuiApplication::platformName(), QString("windows"));
		QVERIFY(m_config.isValid());
		m_output = qEnvironmentVariable("LMMS_UI_EVIDENCE");
		QVERIFY(!m_output.isEmpty());
		QVERIFY(QDir().mkpath(m_output));
		auto* config = ConfigManager::inst();
		config->loadConfigFile(m_config.filePath("ui-config.xml"));
		config->setWorkingDir(m_config.path() + '/');
		const auto stkDir = qEnvironmentVariable("LMMS_UI_STK_DIR");
		if (!stkDir.isEmpty())
		{
			QVERIFY(QFile::exists(stkDir + "/sinewave.raw"));
			config->setSTKDir(stkDir);
		}
		config->setValue("app", "configured", "1");
		config->setValue("audioengine", "audiodev", AudioDummy::name());
		m_gui = std::make_unique<GuiApplication>();
		auto* window = m_gui->mainWindow();
		const auto available = window->screen()->availableGeometry();
		window->resize(QSize(1280, 800).boundedTo(available.size() - QSize(20, 40)));
		window->move(available.topLeft());
		window->show();
		QVERIFY(QTest::qWaitForWindowExposed(window));
		QJsonObject environment{{"qt", qVersion()}, {"platform", QGuiApplication::platformName()},
			{"scale", window->devicePixelRatioF()}, {"font", window->font().toString()}, {"theme", "default"},
			{"windowWidth", window->width()}, {"windowHeight", window->height()}, {"language", QLocale().name()},
			{"simulatedScale", qEnvironmentVariable("QT_SCALE_FACTOR")}};
		QFile metadata(m_output + "/environment.json");
		QVERIFY(metadata.open(QIODevice::WriteOnly));
		metadata.write(QJsonDocument(environment).toJson());
	}
	void translationEntryPoints()
	{
		QCOMPARE(QGuiApplication::platformName(), QString("windows"));
		QCOMPARE(m_gui->mainWindow()->devicePixelRatioF(), 1.0);
		const QStringList languages{"zh_CN", "ja", "en", "ko"};
		for (const auto& language : languages)
		{
			QTranslator translator;
			QVERIFY(translator.load(QString("%1/locale/%2.qm").arg(qEnvironmentVariable("LMMS_DATA_DIR"), language)));
			QCoreApplication::installTranslator(&translator);
			const auto gain = QCoreApplication::translate("NativeSVS", "Gain");
			const auto mixed = QCoreApplication::translate("lmms::gui::SVSParameterPanel", "Mixed");
			if (language != "en")
			{
				QVERIFY(gain != "Gain");
				QVERIFY(mixed != "Mixed");
			}
			svs::Parameter parameter;
			parameter.id = "example.gain";
			parameter.name = "Gain";
			parameter.scope = "track";
			parameter.type = "float";
			parameter.minimum = 0;
			parameter.maximum = 2;
			parameter.defaultValue = 1;
			const auto original = QVector<svs::Parameter>{parameter};
			const auto display = nativeTranslation::svsParameters("org.lmms.svs.example", original);
			QCOMPARE(display.first().id, parameter.id);
			QCOMPARE(original.first().name, QString("Gain"));
			QCOMPARE(display.first().name, gain);
			QCOMPARE(nativeTranslation::svsParameters("third.party", original).first().name, QString("Gain"));
			QCOMPARE(nativeTranslation::svsText("org.lmms.svs.diffsinger", "Custom singer"), QString("Custom singer"));
			QCOMPARE(nativeTranslation::rvcText("third.party", "pitch_shift"), QString("pitch_shift"));
			const auto pitchShift = nativeTranslation::rvcText("RVC", "pitch_shift");
			QCOMPARE(pitchShift, QCoreApplication::translate("NativeRVC", "Pitch shift"));
			QVERIFY(pitchShift != "pitch_shift");
			QWidget window;
			window.setWindowTitle("LMMS translation entries — " + language);
			auto* layout = new QVBoxLayout(&window);
			auto* panel = new SVSParameterPanel(&window);
			layout->addWidget(panel);
			panel->refresh(display, "track", {{{parameter.id, 1}}, {{parameter.id, 2}}}, {}, {});
			QCOMPARE(panel->findChild<QLabel*>("parameterLabel")->text(), gain);
			QCOMPARE(panel->findChild<QDoubleSpinBox*>()->toolTip(), mixed);
			layout->addWidget(new QLabel(pitchShift, &window));
			layout->addWidget(new QLabel(QCoreApplication::translate("SVSProjectUI", "Import SVS project"), &window));
			window.resize(560, 220);
			capture(&window, "M1-entries-" + language);
			QCOMPARE(window.devicePixelRatioF(), 1.0);
			window.close();
			ExportProjectDialog exportDialog(
				m_config.filePath("translation.wav"), ExportProjectDialog::Mode::ExportProject);
			QCOMPARE(exportDialog.windowTitle(),
				QCoreApplication::translate("lmms::gui::ExportProjectDialog", "Export project"));
			bool foundWaveFormat = false;
			for (const auto* combo : exportDialog.findChildren<QComboBox*>())
			{
				foundWaveFormat |= combo->findText(QCoreApplication::translate("ProjectRenderer", "WAV (*.wav)")) >= 0;
			}
			QVERIFY(foundWaveFormat);
			capture(&exportDialog, "M1-export-" + language);
			exportDialog.close();
			QCoreApplication::removeTranslator(&translator);
		}
	}
	void svsTranslations()
	{
		QCOMPARE(m_gui->mainWindow()->devicePixelRatioF(), 1.0);
		for (const auto& language : QStringList{"zh_CN", "ja", "en", "ko"})
		{
			QTranslator translator;
			QVERIFY(translator.load(QString("%1/locale/%2.qm").arg(qEnvironmentVariable("LMMS_DATA_DIR"), language)));
			QCoreApplication::installTranslator(&translator);
			SVSProjectImportDialog importDialog(QJsonObject{{"id", "test"}, {"name", "Test format"}});
			QCOMPARE(importDialog.windowTitle(), QCoreApplication::translate("SVSProjectUI", "Import SVS project"));
			if (language != "en") { QVERIFY(importDialog.windowTitle() != "Import SVS project"); }
			capture(&importDialog, "M2-import-" + language);
			importDialog.close();
			SVSSettingsPage settings;
			settings.resize(1000, 720);
			const auto memoryPolicy = settings.findChild<QComboBox*>("svsComputeMemoryPolicy");
			QVERIFY(memoryPolicy);
			QCOMPARE(memoryPolicy->itemText(0),
				QCoreApplication::translate("lmms::gui::SVSSettingsPage", "Release immediately after rendering"));
			QVERIFY(!memoryPolicy->toolTip().isEmpty());
			capture(&settings, "M2-settings-" + language);
			settings.close();
			auto* track = new SVSTrack(Engine::getSong());
			auto* clip = static_cast<SVSClip*>(track->createClip(0));
			svs::Note note;
			note.id = "translation-note";
			note.duration = 96;
			note.pitch = 60;
			note.lyric = "你好";
			clip->setNotes({note});
			{
				SVSPianoRoll editor(clip);
				editor.resize(1100, 660);
				capture(&editor, "M2-editor-" + language);
				editor.close();
			}
			delete track;
			QCoreApplication::removeTranslator(&translator);
		}
	}
	void scenes()
	{
		auto* song = Engine::getSong();
		song->createNewProject();
		auto* midiTrack = new InstrumentTrack(song);
		midiTrack->setName("MIDI / 长名称乐器轨道");
		QVERIFY(midiTrack->loadInstrument("tripleoscillator"));
		midiTrack->setName("MIDI / 长名称乐器轨道");
		auto* midi = static_cast<MidiClip*>(midiTrack->createClip(0));
		for (int i = 0; i < 8; ++i)
			midi->addNote(Note(TimePos(i == 0 ? 3 : 24), TimePos(i * 24), 60 + i % 5));
		auto* pattern = new PatternTrack(song);
		pattern->createClip(192);
		auto* beatTrack = new InstrumentTrack(Engine::patternStore());
		QVERIFY(beatTrack->loadInstrument("kicker"));
		auto* beat = static_cast<MidiClip*>(beatTrack->createClip(0));
		beat->setSteps(16);
		for (int i = 0; i < 16; i += 4)
			beat->setStep(i, true);
		auto* sampleTrack = new SampleTrack(song);
		auto* sample = static_cast<SampleClip*>(sampleTrack->createClip(0));
		const auto wav = qEnvironmentVariable("LMMS_UI_SAMPLE");
		QVERIFY(QFile::exists(wav));
		sample->setSampleFile(wav);
		auto* automationTrack = new AutomationTrack(song);
		auto* automation = static_cast<AutomationClip*>(automationTrack->createClip(0));
		automation->addObject(&song->tempoModel());
		automation->putValue(0, 120);
		automation->putValue(96, 140);
		auto* svsTrack = new SVSTrack(song);
		svs::Registry::instance().voices();
		QTRY_VERIFY_WITH_TIMEOUT(!svs::Registry::instance().scanning(), 30000);
		const auto voices = svs::Registry::instance().voices();
		QVERIFY(!voices.isEmpty());
		svsTrack->bindVoice(voices.first().pluginId, voices.first().id);
		auto* svsClip = static_cast<SVSClip*>(svsTrack->createClip(0));
		svs::Note svsNote;
		svsNote.id = "ui-baseline-note";
		svsNote.duration = 96;
		svsNote.pitch = 60;
		svsClip->setNotes({svsNote});
		Engine::mixer()->createChannel();
		Engine::mixer()->createChannel();
		auto* chain = &Engine::mixer()->mixerChannel(1)->m_fxChain;
		auto* effect = Effect::instantiate("amplifier", chain, nullptr);
		QVERIFY(effect);
		chain->appendEffect(effect);
		song->addController(Controller::create(Controller::ControllerType::Lfo, song));
		const auto fixture = qEnvironmentVariable("LMMS_UI_FIXTURE");
		QVERIFY(!fixture.isEmpty());
		const auto temporaryFixture = m_config.filePath(QFileInfo(fixture).fileName());
		QVERIFY(song->saveProjectFile(temporaryFixture, true));
		const auto bundled
			= m_config.filePath(QFileInfo(fixture).completeBaseName() + '/' + QFileInfo(fixture).fileName());
		QVERIFY(QFile::exists(bundled));
		const auto destination = QFileInfo(fixture).absolutePath() + '/' + QFileInfo(fixture).completeBaseName();
		if (!QFile::exists(destination + '/' + QFileInfo(fixture).fileName()))
		{
			QVERIFY(QDir().mkpath(destination + "/resources"));
			QVERIFY(QFile::copy(bundled, destination + '/' + QFileInfo(fixture).fileName()));
			QVERIFY(QFile::copy(
				QFileInfo(bundled).absolutePath() + "/resources/tone.wav", destination + "/resources/tone.wav"));
		}
		const auto report = m_config.filePath("reopen-results.txt");
		auto environment = QProcessEnvironment::systemEnvironment();
		environment.insert("LMMS_UI_REOPEN", bundled);
		environment.insert("LMMS_UI_TRACK_COUNT", QString::number(song->tracks().size()));
		QProcess reopen;
		reopen.setProcessEnvironment(environment);
		reopen.start(QCoreApplication::applicationFilePath(), {"reopenFixture", "-o", report + ",txt", "-o", "-,txt"});
		QVERIFY(reopen.waitForStarted(5000));
		QVERIFY(reopen.waitForFinished(30000));
		QFile result(report);
		QVERIFY(result.open(QIODevice::ReadOnly));
		const auto output = result.readAll() + reopen.readAllStandardError();
		QVERIFY2(reopen.exitStatus() == QProcess::NormalExit && reopen.exitCode() == 0, output.constData());
		QFile archived(m_output + "/fixture-reopen-results.txt");
		QVERIFY(archived.open(QIODevice::WriteOnly));
		archived.write(output);
		capture(m_gui->mainWindow(), "S01-main");
		const QList<QPair<SetupDialog::ConfigTab, QString>> pages{{SetupDialog::ConfigTab::GeneralSettings, "general"},
			{SetupDialog::ConfigTab::AudioSettings, "audio"}, {SetupDialog::ConfigTab::PathsSettings, "paths"},
			{SetupDialog::ConfigTab::VstSettings, "vst"}, {SetupDialog::ConfigTab::SvsSettings, "svs"}};
		for (const auto& page : pages)
		{
			SetupDialog settings(page.first);
			capture(&settings, "S02-" + page.second);
			settings.close();
		}
		{
			ExportProjectDialog dialog(m_config.filePath("export.wav"), ExportProjectDialog::Mode::ExportProject);
			capture(&dialog, "S02-export");
			dialog.close();
		}
		showEditor(m_gui->songEditor(), "S03-song");
		showEditor(m_gui->patternEditor(), "S03-pattern");
		midiTrack = nullptr;
		midi = nullptr;
		automation = nullptr;
		svsClip = nullptr;
		for (auto* track : song->tracks())
		{
			if (auto* instrument = dynamic_cast<InstrumentTrack*>(track))
			{
				midiTrack = instrument;
				midi = static_cast<MidiClip*>(track->getClip(0));
			}
			if (dynamic_cast<AutomationTrack*>(track))
				automation = static_cast<AutomationClip*>(track->getClip(0));
			if (dynamic_cast<SVSTrack*>(track))
				svsClip = static_cast<SVSClip*>(track->getClip(0));
		}
		QVERIFY(midi);
		QVERIFY(midi->notes().size() >= 8);
		m_gui->pianoRoll()->setCurrentMidiClip(midi);
		showEditor(m_gui->pianoRoll(), "S04-piano");
		QVERIFY(automation);
		m_gui->automationEditor()->setCurrentClip(automation);
		showEditor(m_gui->automationEditor(), "S04-automation");
		QVERIFY(svsClip);
		auto* svsEditor = new SVSPianoRoll(svsClip);
		svsEditor->openIn(m_gui->mainWindow());
		SVSCanvas* svsCanvas = nullptr;
		for (auto* canvas : svsEditor->findChildren<SVSCanvas*>())
			if (!canvas->isParameterLane())
			{
				svsCanvas = canvas;
				break;
			}
		QVERIFY(svsCanvas);
		svsCanvas->setScroll(0, 67);
		const auto svsNoteRect = svsCanvas->noteRect(svsClip->notes().first());
		QCOMPARE(svsCanvas->tickAt(svsNoteRect.center().x()),
			svsClip->notes().first().tick + svsClip->notes().first().duration / 2);
		QCOMPARE(svsCanvas->pitchAt(svsNoteRect.center().y()), svsClip->notes().first().pitch - .5);
		showEditor(svsEditor, "S05-svs");
		{
			std::unique_ptr<QDialog> settings(createSVSPluginSettings(static_cast<SVSTrack*>(svsClip->getTrack())));
			capture(settings.get(), "S05-plugin");
			settings->close();
		}
		showEditor(m_gui->mixerView(), "S06-mixer");
		showEditor(m_gui->getControllerRackView(), "S06-controller");
		auto* effectWindow = Engine::mixer()->mixerChannel(1)->m_fxChain.effectAt(0)->controls()->createView();
		QVERIFY(effectWindow);
		showEditor(effectWindow, "S06-effect");
		auto* frame = qobject_cast<SubWindow*>(svsEditor->parentWidget());
		QVERIFY(frame);
		frame->show();
		capture(m_gui->mainWindow(), "S07-mdi");
		frame->hide();
		auto* view = m_gui->mainWindow()->findChild<InstrumentTrackView*>();
		QVERIFY(view);
		auto* instrumentWindow = view->getInstrumentTrackWindow();
		showEditor(instrumentWindow, "S08-instrument");
		auto* instrumentTabs = instrumentWindow->findChild<TabWidget*>();
		QVERIFY(instrumentTabs);
		for (int tab = 1; tab <= 5; ++tab)
		{
			instrumentTabs->setActiveTab(tab);
			QCOMPARE(instrumentTabs->activeTab(), tab);
			showEditor(instrumentWindow, QString("S08-common-%1").arg(tab));
			for (auto* label : instrumentWindow->findChildren<LedCheckBox*>())
			{
				if (!label->isVisibleTo(instrumentWindow))
					continue;
				QVERIFY2(label->width() >= label->sizeHint().width(), qPrintable(label->text()));
				QVERIFY2(label->parentWidget()->rect().contains(label->geometry()), qPrintable(label->text()));
			}
		}
		instrumentTabs->setActiveTab(0);
		song->setModified(false);
	}
	void reopenFixture()
	{
		const auto path = qEnvironmentVariable("LMMS_UI_REOPEN");
		if (path.isEmpty())
			QSKIP("Round trip runs in a fresh process, as application startup does.");
		QVERIFY(QFile::exists(path));
		auto* song = Engine::getSong();
		song->loadProject(path);
		QVERIFY2(!song->hasErrors(), qPrintable(song->errorSummary()));
		QCOMPARE(int(song->tracks().size()), qEnvironmentVariableIntValue("LMMS_UI_TRACK_COUNT"));
		bool midi = false, sample = false, automation = false, svs = false;
		for (auto* track : song->tracks())
		{
			for (auto* clip : track->getClips())
			{
				if (auto* notes = dynamic_cast<MidiClip*>(clip))
					midi |= notes->notes().size() >= 8;
				if (auto* audio = dynamic_cast<SampleClip*>(clip))
					sample |= QFile::exists(audio->sampleFile());
				if (auto* curve = dynamic_cast<AutomationClip*>(clip))
					automation |= !curve->objects().empty();
				if (auto* voice = dynamic_cast<SVSClip*>(clip))
					svs |= !voice->notes().isEmpty();
			}
		}
		QVERIFY(midi);
		QVERIFY(sample);
		QVERIFY(automation);
		QVERIFY(svs);
		QVERIFY(Engine::mixer()->numChannels() >= 3);
		QVERIFY(!song->controllers().empty());
		QVERIFY(Engine::mixer()->mixerChannel(1)->m_fxChain.effectAt(0));
		capture(m_gui->mainWindow(), "fixture-reopen");
		song->setModified(false);
	}
	void pluginPanels()
	{
		if (!qEnvironmentVariableIsSet("LMMS_UI_PLUGIN_BASELINES"))
			QSKIP("Full plugin baseline is requested after the frozen targets are built.");
		Engine::getSong()->createNewProject();
		if (Engine::mixer()->numChannels() < 2)
			Engine::mixer()->createChannel();
		QJsonArray coverage;
		auto settings = [](Plugin* plugin) {
			QDomDocument doc;
			auto root = doc.createElement("preset");
			doc.appendChild(root);
			plugin->saveState(doc, root);
			return doc;
		};
		auto checkPreset = [&](Plugin* plugin, const QDomDocument& before) {
			QCOMPARE(settings(plugin).toString(), before.toString());
			plugin->restoreState(before.documentElement().firstChildElement());
			auto actual = settings(plugin);
			auto expected = before.cloneNode(true).toDocument();
			// Preset loading deliberately regenerates this controller's runtime ID.
			if (QString::fromUtf8(plugin->descriptor()->name) == "peakcontrollereffect")
			{
				for (auto* doc : {&actual, &expected})
					doc->elementsByTagName("peakcontrollereffectcontrols")
						.at(0)
						.toElement()
						.removeAttribute("effectId");
			}
			QCOMPARE(actual.toString(), expected.toString());
		};
		for (const auto& info : PluginFactory::instance()->pluginInfos())
		{
			const auto* descriptor = info.descriptor;
			if (!descriptor
				|| (descriptor->type != Plugin::Type::Instrument && descriptor->type != Plugin::Type::Effect
					&& descriptor->type != Plugin::Type::Tool))
				continue;
			const auto name = QString::fromUtf8(descriptor->name);
			if (name.startsWith("carla"))
			{
				coverage.append(QJsonObject{{"plugin", name}, {"status", "MANUAL/PENDING"},
					{"reason",
						"External Carla engine requires a working JACK/runtime installation; native initialization crashes in this environment"}});
				continue;
			}
			qInfo().noquote() << "Capturing plugin" << name;
			Plugin::Descriptor::SubPluginFeatures::KeyList keys;
			// VeSTige has a useful LMMS host panel even without a foreign plugin.
			if (descriptor->subPluginFeatures && name != "vestige")
				descriptor->subPluginFeatures->listSubPluginKeys(descriptor, keys);
			if (descriptor->subPluginFeatures && keys.empty() && name != "vestige")
			{
				coverage.append(QJsonObject{{"plugin", name}, {"status", "MANUAL/PENDING"},
					{"reason", "No external subplugin fixture installed"}});
				continue;
			}
			auto* key = keys.empty() ? nullptr : &keys.first();
			if (descriptor->type == Plugin::Type::Instrument)
			{
				auto* track = new InstrumentTrack(Engine::getSong());
				QVERIFY2(track->loadInstrument(name, key), qPrintable(name));
				QCoreApplication::processEvents();
				InstrumentTrackView* view = nullptr;
				for (auto* candidate : m_gui->mainWindow()->findChildren<InstrumentTrackView*>())
					if (candidate->model() == track)
					{
						view = candidate;
						break;
					}
				QVERIFY2(view, qPrintable(name));
				const auto preset = settings(track->instrument());
				// Keep the default host geometry; forcing an MDI size masks fixed-panel regressions.
				auto* window = view->getInstrumentTrackWindow();
				window->toggleVisibility(true);
				window->parentWidget()->move(0, 0);
				QVERIFY(window->isVisible());
				QVERIFY(!window->visibleRegion().isEmpty());
				capture(m_gui->mainWindow(), "S08-plugin-" + name);
				window->toggleVisibility(false);
				checkPreset(track->instrument(), preset);
			}
			else if (descriptor->type == Plugin::Type::Effect)
			{
				auto* chain = &Engine::mixer()->mixerChannel(1)->m_fxChain;
				auto* effect = Effect::instantiate(name, chain, key);
				QVERIFY2(effect, qPrintable(name));
				if (dynamic_cast<DummyEffect*>(effect) || !effect->isOkay()
					|| (name == "vsteffect" && effect->controls()->controlCount() == 0))
				{
					coverage.append(QJsonObject{{"plugin", name}, {"status", "MANUAL/PENDING"},
						{"reason",
							"External effect initialization returned an invalid/dummy effect or an unloaded VST wrapper; placeholder is not panel coverage"}});
					delete effect;
					continue;
				}
				chain->appendEffect(effect);
				auto* panel = effect->controls()->createView();
				QVERIFY(panel);
				const auto preset = settings(effect);
				showEditor(panel, "S08-plugin-" + name);
				checkPreset(effect, preset);
			}
			else
			{
				auto* plugin = Plugin::instantiate(name, Engine::getSong(), nullptr);
				QVERIFY(plugin);
				m_toolPlugins.append(plugin);
				auto* panel = plugin->createView(m_gui->mainWindow());
				QVERIFY(panel);
				showEditor(panel, "S08-plugin-" + name);
			}
			coverage.append(QJsonObject{{"plugin", name}, {"status", "BASELINE CAPTURED"},
				{"scope",
					name == "vestige" ? "LMMS unloaded host wrapper; foreign editor and sound MANUAL/PENDING"
									  : "LMMS host/shared controls; artwork exceptions in inventory"},
				{"preset",
					descriptor->type == Plugin::Type::Tool ? "N/A: tool panel"
														   : "UI snapshot and restore/save checked"}});
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			QTest::qWait(100);
		}
		QFile report(m_output + "/plugin-panels.json");
		QVERIFY(report.open(QIODevice::WriteOnly));
		report.write(QJsonDocument(coverage).toJson());
		Engine::getSong()->setModified(false);
	}
	void standardControls()
	{
		QFile theme(qEnvironmentVariable("LMMS_DATA_DIR") + "/themes/default/style.css");
		QVERIFY(theme.open(QIODevice::ReadOnly));
		const auto stylesheet = QString::fromUtf8(theme.readAll());
		auto matches = QRegularExpression("resources:(ui-[^\"]+\\.svg)").globalMatch(stylesheet);
		QStringList resources;
		while (matches.hasNext())
		{
			const auto name = matches.next().captured(1);
			QVERIFY2(!QPixmap("resources:" + name).isNull(), qPrintable(name));
			if (!resources.contains(name))
				resources.append(name);
		}
		QFile report(m_output + "/standard-resources.json");
		QVERIFY(report.open(QIODevice::WriteOnly));
		report.write(QJsonDocument(QJsonArray::fromStringList(resources)).toJson());
		QWidget window;
		window.setWindowTitle("LMMS standard theme states");
		auto* layout = new QGridLayout(&window);
		layout->setContentsMargins(12, 12, 12, 12);
		layout->setSpacing(8);
		const QStringList states{"Normal / 中文", "Checked", "Disabled", "Read only"};
		for (int column = 0; column < 4; ++column)
		{
			layout->addWidget(new QLabel(states[column]), 0, column);
			auto* button = new QPushButton(states[column]);
			button->setCheckable(true);
			button->setChecked(column == 1);
			button->setEnabled(column != 2);
			layout->addWidget(button, 1, column);
			auto* check = new QCheckBox("Enable / 启用");
			check->setTristate(true);
			check->setCheckState(column == 0 ? Qt::Unchecked : column == 1 ? Qt::Checked : Qt::PartiallyChecked);
			check->setEnabled(column != 2);
			layout->addWidget(check, 2, column);
			auto* radio = new QRadioButton("Choice");
			radio->setChecked(column == 1);
			radio->setEnabled(column != 2);
			layout->addWidget(radio, 3, column);
			auto* input = new QLineEdit("长名称 / text");
			input->setEnabled(column != 2);
			input->setReadOnly(column == 3);
			layout->addWidget(input, 4, column);
			auto* combo = new QComboBox;
			combo->addItems({"Long device / 长设备名称", "Second"});
			combo->setEditable(column == 1);
			combo->setEnabled(column != 2);
			layout->addWidget(combo, 5, column);
			auto* spin = new QSpinBox;
			spin->setRange(-100, 100);
			spin->setValue(42);
			spin->setEnabled(column != 2);
			layout->addWidget(spin, 6, column);
			auto* slider = new QSlider(Qt::Horizontal);
			slider->setValue(column * 33);
			slider->setEnabled(column != 2);
			layout->addWidget(slider, 7, column);
			auto* progress = new QProgressBar;
			progress->setValue(column == 3 ? 100 : column * 33);
			layout->addWidget(progress, 8, column);
		}
		window.resize(880, 440);
		window.show();
		QVERIFY(QTest::qWaitForWindowExposed(&window));
		auto* check = window.findChild<QCheckBox*>();
		QVERIFY(check);
		check->setFocus();
		QTest::keyClick(check, Qt::Key_Space);
		QVERIFY(check->checkState() != Qt::Unchecked);
		check->setCheckState(Qt::Unchecked);
		auto* input = window.findChild<QLineEdit*>();
		QVERIFY(input);
		input->setFocus();
		input->selectAll();
		const auto before = input->geometry();
		capture(&window, "standard-states-focus");
		QTest::keyClicks(input, "typed");
		QCOMPARE(input->text(), QString("typed"));
		QCOMPARE(input->geometry(), before);
		auto* button = window.findChild<QPushButton*>();
		QVERIFY(button);
		QTest::mouseMove(button, button->rect().center());
		capture(&window, "standard-states-hover");
		QTest::mousePress(button, Qt::LeftButton);
		capture(&window, "standard-states-pressed");
		QTest::mouseRelease(button, Qt::LeftButton);
		QVERIFY(button->isChecked());
		QMenu menu;
		menu.addAction("Long submenu / 长菜单名称");
		auto* checked = menu.addAction("Checked");
		checked->setCheckable(true);
		checked->setChecked(true);
		auto* disabled = menu.addAction("Disabled");
		disabled->setEnabled(false);
		menu.addSeparator();
		menu.addMenu("Submenu")->addAction("Child");
		menu.popup(window.mapToGlobal(QPoint(20, 20)));
		capture(&menu, "standard-menu");
		menu.close();
		for (auto* widget : window.findChildren<QWidget*>())
			if (widget->isVisible() && widget->parentWidget() == &window)
				QVERIFY(window.rect().contains(widget->geometry()));
		window.close();
	}
	void crowdedWindow()
	{
		auto* song = Engine::getSong();
		song->createNewProject();
		auto* track = new SVSTrack(song);
		svs::Registry::instance().voices();
		QTRY_VERIFY_WITH_TIMEOUT(!svs::Registry::instance().scanning(), 30000);
		const auto voices = svs::Registry::instance().voices();
		QVERIFY(!voices.isEmpty());
		track->bindVoice(voices.first().pluginId, voices.first().id);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "scale-spot-check";
		note.duration = 96;
		note.pitch = 60;
		clip->setNotes({note});
		auto* editor = new SVSPianoRoll(clip);
		editor->openIn(m_gui->mainWindow());
		for (auto* canvas : editor->findChildren<SVSCanvas*>())
			if (!canvas->isParameterLane())
				canvas->setScroll(0, 67);
		showEditor(editor, "S05-scale-spot-check");
		auto* toolbar = editor->findChild<QScrollArea*>("svsToolbarScroll");
		QVERIFY(toolbar);
		toolbar->horizontalScrollBar()->setValue(toolbar->horizontalScrollBar()->maximum());
		showEditor(editor, "S05-scale-toolbar-end");
		for (auto* area : editor->findChildren<QScrollArea*>())
		{
			if (area == toolbar)
				continue;
			auto* scroll = area->verticalScrollBar();
			scroll->setValue(scroll->maximum());
			QCOMPARE(scroll->value(), scroll->maximum());
		}
		showEditor(editor, "S05-scale-parameters-end");
		{
			SetupDialog settings(SetupDialog::ConfigTab::AudioSettings);
			capture(&settings, "S02-scale-audio");
			settings.close();
		}
		song->setModified(false);
	}
	void installedLaunch()
	{
		const auto executable = qEnvironmentVariable("LMMS_UI_INSTALLED_EXE");
		if (executable.isEmpty())
			QSKIP("Installed executable smoke is selected explicitly.");
#ifdef Q_OS_WIN
		QVERIFY(QFile::exists(executable));
		ConfigManager::inst()->saveConfigFile();
		auto environment = QProcessEnvironment::systemEnvironment();
		environment.remove("LMMS_DATA_DIR");
		environment.remove("LMMS_PLUGIN_DIR");
		environment.remove("LMMS_SVS_PLUGIN_DIR");
		environment.remove("QT_PLUGIN_PATH");
		environment.remove("QT_QPA_PLATFORM_PLUGIN_PATH");
		environment.insert("PATH",
			QFileInfo(executable).absolutePath() + ";" + QFileInfo(executable).absolutePath() + "/plugins;"
				+ environment.value("SystemRoot") + "/System32;" + environment.value("SystemRoot"));
		QProcess child;
		child.setProcessEnvironment(environment);
		child.setWorkingDirectory(QFileInfo(executable).absolutePath());
		child.start(executable,
			{"--config", m_config.filePath("ui-config.xml"),
				QFileInfo(qEnvironmentVariable("LMMS_UI_FIXTURE")).absolutePath()
					+ "/modernization/modernization.mmp"});
		QVERIFY(child.waitForStarted(5000));
		struct WindowSearch
		{
			DWORD process;
			HWND window = nullptr;
		} search{static_cast<DWORD>(child.processId())};
		auto findWindow = [&]() {
			EnumWindows(
				[](HWND window, LPARAM parameter) -> BOOL {
					auto* result = reinterpret_cast<WindowSearch*>(parameter);
					DWORD process = 0;
					GetWindowThreadProcessId(window, &process);
					wchar_t title[512]{};
					GetWindowTextW(window, title, 512);
					if (process == result->process && IsWindowVisible(window)
						&& QString::fromWCharArray(title).contains("LMMS"))
						result->window = window;
					return TRUE;
				},
				reinterpret_cast<LPARAM>(&search));
			return search.window != nullptr;
		};
		QTRY_VERIFY_WITH_TIMEOUT(findWindow(), 15000);
		ShowWindow(search.window, SW_RESTORE);
		SetForegroundWindow(search.window);
		QTest::qWait(2000);
		QCOMPARE(child.state(), QProcess::Running);
		const auto image = m_gui->mainWindow()->screen()->grabWindow(reinterpret_cast<WId>(search.window));
		QVERIFY(!image.isNull());
		QVERIFY(image.save(m_output + "/S01-installed-executable.png"));
		PostMessageW(search.window, WM_CLOSE, 0, 0);
		QVERIFY(child.waitForFinished(10000));
		QCOMPARE(child.exitStatus(), QProcess::NormalExit);
		QCOMPARE(child.exitCode(), 0);
		QFile log(m_output + "/installed-executable.log");
		QVERIFY(log.open(QIODevice::WriteOnly));
		log.write(child.readAllStandardOutput() + child.readAllStandardError());
#else
		QSKIP("Native Windows installation validation.");
#endif
	}
	void cleanupTestCase()
	{
		if (m_gui)
		{
			delete static_cast<QWidget*>(m_gui->mainWindow());
			qDeleteAll(m_toolPlugins);
			m_toolPlugins.clear();
			m_gui.reset();
		}
	}
};
QTEST_MAIN(UiBaselineCapture)
#include "UiBaselineCapture.moc"
