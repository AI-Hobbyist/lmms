#include <QCheckBox>
#include <QComboBox>
#include <QDomDocument>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGridLayout>
#include <QHelpEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMdiArea>
#include <QMenuBar>
#include <QMessageBox>
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
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTranslator>
#include <QtTest>

#include "AudioDummy.h"
#include "AutomationClip.h"
#include "AutomationEditor.h"
#include "AutomationTrack.h"
#include "ComboBox.h"
#include "ConfigManager.h"
#include "Controller.h"
#include "ControllerRackView.h"
#include "DummyEffect.h"
#include "Effect.h"
#include "EffectControlDialog.h"
#include "EffectControls.h"
#include "ExportProjectDialog.h"
#include "GuiApplication.h"
#include "ImportFilter.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "Knob.h"
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
#include "SVCSettingsPage.h"
#include "SVCTrack.h"
#include "SVCViews.h"
#include "SVCWindow.h"
#include "SVSCanvas.h"
#include "SVSClip.h"
#include "SVSLyricEditor.h"
#include "SVSParameterPanel.h"
#include "SVSProjectImportDialog.h"
#include "SVSSettingsPage.h"
#include "SVSTrack.h"
#include "SVSViews.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "SetupDialog.h"
#include "SideBarWidget.h"
#include "SimpleTextFloat.h"
#include "Song.h"
#include "SongEditor.h"
#include "SubWindow.h"
#include "TabWidget.h"
#include "vsthost/ScanRootsWidget.h"
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
	QTranslator m_startupTranslator;
	QTranslator m_qtStartupTranslator;
	std::unique_ptr<GuiApplication> m_gui;
	QString m_output;
	QList<Plugin*> m_toolPlugins;
	void hoverCapture(QWidget* target, const QString& expected, const QString& name)
	{
		QVERIFY(target->isVisible());
		const auto point = target->rect().center();
		QTest::mouseMove(target, point);
		QTest::qWait(200);
		if (qobject_cast<Knob*>(target))
		{
			QMouseEvent move(QEvent::MouseMove, QPointF(point), QPointF(target->mapToGlobal(point)), Qt::NoButton,
				Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(target, &move);
		}
		if (!qobject_cast<Knob*>(target))
		{
			QHelpEvent help(QEvent::ToolTip, point, target->mapToGlobal(point));
			QApplication::sendEvent(target, &help);
		}
		auto findTooltip = [&]() -> QLabel* {
			for (auto* widget : QApplication::allWidgets())
			{
				auto* label = qobject_cast<QLabel*>(widget);
				if (label && qobject_cast<Knob*>(target))
				{
					auto* floating = qobject_cast<SimpleTextFloat*>(label->window());
					if (!floating || floating->source() != target) { continue; }
				}
				if (label && label->isVisible() && label->window()->windowType() == Qt::ToolTip
					&& label->text().contains(expected))
				{
					return label;
				}
			}
			return nullptr;
		};
		QLabel* tooltip = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((tooltip = findTooltip()) != nullptr, 5000);
		QTest::qWait(100);
		tooltip = findTooltip();
		if (!tooltip)
		{
			qInfo() << "Hover target" << target->metaObject()->className() << target->geometry()
					<< target->visibleRegion() << target->isEnabled() << "cursor" << QCursor::pos() << "target global"
					<< target->mapToGlobal(target->rect().center()) << "widget at cursor"
					<< QApplication::widgetAt(QCursor::pos()) << "active window" << QApplication::activeWindow()
					<< "target window" << target->window();
			for (auto* widget : QApplication::allWidgets())
			{
				if (auto* floating = qobject_cast<SimpleTextFloat*>(widget))
				{
					qInfo() << "Floating tooltip" << floating->isVisible() << floating->source() << "expected source"
							<< target << floating->windowType();
				}
			}
		}
		QVERIFY2(tooltip, qPrintable("Visible tooltip missing: " + expected));
		QVERIFY(!tooltip->text().isEmpty());
		capture(tooltip->window(), name);
		QToolTip::hideText();
		QTest::mouseMove(m_gui->mainWindow(), QPoint(5, 5));
		QTest::qWait(400);
	}
	void finalPluginHover(QWidget* panel, const QString& name)
	{
		if (!qEnvironmentVariableIsSet("LMMS_UI_FINAL_VALIDATION")) { return; }
		const bool wasVisible = panel->isVisible();
		panel->show();
		if (panel->parentWidget())
		{
			panel->parentWidget()->show();
			panel->parentWidget()->raise();
		}
		panel->raise();
		panel->window()->raise();
		panel->window()->activateWindow();
		QVERIFY(QTest::qWaitForWindowExposed(panel->window()));
		QTest::qWait(600);
		const auto prefix = qEnvironmentVariable("LMMS_UI_PLUGIN_PREFIX") + '-' + name;
		const char* viewContext = name == "monstro" ? "lmms::gui::MonstroView" : "lmms::gui::SaControlsDialog";
		const char* viewAction = name == "monstro" ? "Matrix view" : "Access advanced settings";
		if (name == "monstro" || name == "spectrumanalyzer")
		{
			QWidget* button = nullptr;
			for (auto* widget : panel->findChildren<QWidget*>())
			{
				if (widget->toolTip() == QCoreApplication::translate(viewContext, viewAction)) { button = widget; }
			}
			QVERIFY(button);
			QTest::mouseClick(button, Qt::LeftButton);
			capture(panel->window(), prefix + "-alternate-view");
			for (auto* knob : panel->findChildren<Knob*>())
			{
				if (knob->isVisible())
				{
					hoverCapture(knob, QString{}, prefix + "-alternate-tooltip");
					break;
				}
			}
			if (name == "monstro")
			{
				for (auto* widget : panel->findChildren<QWidget*>())
				{
					if (widget->toolTip() == QCoreApplication::translate(viewContext, "Operators view"))
					{
						QTest::mouseClick(widget, Qt::LeftButton);
						break;
					}
				}
			}
			else
			{
				QTest::mouseClick(button, Qt::LeftButton);
			}
		}
		if (name == "malletsstk")
		{
			for (auto* combo : panel->findChildren<ComboBox*>())
			{
				if (combo->isVisible())
				{
					const auto original = combo->model()->value();
					for (const auto preset : {9, 10})
					{
						combo->model()->setValue(preset);
						QCOMPARE(combo->model()->value(), preset);
						capture(panel->window(), prefix + "-preset-" + QString::number(preset));
					}
					combo->model()->setValue(original);
					break;
				}
			}
		}
		for (auto* led : panel->findChildren<LedCheckBox*>())
		{
			if (led->isVisible() && !led->text().isEmpty() && led->toolTip().contains(led->text()))
			{
				hoverCapture(led, led->text(), prefix + "-label-tooltip");
				break;
			}
		}
		Knob* representativeKnob = nullptr;
		for (auto* knob : panel->findChildren<Knob*>())
		{
			if (knob->isVisible() && !knob->getLabel().isEmpty())
			{
				if (!representativeKnob) { representativeKnob = knob; }
				if (knob->fontMetrics().horizontalAdvance(knob->getLabel()) > knob->width())
				{
					representativeKnob = knob;
					break;
				}
			}
		}
		if (representativeKnob)
		{
			const auto expected = representativeKnob->fontMetrics().horizontalAdvance(representativeKnob->getLabel())
					> representativeKnob->width()
				? representativeKnob->getLabel()
				: QString{};
			hoverCapture(representativeKnob, expected, prefix + "-knob-tooltip");
		}
		if (name == "slicert")
		{
			for (const auto* source : {"Threshold", "Fade Out"})
			{
				const auto title = QCoreApplication::translate("lmms::gui::SlicerTView", source);
				QWidget* control = nullptr;
				for (auto* widget : panel->findChildren<QWidget*>())
				{
					auto* knob = qobject_cast<Knob*>(widget);
					const auto tooltip = knob ? knob->toolTip() : widget->toolTip();
					if (widget->isVisible() && tooltip.startsWith(title + '\n')) { control = widget; }
				}
				QVERIFY(control);
				hoverCapture(control, title, prefix + '-' + QString::fromLatin1(source).replace(' ', '-') + "-tooltip");
			}
		}
		if (!wasVisible)
		{
			panel->hide();
			if (auto* frame = qobject_cast<QMdiSubWindow*>(panel->parentWidget())) { frame->hide(); }
		}
	}
	void capture(QWidget* widget, const QString& name)
	{
		// Native tooltip windows are already stable when captured by hoverCapture.
		// Activating them dismisses the tooltip and can delete the widget.
		if (widget->windowType() != Qt::ToolTip)
		{
			widget->show();
			widget->raise();
			widget->activateWindow();
			QVERIFY(QTest::qWaitForWindowExposed(widget->window()));
			QTest::qWait(600);
		}
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
		const auto startupLanguage = qEnvironmentVariable("LMMS_UI_TRANSLATION");
		if (!startupLanguage.isEmpty())
		{
			QVERIFY(m_startupTranslator.load(
				QString("%1/locale/%2.qm").arg(qEnvironmentVariable("LMMS_DATA_DIR"), startupLanguage)));
			QCoreApplication::installTranslator(&m_startupTranslator);
			if (startupLanguage != "en")
			{
				QVERIFY(m_qtStartupTranslator.load(
					QString("%1/locale/qt_%2.qm").arg(qEnvironmentVariable("LMMS_DATA_DIR"), startupLanguage)));
				QCoreApplication::installTranslator(&m_qtStartupTranslator);
			}
		}
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
		if (!startupLanguage.isEmpty()) { config->setValue("app", "language", startupLanguage); }
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
			{"windowWidth", window->width()}, {"windowHeight", window->height()}, {"language", startupLanguage},
			{"systemLocale", QLocale().name()}, {"simulatedScale", qEnvironmentVariable("QT_SCALE_FACTOR")}};
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
	void svcTranslations()
	{
		QCOMPARE(m_gui->mainWindow()->devicePixelRatioF(), 1.0);
		const auto startupLanguage = qEnvironmentVariable("LMMS_UI_TRANSLATION");
		QVERIFY(!startupLanguage.isEmpty());
		for (const auto& language : QStringList{startupLanguage})
		{
			QTranslator translator;
			QVERIFY(translator.load(QString("%1/locale/%2.qm").arg(qEnvironmentVariable("LMMS_DATA_DIR"), language)));
			QCoreApplication::installTranslator(&translator);
			auto* track = new SVCTrack(Engine::getSong());
			track->setName("User model 日本語 한국어");
			{
				SVCWindow window(track);
				QCOMPARE(window.windowTitle(),
					QCoreApplication::translate("lmms::gui::SVCWindow", "Singing Voice Conversion — %1")
						.arg(track->name()));
				const auto modes = window.findChild<QComboBox*>("svcAuditionMode");
				QVERIFY(modes);
				QCOMPARE(modes->itemText(2), QCoreApplication::translate("lmms::gui::SVCWindow", "A+B · Overlay"));
				QCOMPARE(nativeTranslation::rvcText("RVC", "pitch_shift"),
					QCoreApplication::translate("NativeRVC", "Pitch shift"));
				capture(&window, "M3-empty-" + language);
				window.close();
			}
			delete track;
			SVCSettingsPage settings;
			settings.resize(1000, 720);
			capture(&settings, "M3-settings-" + language);
			settings.close();
			QCoreApplication::removeTranslator(&translator);
		}
	}
	void translationClosure()
	{
		QCOMPARE(m_gui->mainWindow()->devicePixelRatioF(), 1.0);
		const auto language = qEnvironmentVariable("LMMS_UI_TRANSLATION");
		QVERIFY(!language.isEmpty());
		const auto translated = [](const char* source) { return QCoreApplication::translate("NativeSVS", source); };
		QCOMPARE(nativeTranslation::svsStatus("Rendering"), translated("Rendering"));
		QCOMPARE(nativeTranslation::svsStatus("Failed: Queued"), translated("Failed: %1").arg(translated("Queued")));
		QCOMPARE(nativeTranslation::svsStatus("External engine diagnostic"), QString("External engine diagnostic"));
		QCOMPARE(nativeTranslation::svsPronunciationDiagnostic("Unsupported note language: ja"),
			translated("Unsupported note language: %1").arg("ja"));
		QCOMPARE(nativeTranslation::svsPronunciationDiagnostic("Illegal phoneme: x; Illegal phoneme: y; "),
			translated("Illegal phoneme: %1").arg("x") + "; " + translated("Illegal phoneme: %1").arg("y"));
		ScanRootsWidget roots({}, "Invalid VST scan roots JSON");
		roots.resize(800, 400);
		QCOMPARE(roots.findChild<QLabel*>("vstRootError")->text(),
			QCoreApplication::translate("lmms::gui::ScanRootsWidget", "Invalid VST scan roots JSON"));
		if (language != "en")
		{
			QVERIFY(roots.findChild<QLabel*>("vstRootError")->text() != "Invalid VST scan roots JSON");
		}
		capture(&roots, "M6-scan-error-" + language);
		roots.close();
		auto* track = new SVSTrack(Engine::getSong());
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "closure-note";
		note.duration = 96;
		note.pitch = 60;
		note.lyric = "日本語 한국어 中文";
		note.language = "ja";
		clip->setNotes({note});
		{
			SVSLyricEditor lyrics(clip, {note.id});
			capture(&lyrics, "M6-lyric-diagnostic-" + language);
			lyrics.close();
		}
		QString error;
		QVERIFY(!clip->importDictionary("{}", error));
		QCOMPARE(error, translated("Missing dictionary fields"));
		QDomDocument document;
		auto node = document.createElement("svsclip");
		node.setAttribute("seed", "invalid");
		clip->loadSettings(node);
		QCOMPARE(clip->migrationDiagnostic(), translated("Invalid SVS seed; original node preserved"));
		{
			SVSPianoRoll editor(clip);
			editor.resize(1100, 660);
			capture(&editor, "M6-migration-error-" + language);
			editor.close();
		}
		delete track;
	}
	void hostTranslations()
	{
		QCOMPARE(m_gui->mainWindow()->devicePixelRatioF(), 1.0);
		const auto language = qEnvironmentVariable("LMMS_UI_TRANSLATION");
		QVERIFY(!language.isEmpty());
		capture(m_gui->mainWindow(), "M4-main-" + language);
		const QList<QPair<SetupDialog::ConfigTab, QString>> pages{
			{SetupDialog::ConfigTab::GeneralSettings, "general"}, {SetupDialog::ConfigTab::VstSettings, "vst"}};
		for (const auto& page : pages)
		{
			SetupDialog settings(page.first);
			QCOMPARE(settings.windowTitle(), QCoreApplication::translate("lmms::gui::SetupDialog", "Settings"));
			capture(&settings, "M4-" + page.second + '-' + language);
			settings.close();
		}
		ExportProjectDialog dialog(m_config.filePath("export.wav"), ExportProjectDialog::Mode::ExportProject);
		capture(&dialog, "M4-export-" + language);
		dialog.close();
	}
	void editorToolbarOverflowIcons()
	{
		const QList<QPair<QWidget*, QString>> editors{
			{m_gui->songEditor(), "song-toolbar-overflow"}, {m_gui->pianoRoll(), "piano-toolbar-overflow"}};
		for (const auto& entry : editors)
		{
			auto* editor = entry.first;
			auto* frame = qobject_cast<QMdiSubWindow*>(editor->parentWidget());
			QVERIFY(frame);
			const auto previousGeometry = frame->geometry();
			const auto previouslyVisible = frame->isVisible();
			editor->show();
			frame->show();
			frame->setGeometry(QRect(0, 0, 440, 500));
			frame->raise();
			capture(m_gui->mainWindow(), entry.second);
			QToolButton* visibleExtension = nullptr;
			for (auto* extension : editor->findChildren<QToolButton*>("qt_toolbar_ext_button"))
			{
				QCOMPARE(extension->iconSize(), QSize(12, 12));
				QVERIFY(!extension->icon().isNull());
				if (extension->isVisible()) { visibleExtension = extension; }
			}
			QVERIFY(visibleExtension);
			QVERIFY(visibleExtension->width() >= 20);
			auto* toolbar = qobject_cast<QToolBar*>(visibleExtension->parentWidget());
			QVERIFY(toolbar);
			const auto collapsedHeight = toolbar->height();
			QTest::mouseClick(visibleExtension, Qt::LeftButton);
			QTRY_VERIFY(toolbar->height() > collapsedHeight);
			capture(m_gui->mainWindow(), entry.second + "-open");
			for (auto* action : toolbar->actions())
			{
				auto* widget = toolbar->widgetForAction(action);
				if (action->isVisible() && !action->isSeparator() && widget)
				{
					QVERIFY2(widget->isVisible(), qPrintable(widget->objectName() + " " + action->text()));
				}
			}
			QTest::mouseClick(visibleExtension, Qt::LeftButton);
			frame->setGeometry(previousGeometry);
			frame->setVisible(previouslyVisible);
		}
	}
	void svsToolbarOverflow()
	{
		auto* config = ConfigManager::inst();
		const auto previousAxis = config->value("ui", "pitchalignmentaxis", "0");
		config->setValue("ui", "pitchalignmentaxis", "1");
		auto* track = new SVSTrack(Engine::getSong());
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		SVSPianoRoll editor(clip);
		editor.resize(1000, 660);
		capture(&editor, "svs-toolbar-overflow-closed");
		auto* toolbar = editor.findChild<QToolBar*>("svsEditorToolbar");
		QVERIFY(toolbar);
		auto* extension = toolbar->findChild<QToolButton*>("qt_toolbar_ext_button");
		auto* controls = editor.findChild<QWidget*>("noteLabelDisplayControls");
		auto* reference = controls ? controls->findChild<ComboBox*>("noteLabelReferenceComboBox") : nullptr;
		QVERIFY(extension && controls && reference);
		QVERIFY(extension->isVisible());
		QCOMPARE(extension->iconSize(), QSize(12, 12));
		QVERIFY(!extension->icon().isNull());
		QVERIFY(extension->width() >= 20);
		QVERIFY(!controls->isVisible());
		bool operated = false;
		QTimer::singleShot(300, &editor, [&] {
			QVERIFY(controls->isVisible());
			capture(&editor, "svs-toolbar-overflow-open");
			QCOMPARE(reference->model()->value(), 0);
			QTest::mouseClick(reference, Qt::LeftButton, Qt::NoModifier, QPoint(8, reference->height() / 2));
			QCOMPARE(reference->model()->value(), 1);
			QCOMPARE(editor.property("noteLabelReferenceOctave").toInt(), 0);
			operated = true;
			if (auto* popup = QApplication::activePopupWidget()) { popup->close(); }
		});
		QTest::mouseClick(extension, Qt::LeftButton);
		QTRY_VERIFY(operated);
		editor.close();
		config->setValue("ui", "pitchalignmentaxis", previousAxis);
	}
	void pitchAlignmentAxes()
	{
		QCOMPARE(m_gui->mainWindow()->devicePixelRatioF(), 1.0);
		auto* config = ConfigManager::inst();
		config->setValue("ui", "pitchalignmentaxis", "0");
		config->setValue("ui", "printnotelabels", "0");
		auto* song = Engine::getSong();
		auto* midiTrack = new InstrumentTrack(song);
		auto* midi = static_cast<MidiClip*>(midiTrack->createClip(0));
		midi->addNote(Note(TimePos(96), TimePos(0), 69));
		m_gui->pianoRoll()->setCurrentMidiClip(midi);
		auto* instrument = m_gui->pianoRoll()->findChild<PianoRoll*>();
		QVERIFY(instrument);
		{
			SetupDialog settings(SetupDialog::ConfigTab::GeneralSettings);
			auto* checkbox = settings.findChild<QCheckBox*>("pitchAlignmentAxisCheckBox");
			QVERIFY(checkbox && !checkbox->isChecked());
			auto* labelPosition = settings.findChild<QComboBox*>("alignmentLabelPosition");
			QVERIFY(labelPosition);
			QCOMPARE(labelPosition->currentData().toString(), QString("axes"));
			capture(&settings, "axis-global-settings");
			QTest::mouseClick(checkbox, Qt::LeftButton, Qt::NoModifier, QPoint(8, checkbox->height() / 2));
			QVERIFY(checkbox->isChecked());
			QVERIFY(QMetaObject::invokeMethod(&settings, "accept", Qt::DirectConnection));
			QCOMPARE(config->value("ui", "pitchalignmentaxis"), QString("1"));
		}
		auto* svsTrack = new SVSTrack(song);
		auto* clip = static_cast<SVSClip*>(svsTrack->createClip(0));
		{
			SVSPianoRoll editor(clip);
			editor.resize(1100, 660);
			SVSCanvas* canvas = nullptr;
			for (auto* candidate : editor.findChildren<SVSCanvas*>())
			{
				if (!candidate->isParameterLane()) { canvas = candidate; }
			}
			QVERIFY(canvas);
			capture(&editor, "axis-svs-window");
			auto* svsHoverControls = editor.findChild<QWidget*>("noteLabelDisplayControls");
			auto* instrumentHoverControls = m_gui->pianoRoll()->findChild<QWidget*>("noteLabelDisplayControls");
			QVERIFY(svsHoverControls && instrumentHoverControls);
			auto* toolbar = editor.findChild<QToolBar*>("svsEditorToolbar");
			QVERIFY(toolbar);
			QAction* hoverAction = nullptr;
			QAction* settingsAction = nullptr;
			for (auto* action : toolbar->actions())
			{
				if (toolbar->widgetForAction(action) == svsHoverControls) { hoverAction = action; }
				auto* widget = toolbar->widgetForAction(action);
				if (widget && widget->objectName() == "svsEditorSettingsButton")
				{
					settingsAction = action;
				}
			}
			QVERIFY(hoverAction && settingsAction);
			QVERIFY(hoverAction->isVisible());
			QVERIFY(toolbar->actions().indexOf(hoverAction) < toolbar->actions().indexOf(settingsAction));
			config->setValue("ui", "pitchalignmentaxis", "0");
			QVERIFY(svsHoverControls->isHidden());
			QVERIFY(instrumentHoverControls->isHidden());
			config->setValue("ui", "pitchalignmentaxis", "1");
			QVERIFY(hoverAction->isVisible() && !instrumentHoverControls->isHidden());
			QCOMPARE(editor.property("pitchAlignmentLineColor").value<QColor>(), QColor("#8ec6e8"));
			QCOMPARE(editor.property("timeAlignmentLineColor").value<QColor>(), QColor("#e8c68e"));
			editor.setStyleSheet("lmms--gui--SVSPianoRoll { qproperty-pitchAlignmentLineColor: #ff8090; "
				"qproperty-timeAlignmentLineColor: #90ff80; }");
			editor.ensurePolished();
			QCOMPARE(editor.property("pitchAlignmentLineColor").value<QColor>(), QColor("#ff8090"));
			QCOMPARE(editor.property("timeAlignmentLineColor").value<QColor>(), QColor("#90ff80"));
			auto verifyLines = [&](QWidget* target, QPoint pointer, const QString& prefix) {
				target->window()->raise();
				target->window()->activateWindow();
#ifdef Q_OS_WIN
				SetForegroundWindow(reinterpret_cast<HWND>(target->window()->winId()));
#endif
				QVERIFY(QTest::qWaitForWindowActive(target->window()));
				QTest::qWait(300);
				QCursor::setPos(target->mapToGlobal(QPoint(5, 5)));
				QTest::qWait(100);
				QTest::mouseMove(target, pointer);
				QTRY_VERIFY_WITH_TIMEOUT(target->underMouse(), 3000);
				QTest::qWait(300);
				const auto nativeImage = [&] {
					const auto origin = target->mapToGlobal(QPoint());
					return target->screen()->grabWindow(0, origin.x(), origin.y(), target->width(), target->height()).toImage();
				};
				config->setValue("ui", "pitchalignmentaxis", "0");
				QTest::qWait(300);
				QTest::mouseMove(target, pointer);
				QTest::qWait(100);
				const auto without = nativeImage();
				config->setValue("ui", "pitchalignmentaxis", "1");
				QTest::qWait(300);
				QCursor::setPos(target->mapToGlobal(pointer + QPoint(1, 1)));
				QTest::mouseMove(target, pointer);
				QTest::qWait(100);
				const auto with = nativeImage();
				QVERIFY(!with.isNull() && with.size() == without.size());
				int horizontalChanges = 0, verticalChanges = 0;
				for (int x = pointer.x() + 30; x < target->width() - 30; ++x)
				{
					if (with.pixel(x, pointer.y()) != without.pixel(x, pointer.y())) { ++horizontalChanges; }
				}
				for (int y = pointer.y() + 30; y < target->height() - 130; ++y)
				{
					if (with.pixel(pointer.x(), y) != without.pixel(pointer.x(), y)) { ++verticalChanges; }
				}
				QVERIFY(horizontalChanges > 20);
				QVERIFY(verticalChanges > 10);
				const QColor pitchColor(prefix == "axis-svs" ? "#ff8090" : "#8ec6e8");
				const QColor timeColor(prefix == "axis-svs" ? "#90ff80" : "#e8c68e");
				int pitchPixels = 0, timePixels = 0;
				for (int x = pointer.x() + 30; x < target->width() - 30; ++x)
				{
					if (with.pixelColor(x, pointer.y()) == pitchColor) { ++pitchPixels; }
				}
				for (int y = pointer.y() + 30; y < target->height() - 130; ++y)
				{
					if (with.pixelColor(pointer.x(), y) == timeColor) { ++timePixels; }
				}
				QVERIFY(pitchPixels > 20);
				QVERIFY(timePixels > 10);
				QVERIFY(with.save(m_output + '/' + prefix + "-standard.png"));
				config->setValue("ui", "printnotelabels", "1");
				config->setValue("ui", "notelabelmode", "pitch");
				QTest::qWait(300);
				const auto standardKeyboard = nativeImage().copy(0, 30, 60, target->height() - 160);
				config->setValue("ui", "notelabelmode", "numbered");
				QTest::mouseMove(target, pointer + QPoint(90, 35));
				QTest::qWait(300);
				QCOMPARE(nativeImage().copy(0, 30, 60, target->height() - 160), standardKeyboard);
				QVERIFY(nativeImage().save(m_output + '/' + prefix + "-numbered.png"));
				config->setValue("ui", "pitchalignmentlabelposition", "cursor");
				QTest::qWait(300);
				QVERIFY(nativeImage().save(m_output + '/' + prefix + "-cursor.png"));
				config->setValue("ui", "pitchalignmentlabelposition", "axes");
			};
			verifyLines(canvas, canvas->pointAt(48, 69).toPoint() + QPoint(0, 12), "axis-svs");
			config->setValue("ui", "notelabeltonic", "0");
			config->setValue("ui", "notelabeloctave", "5");
			config->setValue("ui", "printnotelabels", "0");
			for (const auto pitch : {84, 36})
			{
				canvas->setScroll(0, pitch + 8);
				QTest::qWait(100);
				QTest::mouseMove(canvas, canvas->pointAt(48, pitch).toPoint() + QPoint(0, 12));
				for (const auto& position : {QString("axes"), QString("cursor")})
				{
					config->setValue("ui", "pitchalignmentlabelposition", position);
					QTest::qWait(300);
					capture(&editor, QString("axis-octave-%1-%2").arg(pitch).arg(position));
				}
			}
			config->setValue("ui", "pitchalignmentlabelposition", "axes");
			editor.hide();
			config->setValue("ui", "printnotelabels", "0");
			m_gui->pianoRoll()->show();
			auto* frame = qobject_cast<QMdiSubWindow*>(m_gui->pianoRoll()->parentWidget());
			QVERIFY(frame);
			frame->show();
			frame->setGeometry(QRect(0, 0, 1000, 600));
			frame->raise();
			m_gui->mainWindow()->raise();
			m_gui->mainWindow()->activateWindow();
			QTest::qWait(600);
			verifyLines(instrument, QPoint(240, 160), "axis-instrument");
			capture(m_gui->mainWindow(), "axis-instrument-controls");
			QVERIFY(instrumentHoverControls->isVisible());
			auto* instrumentTonic = instrumentHoverControls->findChild<ComboBox*>("noteLabelTonicComboBox");
			auto* instrumentPlay = m_gui->pianoRoll()->findChild<QToolButton*>("playButton");
			QVERIFY(instrumentTonic && instrumentPlay);
			QVERIFY(std::abs(instrumentTonic->mapTo(m_gui->pianoRoll(), instrumentTonic->rect().center()).y()
				- instrumentPlay->mapTo(m_gui->pianoRoll(), instrumentPlay->rect().center()).y()) <= 2);
			QVERIFY(m_gui->pianoRoll()->width()
				- instrumentHoverControls->mapTo(m_gui->pianoRoll(), instrumentHoverControls->rect().topRight()).x() <= 32);
			config->setValue("ui", "pitchalignmentaxis", "0");
			frame->hide();
		}
		m_gui->pianoRoll()->setCurrentMidiClip(nullptr);
		// Song owns the tracks; let normal application teardown clean up their views.
		config->setValue("ui", "printnotelabels", "0");
		config->setValue("ui", "notelabelmode", "pitch");
		{
			SetupDialog settings(SetupDialog::ConfigTab::GeneralSettings);
			auto* position = settings.findChild<QComboBox*>("alignmentLabelPosition");
			QVERIFY(position);
			position->setCurrentIndex(position->findData("cursor"));
			QVERIFY(QMetaObject::invokeMethod(&settings, "accept", Qt::DirectConnection));
			QCOMPARE(config->value("ui", "pitchalignmentlabelposition"), QString("cursor"));
		}
		config->setValue("ui", "pitchalignmentlabelposition", "axes");
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
		const auto selectedPlugins = qEnvironmentVariable("LMMS_UI_PLUGIN_NAMES").split(',', Qt::SkipEmptyParts);
		const auto evidencePrefix = qEnvironmentVariable("LMMS_UI_PLUGIN_PREFIX", "S08-plugin");
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
			if (!selectedPlugins.isEmpty() && !selectedPlugins.contains(name))
			{
				continue;
			}
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
				if (name == "bitinvader" && !selectedPlugins.isEmpty())
				{
					QStringList labels;
					for (auto* label : window->findChildren<LedCheckBox*>())
					{
						labels.append(label->text());
					}
					QVERIFY(labels.contains(QCoreApplication::translate("lmms::gui::BitInvaderView", "Interpolation")));
					QVERIFY(labels.contains(QCoreApplication::translate("lmms::gui::BitInvaderView", "Normalize")));
				}
				window->toggleVisibility(true);
				window->parentWidget()->move(0, 0);
				QVERIFY(window->isVisible());
				QVERIFY(!window->visibleRegion().isEmpty());
				capture(m_gui->mainWindow(), evidencePrefix + '-' + name);
				finalPluginHover(window, name);
				if (name == "xpressive" && !selectedPlugins.isEmpty())
				{
					QWidget* helpButton = nullptr;
					for (auto* widget : window->findChildren<QWidget*>())
					{
						if (widget->toolTip()
							== QCoreApplication::translate("lmms::gui::XpressiveView", "Open help window"))
						{
							helpButton = widget;
						}
					}
					QVERIFY(helpButton);
					QTest::mouseClick(helpButton, Qt::LeftButton);
					QTextEdit* help = nullptr;
					for (auto* widget : QApplication::allWidgets())
					{
						auto* text = qobject_cast<QTextEdit*>(widget);
						if (text
							&& text->windowTitle()
								== QCoreApplication::translate("lmms::gui::XpressiveHelpView", "Xpressive Help"))
						{
							help = text;
						}
					}
					QVERIFY(help);
					if (qEnvironmentVariable("LMMS_UI_TRANSLATION") != "en")
					{
						QVERIFY(!help->toPlainText().contains("Two output waves"));
					}
					QVERIFY(help->isVisible());
					capture(m_gui->mainWindow(), evidencePrefix + "-help");
					if (qEnvironmentVariableIsSet("LMMS_UI_FINAL_VALIDATION"))
					{
						help->verticalScrollBar()->setValue(help->verticalScrollBar()->maximum());
						QCOMPARE(help->verticalScrollBar()->value(), help->verticalScrollBar()->maximum());
						capture(m_gui->mainWindow(), evidencePrefix + "-help-bottom");
					}
					help->parentWidget()->hide();
				}
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
				if (panel && qEnvironmentVariableIsSet("LMMS_UI_FINAL_VALIDATION"))
				{
					// EffectView uses this same production container for effect controls.
					m_gui->mainWindow()->addWindowedWidget(panel);
				}
				QVERIFY(panel);
				const auto preset = settings(effect);
				showEditor(panel, evidencePrefix + '-' + name);
				finalPluginHover(panel, name);
				if ((name == "frequencyshifter" || name == "granularpitchshifter" || name == "slewdistortion")
					&& !selectedPlugins.isEmpty())
				{
					const bool granular = name == "granularpitchshifter";
					const bool slew = name == "slewdistortion";
					const char* panelContext = slew ? "lmms::gui::SlewDistortionControlDialog"
						: granular					? "lmms::gui::GranularPitchShifterControlDialog"
													: "lmms::gui::FrequencyShifterControlDialog";
					const char* helpContext = slew ? "lmms::gui::SlewDistortionHelpView"
						: granular				   ? "lmms::gui::GranularPitchShifterHelpView"
												   : "lmms::gui::FrequencyShifterHelpView";
					const char* helpTitle = slew ? "Slew Distortion Help"
						: granular				 ? "Granular Pitch Shifter Help"
												 : "Frequency Shifter Help";
					QWidget* helpButton = nullptr;
					for (auto* widget : panel->findChildren<QWidget*>())
					{
						if (widget->toolTip() == QCoreApplication::translate(panelContext, "Open help window"))
						{
							helpButton = widget;
						}
					}
					QVERIFY(helpButton);
					panel->show();
					if (panel->parentWidget())
					{
						panel->parentWidget()->show();
					}
					QVERIFY(QTest::qWaitForWindowExposed(panel->window()));
					QVERIFY(helpButton->isVisible());
					QTest::qWait(200);
					QTest::mouseClick(helpButton, Qt::LeftButton);
					QCoreApplication::processEvents();
					QTextEdit* help = nullptr;
					for (auto* widget : QApplication::allWidgets())
					{
						auto* text = qobject_cast<QTextEdit*>(widget);
						if (text && text->windowTitle() == QCoreApplication::translate(helpContext, helpTitle))
						{
							help = text;
						}
					}
					QVERIFY(help);
					if (qEnvironmentVariable("LMMS_UI_TRANSLATION") != "en")
					{
						QVERIFY(!help->toPlainText().contains(slew ? "Slew Distortion is a multiband slew rate limiter"
								: granular							 ? "Granular Pitch Shifter"
																	 : "Frequency Shifter"));
					}
					showEditor(help, evidencePrefix + "-help");
					if (granular)
					{
						// The plugin owns this static widget; the test GUI must not delete it.
						help->setParent(nullptr);
						help->hide();
					}
					panel->hide();
				}
				checkPreset(effect, preset);
			}
			else
			{
				auto* plugin = Plugin::instantiate(name, Engine::getSong(), nullptr);
				QVERIFY(plugin);
				m_toolPlugins.append(plugin);
				auto* panel = plugin->createView(m_gui->mainWindow());
				QVERIFY(panel);
				showEditor(panel, evidencePrefix + '-' + name);
				finalPluginHover(panel, name);
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
		QVERIFY(!coverage.isEmpty());
		for (const auto& name : selectedPlugins)
		{
			bool recorded = false;
			for (const auto& entry : coverage)
			{
				recorded |= entry.toObject().value("plugin").toString() == name;
			}
			if (!recorded)
			{
				coverage.append(QJsonObject{{"plugin", name}, {"status", "MANUAL/PENDING"},
					{"reason", "Plugin unavailable in the configured development deployment"}});
			}
		}
		const auto reportName = selectedPlugins.isEmpty() ? "plugin-panels" : evidencePrefix + "-panels";
		QFile report(m_output + '/' + reportName + ".json");
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
	void sidebarTranslationFollowup()
	{
		const auto language = qEnvironmentVariable("LMMS_UI_TRANSLATION");
		const QMap<QString, QStringList> titles{{"zh_CN", {"歌声转换", "歌声合成", "我的收藏"}},
			{"ja", {"歌声変換", "歌声合成", "お気に入り"}}, {"en", {"SVC", "SVS", "My Favorites"}},
			{"ko", {"가창 변환", "가창 합성", "즐겨찾기"}}};
		QVERIFY(titles.contains(language));
		auto* main = m_gui->mainWindow();
		QCOMPARE(main->devicePixelRatioF(), 1.0);
		int index = 0;
		for (const auto& title : titles.value(language))
		{
			SideBarWidget* panel = nullptr;
			for (auto* candidate : main->findChildren<SideBarWidget*>())
			{
				if (candidate->title() == title) { panel = candidate; }
			}
			QVERIFY2(panel, qPrintable(title));
			QToolButton* button = nullptr;
			for (auto* candidate : main->findChildren<QToolButton*>())
			{
				if (candidate->toolTip() == title) { button = candidate; }
			}
			QVERIFY(button);
			QTest::mouseClick(button, Qt::LeftButton);
			QVERIFY(panel->isVisible());
			capture(main, "sidebar-followup-" + language + '-' + QString::number(++index));
			QTest::mouseClick(button, Qt::LeftButton);
		}
		auto* svsTrack = new SVSTrack(Engine::getSong());
		auto* svcTrack = new SVCTrack(Engine::getSong());
		QCoreApplication::processEvents();
		int checked = 0;
		for (auto* view : main->findChildren<TrackView*>())
		{
			if (view->getTrack() != svsTrack && view->getTrack() != svcTrack) { continue; }
			QStringList labels;
			for (auto* knob : view->findChildren<Knob*>())
			{
				labels.append(knob->getLabel());
			}
			QVERIFY(labels.contains(QCoreApplication::translate("lmms::gui::InstrumentTrackView", "VOL")));
			QVERIFY(labels.contains(QCoreApplication::translate("lmms::gui::InstrumentTrackView", "PAN")));
			++checked;
		}
		QCOMPARE(checked, 2);
		capture(main, "sidebar-followup-" + language + "-track-knobs");
		delete svcTrack;
		delete svsTrack;
	}
	void finalTranslations()
	{
		const auto language = qEnvironmentVariable("LMMS_UI_TRANSLATION");
		QVERIFY(!language.isEmpty());
		QCOMPARE(m_gui->mainWindow()->devicePixelRatioF(), 1.0);
		const auto prefix = "M7-" + language;
		QFile invalidMidi(m_config.filePath("invalid-日本語-한국어.mid"));
		QVERIFY(invalidMidi.open(QIODevice::WriteOnly));
		QCOMPARE(invalidMidi.write("invalid-midi"), qint64(12));
		invalidMidi.close();
		int midiDialogs = 0;
		QTimer dismissMidi;
		connect(&dismissMidi, &QTimer::timeout, this, [&] {
			for (auto* widget : QApplication::topLevelWidgets())
			{
				auto* message = qobject_cast<QMessageBox*>(widget);
				if (message && message->isVisible())
				{
					dismissMidi.stop();
					capture(message, prefix + "-midi-error-" + QString::number(++midiDialogs));
					message->done(QMessageBox::Ok);
					dismissMidi.start(500);
					break;
				}
			}
		});
		dismissMidi.start(500);
		ImportFilter::import(invalidMidi.fileName(), Engine::getSong());
		dismissMidi.stop();
		QVERIFY(midiDialogs > 0);
		capture(m_gui->mainWindow(), prefix + "-main");
		auto* menuBar = m_gui->mainWindow()->menuBar();
		QVERIFY(!menuBar->actions().isEmpty());
		auto* menu = menuBar->actions().first()->menu();
		QVERIFY(menu);
		menu->popup(menuBar->mapToGlobal(menuBar->actionGeometry(menuBar->actions().first()).bottomLeft()));
		capture(menu, prefix + "-menu");
		menu->close();
		for (const auto tab : {SetupDialog::ConfigTab::GeneralSettings, SetupDialog::ConfigTab::VstSettings,
				 SetupDialog::ConfigTab::SvsSettings})
		{
			SetupDialog settings(tab);
			const auto suffix = QString::number(static_cast<int>(tab));
			capture(&settings, prefix + "-settings-" + suffix);
			for (auto* scroll : settings.findChildren<QScrollArea*>())
			{
				if (scroll->isVisible() && scroll->verticalScrollBar()->maximum() > 0)
				{
					scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
					capture(&settings, prefix + "-settings-bottom-" + suffix);
				}
			}
			if (auto* memory = settings.findChild<QComboBox*>("svsComputeMemoryPolicy"))
			{
				if (memory->isVisible()) { hoverCapture(memory, memory->toolTip(), prefix + "-compute-tooltip"); }
			}
			for (auto* tabs : settings.findChildren<QTabWidget*>())
			{
				for (int index = 0; index < tabs->count(); ++index)
				{
					if (tabs->tabText(index).contains("DiffSinger"))
					{
						tabs->setCurrentIndex(index);
						capture(&settings, prefix + "-diffsinger-settings");
					}
				}
			}
			settings.close();
		}
		{
			ScanRootsWidget roots({}, "Invalid VST scan roots JSON");
			roots.resize(800, 400);
			capture(&roots, prefix + "-scan-error");
			roots.close();
			SVSProjectImportDialog dialog(QJsonObject{{"id", "test"}, {"name", "Test format"}});
			capture(&dialog, prefix + "-svs-import");
			dialog.close();
			ExportProjectDialog exportDialog(m_config.filePath("export.wav"), ExportProjectDialog::Mode::ExportProject);
			capture(&exportDialog, prefix + "-export");
			exportDialog.close();
		}
		auto* svcTrack = new SVCTrack(Engine::getSong());
		svcTrack->setName("User model 日本語 한국어");
		{
			SVCWindow window(svcTrack);
			capture(&window, prefix + "-svc-empty");
			window.close();
			SVCSettingsPage settings;
			settings.resize(1000, 720);
			capture(&settings, prefix + "-svc-settings");
			settings.close();
		}
		delete svcTrack;
		auto* track = new SVSTrack(Engine::getSong());
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "final-note";
		note.duration = 96;
		note.pitch = 60;
		note.lyric = "日本語 한국어 中文";
		note.language = "ja";
		clip->setNotes({note});
		{
			SVSLyricEditor lyrics(clip, {note.id});
			capture(&lyrics, prefix + "-lyrics");
			auto* table = lyrics.findChild<QTableWidget*>("svsBatchLyricPreview");
			QVERIFY(table && table->item(0, 3));
			QCOMPARE(table->item(0, 3)->toolTip(), table->item(0, 3)->text());
			const auto point = table->visualItemRect(table->item(0, 3)).center();
			QTest::mouseMove(table->viewport(), point);
			QTest::qWait(200);
			QHelpEvent help(QEvent::ToolTip, point, table->viewport()->mapToGlobal(point));
			QApplication::sendEvent(table->viewport(), &help);
			QTest::qWait(600);
			QCOMPARE(QToolTip::text(), table->item(0, 3)->text());
			for (auto* widget : QApplication::topLevelWidgets())
			{
				if (widget->isVisible() && widget->windowType() == Qt::ToolTip)
				{
					capture(widget, prefix + "-lyric-tooltip");
					break;
				}
			}
			QToolTip::hideText();
			QTest::qWait(400);
			lyrics.close();
		}
		QDomDocument document;
		auto node = document.createElement("svsclip");
		node.setAttribute("seed", "invalid");
		clip->loadSettings(node);
		{
			SVSPianoRoll editor(clip);
			editor.resize(1100, 660);
			capture(&editor, prefix + "-svs-wide");
			auto* status = editor.findChild<QLabel*>("svsSynthesisStatus");
			QVERIFY(status);
			const auto fullText = nativeTranslation::svsStatus(clip->status());
			QCOMPARE(status->toolTip(), fullText);
			auto* toolbar = editor.findChild<QScrollArea*>("svsToolbarScroll");
			QVERIFY(toolbar);
			toolbar->ensureWidgetVisible(status);
			QTest::qWait(600);
			QVERIFY(status->width() >= status->fontMetrics().horizontalAdvance(QString(QChar(0x2026))));
			QVERIFY(!status->visibleRegion().isEmpty());
			capture(&editor, prefix + "-svs-toolbar-end");
			hoverCapture(status, fullText, prefix + "-svs-status-tooltip");
			editor.resize(900, 600);
			QCoreApplication::processEvents();
			toolbar->ensureWidgetVisible(status);
			capture(&editor, prefix + "-svs-narrow");
			QCOMPARE(status->toolTip(), fullText);
			editor.close();
		}
		delete track;
	}
	void finalProjectDialog()
	{
		const auto prefix = "M7-" + qEnvironmentVariable("LMMS_UI_TRANSLATION");
		const QJsonObject format{{"id", "test"}, {"name", "Test format"},
			{"exportPolicy", QJsonObject{{"singing", 1}, {"audio", 1}}},
			{"outputDefaults", QJsonObject{{"use_edited_pitch", true}}}};
		const QJsonObject project{{"track_list",
			QJsonArray{QJsonObject{{"type_", "Singing"}, {"title", "日本語 한국어 中文"}},
				QJsonObject{{"type_", "Instrumental"}, {"title", "User accompaniment"}}}}};
		SVSProjectExportDialog dialog(format, project);
		QCOMPARE(dialog.windowTitle(), QCoreApplication::translate("SVSProjectUI", "Export SVS project"));
		capture(&dialog, prefix + "-svs-project-export");
		dialog.close();
	}
	void hostVolumeCurve()
	{
		const auto prefix = "host-volume-" + qEnvironmentVariable("LMMS_UI_TRANSLATION");
		auto* track = new SVSTrack(Engine::getSong());
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		{
			SVSPianoRoll editor(clip);
			editor.resize(1100, 660);
			capture(&editor, prefix + "-unselected");
			QCOMPARE(editor.devicePixelRatioF(), 1.0);
			auto* tab = editor.findChild<QToolButton*>("svsParameterTab.input:svs.volume");
			QVERIFY(tab && tab->isVisible() && tab->isEnabled());
			QCOMPARE(tab->text(), QCoreApplication::translate("NativeSVS", "Volume"));
			QCOMPARE(clip->editorState()["selectedParameter"].toString(), QString("input:svs.volume"));
			const auto voices = svs::Registry::instance().voices();
			auto voice = std::find_if(voices.cbegin(), voices.cend(),
				[](const auto& item) { return item.pluginId == "org.lmms.svs.example"; });
			QVERIFY(voice != voices.cend());
			track->bindVoice(voice->pluginId, "full");
			QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
			svs::Curve curve;
			curve.id = svs::VolumeId;
			curve.unit = "dB";
			curve.insert(0, -12.);
			curve.insert(48, 6.);
			curve.insert(96, 0.);
			clip->setEditorData(clip->notes(), {{svs::VolumeId, curve}});
			QTest::mouseClick(tab, Qt::LeftButton);
			capture(&editor, prefix + "-engine");
			QCOMPARE(tab->text(), QCoreApplication::translate("NativeSVS", "Volume"));
			QVERIFY(tab->isVisible() && tab->isEnabled());
			editor.close();
		}
		delete track;
	}
	void svsRegisterFollowup()
	{
		const auto prefix = "svs-register-" + qEnvironmentVariable("LMMS_UI_TRANSLATION");
		QFile schemaFile(qEnvironmentVariable("LMMS_UI_DIFFSINGER_SCHEMA"));
		QVERIFY(schemaFile.open(QIODevice::ReadOnly));
		svs::Capabilities capabilities;
		QString error;
		QVERIFY2(svs::Capabilities::parse(QJsonDocument::fromJson(schemaFile.readAll()).object(), capabilities, error),
			qPrintable(error));
		const auto* shift = capabilities.parameter("diffsinger.tone_shift", "clip");
		QVERIFY(shift);
		QCOMPARE(shift->minimum, -12.);
		QCOMPARE(shift->maximum, 12.);
		QScrollArea window;
		auto* panel = new SVSParameterPanel;
		window.setWidget(panel);
		window.setWidgetResizable(true);
		const auto display = nativeTranslation::svsParameters("org.lmms.svs.diffsinger", capabilities.parameters);
		panel->refresh(display, "clip", {QJsonObject{}}, {}, {});
		for (const auto& parameter : display)
		{
			if (parameter.id.endsWith(".offset"))
			{
				const auto source = parameter.id.mid(QString("diffsinger.").size()).chopped(QString(".offset").size());
				QCOMPARE(parameter.name, nativeTranslation::svsText("org.lmms.svs.diffsinger", source));
			}
		}
		if (qEnvironmentVariable("LMMS_UI_TRANSLATION") == "zh_CN")
		{
			QCOMPARE(nativeTranslation::svsText("org.lmms.svs.diffsinger", "breathiness (absolute)"),
				QString("气声（实参）"));
			QCOMPARE(nativeTranslation::svsText("org.lmms.svs.diffsinger", "Tone shift"), QString("音区偏移"));
		}
		window.resize(500, 760);
		capture(&window, prefix + "-parameters-top");
		QCOMPARE(window.devicePixelRatioF(), 1.0);
		window.verticalScrollBar()->setValue(window.verticalScrollBar()->maximum());
		capture(&window, prefix + "-parameters-bottom");
		window.close();
		const auto voices = svs::Registry::instance().voices();
		const auto voice = std::find_if(voices.cbegin(), voices.cend(),
			[](const auto& candidate) { return candidate.pluginId == "org.lmms.svs.example"; });
		QVERIFY(voice != voices.cend());
		auto* track = new SVSTrack(Engine::getSong());
		track->bindVoice(voice->pluginId, voice->id);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "lyric-only-note";
		note.duration = 96;
		note.pitch = 60;
		note.lyric = "你 あ 한 a";
		note.language = "zh";
		clip->setNotes({note});
		auto* config = ConfigManager::inst();
		const auto previous = config->value("ui", "printnotelabels", "0");
		config->setValue("ui", "printnotelabels", "1");
		{
			SVSPianoRoll editor(clip);
			editor.resize(1100, 660);
			capture(&editor, prefix + "-lyric-only");
			editor.close();
		}
		QCOMPARE(clip->notes().first().pitch, 60.);
		config->setValue("ui", "printnotelabels", previous);
		delete track;
	}
	void finalVoiceScenes()
	{
		const auto language = qEnvironmentVariable("LMMS_UI_TRANSLATION");
		const auto prefix = "M7-" + language;
		const auto voices = svs::Registry::instance().voices();
		const auto voice = std::find_if(voices.cbegin(), voices.cend(),
			[](const auto& candidate) { return candidate.pluginId == "org.lmms.svs.example"; });
		QVERIFY(voice != voices.cend());
		auto* track = new SVSTrack(Engine::getSong());
		track->bindVoice(voice->pluginId, voice->id);
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note note;
		note.id = "voice-scene-note";
		note.duration = 96;
		note.pitch = 60;
		note.lyric = "你";
		note.language = "zh";
		clip->setNotes({note});
		{
			SVSPianoRoll editor(clip);
			editor.resize(1100, 660);
			capture(&editor, prefix + "-voice-editor");
			auto* config = ConfigManager::inst();
			const auto previousLabels = config->value("ui", "printnotelabels", "0");
			const auto previousMode = config->value("ui", "notelabelmode", "pitch");
			config->setValue("ui", "printnotelabels", "1");
			auto* labels = editor.findChild<ComboBox*>("noteLabelTonicComboBox");
			QVERIFY(labels && labels->isEnabled());
			QTimer::singleShot(150, labels, [this, labels, prefix] {
				auto* menu = labels->findChild<QMenu*>();
				QVERIFY(menu);
				capture(menu, prefix + "-note-label-menu");
				menu->close();
			});
			QTest::mouseClick(labels, Qt::LeftButton, Qt::NoModifier, QPoint(labels->width() - 5, labels->height() / 2));
			config->setValue("ui", "notelabelmode", "numbered");
			QCOMPARE(config->value("ui", "notelabelmode"), QString("numbered"));
			capture(&editor, prefix + "-numbered-notes");
			config->setValue("ui", "notelabelmode", previousMode);
			config->setValue("ui", "printnotelabels", previousLabels);
			int scrollIndex = 0;
			for (auto* scroll : editor.findChildren<QScrollArea*>())
			{
				if (scroll->isVisible() && scroll->horizontalScrollBar()->maximum() > 0)
				{
					scroll->horizontalScrollBar()->setValue(scroll->horizontalScrollBar()->maximum());
					capture(&editor, prefix + "-voice-horizontal-end-" + QString::number(++scrollIndex));
				}
				if (scroll->isVisible() && scroll->verticalScrollBar()->maximum() > 0)
				{
					scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
					capture(&editor, prefix + "-voice-parameters-bottom-" + QString::number(++scrollIndex));
				}
			}
			editor.close();
		}
		delete track;
		SVSSettingsPage settings;
		settings.resize(1000, 720);
		settings.show();
		QVERIFY(QTest::qWaitForWindowExposed(&settings));
		for (auto* tabs : settings.findChildren<QTabWidget*>())
		{
			for (int index = 0; index < tabs->count(); ++index)
			{
				if (tabs->tabText(index).contains("DiffSinger")) { tabs->setCurrentIndex(index); }
			}
		}
		capture(&settings, prefix + "-diffsinger-full");
		for (auto* scroll : settings.findChildren<QScrollArea*>())
		{
			if (scroll->isVisible() && scroll->verticalScrollBar()->maximum() > 0)
			{
				scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
				capture(&settings, prefix + "-diffsinger-bottom");
			}
		}
		settings.close();
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
		QStringList arguments{"--config", m_config.filePath("ui-config.xml")};
		const bool finalValidation = qEnvironmentVariableIsSet("LMMS_UI_FINAL_VALIDATION");
		if (!finalValidation)
		{
			arguments.append(
				QFileInfo(qEnvironmentVariable("LMMS_UI_FIXTURE")).absolutePath() + "/modernization/modernization.mmp");
		}
		child.start(executable, arguments);
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
		if (finalValidation) { QCOMPARE(GetDpiForWindow(search.window), UINT(96)); }
		ShowWindow(search.window, SW_RESTORE);
		SetForegroundWindow(search.window);
		QTest::qWait(2000);
		QCOMPARE(child.state(), QProcess::Running);
		const auto image = m_gui->mainWindow()->screen()->grabWindow(reinterpret_cast<WId>(search.window));
		QVERIFY(!image.isNull());
		const auto evidenceName = finalValidation ? "M7-installed-" + qEnvironmentVariable("LMMS_UI_TRANSLATION")
												  : QString("S01-installed-executable");
		QVERIFY(image.save(m_output + '/' + evidenceName + ".png"));
		PostMessageW(search.window, WM_CLOSE, 0, 0);
		QVERIFY(child.waitForFinished(10000));
		QCOMPARE(child.exitStatus(), QProcess::NormalExit);
		QCOMPARE(child.exitCode(), 0);
		QFile log(m_output + '/' + evidenceName + ".log");
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
