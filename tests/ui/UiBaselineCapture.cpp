#include <QCheckBox>
#include <QComboBox>
#include <QDomDocument>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHelpEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLibraryInfo>
#include <QMap>
#include <QMdiArea>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QAbstractButton>
#include <QProcess>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTranslator>
#include <QtMath>
#include <QtTest>

#include "AudioDummy.h"
#include "AICacheSettingsPage.h"
#include "AICacheBudget.h"
#include "SVCCache.h"
#include "SVSCache.h"
#include "AutomationClip.h"
#include "AutomationEditor.h"
#include "AutomationTrack.h"
#include "ComboBox.h"
#include "ConfigManager.h"
#include "Controller.h"
#include "ControllerRackView.h"
#include "DataFile.h"
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
#include "MidiDummy.h"
#include "NotePlayHandle.h"

#include <QAction>
#include "MidiClip.h"
#include "MidiClipView.h"
#include "MidiEvent.h"
#include "Mixer.h"
#include "MixerView.h"
#include "NativePluginTranslation.h"
#include "PatternEditor.h"
#include "PatternStore.h"
#include <QContextMenuEvent>

#include "FileBrowser.h"
#include "PatternTrack.h"
#include "NodeWorkflowCanvas.h"
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

// Advance audio explicitly while checking the real GUI, without a concurrent dummy-device thread.
class UiManualAudioDevice final : public AudioDummy
{
public:
	using AudioDummy::AudioDummy;

private:
	void startProcessingImpl() override {}
	void stopProcessingImpl() override {}
};

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
				const auto catalog = QStringLiteral("qt_%1.qm").arg(startupLanguage);
				QVERIFY(m_qtStartupTranslator.load(QDir(qEnvironmentVariable("LMMS_DATA_DIR") + "/locale").filePath(catalog))
					|| m_qtStartupTranslator.load(QDir(QLibraryInfo::path(QLibraryInfo::TranslationsPath)).filePath(catalog)));
				QCoreApplication::installTranslator(&m_qtStartupTranslator);
			}
		}
		auto* config = ConfigManager::inst();
		QCOMPARE(config->aiCacheDir(), QDir(QCoreApplication::applicationDirPath()).filePath("cache") + '/');
		QFile isolatedConfig(m_config.filePath("ui-config.xml"));
		QVERIFY(isolatedConfig.open(QIODevice::WriteOnly));
		QDomDocument cacheConfig;
		auto cacheRoot = cacheConfig.createElement("lmms");
		cacheConfig.appendChild(cacheRoot);
		auto cachePaths = cacheConfig.createElement("paths");
		cachePaths.setAttribute("aicache", m_config.filePath("custom-ai-cache"));
		cacheRoot.appendChild(cachePaths);
		isolatedConfig.write(cacheConfig.toByteArray());
		isolatedConfig.close();
		config->loadConfigFile(isolatedConfig.fileName());
		QCOMPARE(config->aiCacheDir(), m_config.filePath("custom-ai-cache") + '/');
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
		const auto arguments = QCoreApplication::arguments();
		if (arguments.contains(QStringLiteral("nodeWorkflowRoot"))
			|| arguments.contains(QStringLiteral("nodeWorkflowRootRouteEditing"))
			|| arguments.contains(QStringLiteral("nodeWorkflowImportedPatternAndAutomation"))
			|| arguments.contains(QStringLiteral("nodeWorkflowTriggerLcd"))
			|| arguments.contains(QStringLiteral("nodeWorkflowDnBTriggerLcd"))
			|| arguments.contains(QStringLiteral("nodeWorkflowInstrumentLayers"))
			|| arguments.contains(QStringLiteral("nodeWorkflowAudioSignalThreshold"))
			|| arguments.contains(QStringLiteral("nodeWorkflowSongClipPreviews"))
			|| arguments.contains(QStringLiteral("nodeWorkflowLocalizedMenus"))
			|| arguments.contains(QStringLiteral("nodeWorkflowStandaloneStep"))
			|| arguments.contains(QStringLiteral("nodeWorkflowStandalonePianoRolls"))
			|| arguments.contains(QStringLiteral("nodeWorkflowTrackRename")))
		{
			config->setValue("audioengine", "mididev", MidiDummy::name());
			NotePlayHandleManager::init();
		}
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
	void nodeWorkflowLocalizedMenus()
	{
		const auto language = qEnvironmentVariable("LMMS_UI_TRANSLATION");
		const QHash<QString, QStringList> expected{
			{"en", {"New Node Workflow Project", "Root Graph", "Search Sessions", "Copy Node"}},
			{"zh_CN", {"新建节点工程", "根图", "搜索会话", "复制节点"}},
			{"ja", {"新規ノードプロジェクト", "ルートグラフ", "セッションを検索", "ノードをコピー"}},
			{"ko", {"새 노드 프로젝트", "루트 그래프", "세션 검색", "노드 복사"}},
		};
		QVERIFY(expected.contains(language));
		const auto strings = expected.value(language);
		auto* window = m_gui->mainWindow();
		const auto actions = window->findChildren<QAction*>();
		const auto create = std::find_if(actions.cbegin(), actions.cend(), [&strings](const auto* action) {
			return action->text() == strings.at(0);
		});
		QVERIFY(create != actions.cend());
		Engine::getSong()->setModified(false);
		(*create)->trigger();
		auto* canvas = window->findChild<QWidget*>("nodeWorkflowCanvas");
		QVERIFY(canvas);
		QTRY_VERIFY(canvas->isVisible());
		const auto tools = canvas->window()->findChild<QWidget*>("nodeWorkflowContent")->findChildren<QToolButton*>();
		QVERIFY(std::any_of(tools.cbegin(), tools.cend(), [&strings](const auto* button) {
			return button->accessibleName() == strings.at(1);
		}));
		QCOMPARE(QCoreApplication::translate("lmms::gui::NodeWorkflowCanvas", "Copy Node"), strings.at(3));
		capture(window, "node-workflow-localized-" + language);

		auto* inspector = window->findChild<QWidget*>("nodeWorkflowInspector");
		QVERIFY(inspector && inspector->isVisible());
		auto* inspectorTitle = inspector->findChild<QLabel*>("nodeWorkflowInspectorTitle");
		QVERIFY(inspectorTitle);
		QCOMPARE(inspectorTitle->text(), QCoreApplication::translate("lmms::gui::MainWindow", "Default Project Layout"));
		const auto inspectorActions = inspector->findChildren<QPushButton*>("nodeWorkflowInspectorAction");
		QCOMPARE(inspectorActions.size(), 2);
		QTest::mouseClick(inspectorActions.front(), Qt::LeftButton);
		QCOMPARE(inspectorTitle->text(), QCoreApplication::translate("lmms::gui::MainWindow", "Multitrack"));
		capture(window, "node-workflow-inspector-multitrack-" + language);
		const auto multitrackActions = inspector->findChildren<QPushButton*>("nodeWorkflowInspectorAction");
		const auto backToRoot = QCoreApplication::translate("lmms::gui::MainWindow", "Back to Root Graph");
		const auto back = std::find_if(multitrackActions.cbegin(), multitrackActions.cend(),
			[&backToRoot](const auto* button) { return button->isVisible() && button->text() == backToRoot; });
		QVERIFY(back != multitrackActions.cend());
		QTest::mouseClick(*back, Qt::LeftButton);
		QCOMPARE(inspectorTitle->text(), QCoreApplication::translate("lmms::gui::MainWindow", "Default Project Layout"));

		auto* dock = window->findChild<QWidget*>("agentChatDock");
		QVERIFY(dock);
		auto* showAgent = dock->findChild<QAction*>("showAgentChat");
		QVERIFY(showAgent);
		showAgent->trigger();
		auto* search = dock->findChild<QLineEdit*>("agentSessionSearch");
		QVERIFY(search);
		QCOMPARE(search->placeholderText(), strings.at(2));
		QTRY_VERIFY(dock->width() > 300);
		capture(window, "node-workflow-agent-localized-" + language);
	}

	void nodeWorkspaceConversion()
	{
		auto* window = m_gui->mainWindow();
		auto* song = Engine::getSong();
		const QString source = QDir::fromNativeSeparators(qEnvironmentVariable("LMMS_NODE_CONVERSION_SOURCE"));
		if (source.isEmpty())
		{
			QSKIP("Set LMMS_NODE_CONVERSION_SOURCE to the classic conversion fixture.");
		}
		QFile sourceFile(source);
		QVERIFY(sourceFile.open(QIODevice::ReadOnly));
		const auto originalBytes = sourceFile.readAll();
		sourceFile.close();

		QMenu* workspaceMode = nullptr;
		for (auto* menu : window->findChildren<QMenu*>())
		{
			if (menu->title() == QStringLiteral("Workspace Mode"))
			{
				workspaceMode = menu;
				break;
			}
		}
		QVERIFY(workspaceMode);
		QCOMPARE(workspaceMode->actions().size(), 2);
		auto* classicMode = workspaceMode->actions().at(0);
		auto* nodeMode = workspaceMode->actions().at(1);
		QVERIFY(classicMode->isChecked());

		FileBrowserTreeWidget browser(nullptr);
		browser.resize(420, 220);
		const QFileInfo sourceInfo(source);
		auto* sourceItem = new FileItem(&browser, sourceInfo.fileName(), sourceInfo.absolutePath());
		auto* compressedItem = new FileItem(&browser, "other.MMPZ", sourceInfo.absolutePath());
		auto* nodeItem = new FileItem(&browser, "other.mmpn", sourceInfo.absolutePath());
		browser.show();
		auto sidebarHasImport = [&browser](FileItem* item, bool activate = false) {
			bool found = false;
			QTimer popupTimer;
			popupTimer.setSingleShot(true);
			connect(&popupTimer, &QTimer::timeout, &browser, [&] {
				auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
				if (!menu)
				{
					return;
				}
				for (auto* action : menu->actions())
				{
					if (action->text() == QStringLiteral("Import as Node Project"))
					{
						found = true;
						if (activate)
						{
							menu->close();
							action->trigger();
						}
						break;
					}
				}
				menu->close();
			});
			const auto point = browser.visualItemRect(item).center();
			QContextMenuEvent event(QContextMenuEvent::Mouse, point, browser.viewport()->mapToGlobal(point));
			popupTimer.start(0);
			QApplication::sendEvent(browser.viewport(), &event);
			return found;
		};
		QVERIFY(!sidebarHasImport(sourceItem));

		song->loadProject(source);
		QCOMPARE(song->projectFileName(), source);
		QVERIFY(!song->isNodeProject());
		const bool wasModified = song->isModified();
		auto changeMode = [](QAction* action, const QString& destination) {
			bool foundDialog = false;
			QTimer timer;
			timer.setSingleShot(true);
			connect(&timer, &QTimer::timeout, [&] {
				auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
				if (!dialog)
				{
					if (auto* unexpected = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
					{
						unexpected->reject();
					}
					return;
				}
				foundDialog = true;
				if (destination.isEmpty())
				{
					dialog->reject();
				}
				else
				{
					dialog->selectFile(destination);
					static_cast<QDialog*>(dialog)->accept();
				}
			});
			timer.start(0);
			action->trigger();
			return foundDialog;
		};
		QVERIFY(changeMode(nodeMode, {}));
		QCOMPARE(song->projectFileName(), source);
		QCOMPARE(song->isModified(), wasModified);
		QVERIFY(!song->isNodeProject());

		QTemporaryDir copies(m_output + "/node-workspace-conversion-XXXXXX");
		QVERIFY(copies.isValid());
		const auto nodePath = copies.filePath("converted.mmpnz");
		const auto classicPath = copies.filePath("converted.mmp");
		QVERIFY(changeMode(nodeMode, nodePath));
		QCOMPARE(song->projectFileName(), nodePath);
		QVERIFY(song->isNodeProject());
		QVERIFY(QFile::exists(nodePath));
		auto* canvas = window->findChild<QWidget*>("nodeWorkflowCanvas");
		QVERIFY(canvas);
		QTRY_VERIFY(canvas->isVisible());
		capture(window, "node-workspace-converted-node");

		song->setNodeMasterOutputConnected(0, false);
		bool warnedAboutMaster = false;
		QTimer::singleShot(0, window, [&] {
			if (auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
			{
				warnedAboutMaster = warning->text().contains(QStringLiteral("Both Master output channels"));
				warning->done(QMessageBox::Cancel);
			}
		});
		classicMode->trigger();
		QVERIFY(warnedAboutMaster);
		QVERIFY(song->isNodeProject() && canvas->isVisible());
		QCOMPARE(song->projectFileName(), nodePath);
		QCOMPARE(Engine::mixer()->nodeMasterOutputMask(), 2u);
		song->setNodeMasterOutputConnected(0, true);

		const auto blockedPath = copies.filePath("blocked.mmp");
		QVERIFY(QDir().mkdir(blockedPath));
		bool reportedWriteFailure = false;
		QTimer::singleShot(0, window, [&] {
			if (auto* error = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
			{
				reportedWriteFailure = error->windowTitle() == QStringLiteral("Could not write file");
				error->done(QMessageBox::Ok);
			}
		});
		QVERIFY(!song->convertToClassicProject(blockedPath));
		QVERIFY(reportedWriteFailure);
		QVERIFY(QFileInfo(blockedPath).isDir());
		QVERIFY(song->isNodeProject() && canvas->isVisible());
		QCOMPARE(song->projectFileName(), nodePath);

		QVERIFY(changeMode(classicMode, classicPath));
		QCOMPARE(song->projectFileName(), classicPath);
		QVERIFY(!song->isNodeProject());
		QVERIFY(QFile::exists(classicPath));
		QVERIFY(!canvas->isVisible());
		QVERIFY(sourceFile.open(QIODevice::ReadOnly));
		QCOMPARE(sourceFile.readAll(), originalBytes);
		sourceFile.close();
		capture(window, "node-workspace-converted-classic");

		QAction* importAction = nullptr;
		for (auto* action : window->findChildren<QAction*>())
		{
			if (action->text() == QStringLiteral("Import Classic Project as Node Project…"))
			{
				importAction = action;
				break;
			}
		}
		QVERIFY(importAction);
		const auto importedPath = copies.filePath("imported.mmpn");
		int acceptedDialogs = 0;
		QPointer<QFileDialog> lastDialog;
		QTimer importTimer;
		connect(&importTimer, &QTimer::timeout, [&] {
			auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
			if (!dialog)
			{
				if (auto* unexpected = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
				{
					unexpected->reject();
				}
				return;
			}
			if (lastDialog == dialog)
			{
				dialog->reject();
				return;
			}
			lastDialog = dialog;
			const bool selectingSource = dialog->acceptMode() == QFileDialog::AcceptOpen;
			const QFileInfo path(selectingSource ? source : importedPath);
			dialog->setDirectory(path.absolutePath());
			if (auto* fileName = dialog->findChild<QLineEdit*>("fileNameEdit"))
			{
				fileName->setText(path.fileName());
			}
			connect(dialog, &QDialog::accepted, &importTimer, [&] { ++acceptedDialogs; });
			static_cast<QDialog*>(dialog)->accept();
		});
		importTimer.start(25);
		importAction->trigger();
		importTimer.stop();
		QCOMPARE(acceptedDialogs, 2);
		QVERIFY(song->isNodeProject());
		QCOMPARE(song->projectFileName(), importedPath);
		QVERIFY(QFile::exists(importedPath));
		QVERIFY(sourceFile.open(QIODevice::ReadOnly));
		QCOMPARE(sourceFile.readAll(), originalBytes);
		sourceFile.close();

		QVERIFY(sidebarHasImport(compressedItem));
		QVERIFY(!sidebarHasImport(nodeItem));
		acceptedDialogs = 0;
		lastDialog.clear();
		const auto sidebarPath = copies.filePath("sidebar-imported.mmpn");
		QTimer sidebarTimer;
		connect(&sidebarTimer, &QTimer::timeout, [&] {
			if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()))
			{
				if (dialog->acceptMode() != QFileDialog::AcceptSave)
				{
					dialog->reject();
					return;
				}
				const QFileInfo path(sidebarPath);
				dialog->setDirectory(path.absolutePath());
				if (auto* fileName = dialog->findChild<QLineEdit*>("fileNameEdit"))
				{
					fileName->setText(path.fileName());
				}
				++acceptedDialogs;
				static_cast<QDialog*>(dialog)->accept();
			}
			else if (auto* unexpected = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
			{
				unexpected->reject();
			}
		});
		sidebarTimer.start(25);
		const bool sidebarActionFound = sidebarHasImport(sourceItem, true);
		sidebarTimer.stop();
		QVERIFY(sidebarActionFound);
		QCOMPARE(acceptedDialogs, 1);
		QVERIFY(song->isNodeProject());
		QCOMPARE(song->projectFileName(), sidebarPath);
		QVERIFY(QFile::exists(sidebarPath));
		QVERIFY(sourceFile.open(QIODevice::ReadOnly));
		QCOMPARE(sourceFile.readAll(), originalBytes);
		sourceFile.close();
		const auto extendedScope = QStringLiteral("source/")
			+ song->tracks().front()->nodeWorkflowId().toString(QUuid::WithoutBraces) + '/';
		song->addNodeBoundaryPort(extendedScope, QStringLiteral("audio"), false);
		const auto extendedPath = copies.filePath("extended.mmpn");
		QVERIFY(song->saveProjectFile(extendedPath));
		const auto rejectedPath = copies.filePath("extended-classic.mmp");
		QVERIFY(!song->convertToClassicProject(rejectedPath));
		QVERIFY(!QFile::exists(rejectedPath));
		bool explainedImport = false;
		QTimer::singleShot(0, window, [&] {
			if (auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
			{
				explainedImport = warning->text().contains(QStringLiteral("cannot store nodes and routing"));
				warning->accept();
			}
		});
		QVERIFY(!window->canImportAsClassicProject(extendedPath));
		QVERIFY(explainedImport);
		QCOMPARE(song->projectFileName(), sidebarPath);
		copies.setAutoRemove(false);
		qInfo().noquote() << "Workspace conversion copies:" << nodePath << classicPath;
		qInfo().noquote() << "Imported classic project copy:" << importedPath;
	}

	void nodeWorkflowImportedPatternAndAutomation()
	{
		const auto fixture = qEnvironmentVariable("LMMS_NODE_PATTERN_AUTOMATION_SOURCE");
		if (fixture.isEmpty())
		{
			QSKIP("Set LMMS_NODE_PATTERN_AUTOMATION_SOURCE to the classic project.");
		}
		auto* song = Engine::getSong();
		song->loadProject(fixture);
		QCOMPARE(song->projectFileName(), fixture);
		QTemporaryDir copies(m_output + "/node-pattern-automation-XXXXXX");
		QVERIFY(copies.isValid());
		QVERIFY(song->convertToNodeProject(copies.filePath("converted.mmpn")));

		bool foundPattern = false;
		int automationCount = 0;
		for (auto* track : song->tracks())
		{
			if (track->name() != QStringLiteral("LB 302"))
			{
				continue;
			}
			if (track->type() == Track::Type::Automation)
			{
				++automationCount;
				QVERIFY(!track->getClips().empty());
			}
			else if (track->type() == Track::Type::Pattern)
			{
				foundPattern = true;
				QVERIFY(!song->nodeTrackMixerEndpoint(*track, 0));
				QVERIFY(!song->nodeTrackMixerEndpoint(*track, 1));
			}
		}
		QVERIFY(foundPattern);
		QCOMPARE(automationCount, 2);

		auto* window = m_gui->mainWindow();
		window->showNodeWorkflow();
		auto* play = window->findChild<QToolButton*>("nodeWorkflowPlay");
		auto* stop = window->findChild<QToolButton*>("nodeWorkflowStop");
		QVERIFY(play && stop);
		QTest::mouseClick(play, Qt::LeftButton);
		QVERIFY(song->isPlaying());
		QCOMPARE(song->playMode(), Song::PlayMode::Song);
		QVERIFY(stop->isEnabled());
		QTest::mouseClick(play, Qt::LeftButton);
		QVERIFY(song->isPaused());
		QTest::mouseClick(stop, Qt::LeftButton);
		QVERIFY(song->isStopped());
		auto* breadcrumb = window->findChild<QLabel*>("nodeWorkflowBreadcrumb");
		QVERIFY(breadcrumb);
		breadcrumb->linkActivated(QStringLiteral("song"));
		auto* canvas = window->findChild<QWidget*>("nodeWorkflowCanvas");
		QVERIFY(canvas);
		QTRY_VERIFY(canvas->isVisible() && !canvas->visibleRegion().isEmpty());
		capture(window, "node-workflow-imported-pattern-automation");
		breadcrumb->linkActivated(QStringLiteral("pattern"));
		capture(window, "node-workflow-pattern-audio-boundary");
	}

	void nodeWorkflowTrackRename()
	{
		auto* window = m_gui->mainWindow();
		auto* song = Engine::getSong();
		QAction* newNodeProject = nullptr;
		for (auto* action : window->findChildren<QAction*>())
		{
			if (action->text() == QStringLiteral("New Node Workflow Project"))
			{
				newNodeProject = action;
				break;
			}
		}
		QVERIFY(newNodeProject);
		song->setModified(false);
		newNodeProject->trigger();
		auto* canvas = window->findChild<QWidget*>("nodeWorkflowCanvas");
		QVERIFY(canvas);
		QTRY_VERIFY(canvas->isVisible() && !canvas->visibleRegion().isEmpty());
		QVERIFY(!song->tracks().empty());
		auto* track = song->tracks().front();
		QCOMPARE(track->type(), Track::Type::Instrument);
		const auto id = track->nodeWorkflowId();
		const auto left = song->nodeTrackMixerEndpoint(*track, 0);
		const auto right = song->nodeTrackMixerEndpoint(*track, 1);
		QVERIFY(left && right);
		capture(window, "node-workflow-track-rename-before");
		track->setName("Lead");
		capture(window, "node-workflow-track-rename-after");
		QCOMPARE(track->nodeWorkflowId(), id);
		const auto renamedLeft = song->nodeTrackMixerEndpoint(*track, 0);
		const auto renamedRight = song->nodeTrackMixerEndpoint(*track, 1);
		QVERIFY(renamedLeft && renamedRight);
		QCOMPARE(renamedLeft->channel, left->channel);
		QCOMPARE(renamedLeft->side, left->side);
		QCOMPARE(renamedRight->channel, right->channel);
		QCOMPARE(renamedRight->side, right->side);
		const auto audioPortColor = canvas->property("audioPortColor").value<QColor>();
		track->setColor(QColor("#F57C58"));
		capture(window, "node-workflow-track-color-root");
		QCOMPARE(canvas->property("audioPortColor").value<QColor>(), audioPortColor);

		QMenu* viewMenu = nullptr;
		for (auto* action : window->menuBar()->actions())
		{
			if (action->text() == QStringLiteral("&View"))
			{
				viewMenu = action->menu();
				break;
			}
		}
		QVERIFY(viewMenu);
		const auto visibilityAction = [viewMenu](const QString& label) -> QAction* {
			QMetaObject::invokeMethod(viewMenu, "aboutToShow");
			for (auto* action : viewMenu->actions())
			{
				if (action->text() == label)
				{
					return action;
				}
			}
			return nullptr;
		};
		auto* hide = visibilityAction("Hide Node Editor");
		QVERIFY(hide && hide->isEnabled());
		auto* toggleNodeEditor = window->findChild<QToolButton*>("nodeWorkflowToggleButton");
		QVERIFY(toggleNodeEditor && !toggleNodeEditor->icon().isNull());
		QCOMPARE(toggleNodeEditor->defaultAction(), hide);
		QTest::mouseClick(toggleNodeEditor, Qt::LeftButton);
		QVERIFY(!canvas->isVisible());
		auto* show = visibilityAction("Show Node Editor");
		QVERIFY(show && show->isEnabled());
		QCOMPARE(show, hide);
		show->trigger();
		QTRY_VERIFY(canvas->isVisible());
		QCOMPARE(window->findChild<QWidget*>("nodeWorkflowCanvas"), canvas);

		auto* breadcrumb = window->findChild<QLabel*>("nodeWorkflowBreadcrumb");
		auto* panel = window->findChild<QWidget*>("nodeWorkflowPatternPanel");
		QVERIFY(breadcrumb && panel);
		QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier,
			QPoint(canvas->width() * 0.16, canvas->height() * 0.67));
		QTRY_VERIFY(breadcrumb->text().contains(QStringLiteral(">Root Graph</a>")));
		QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier,
			QPoint(canvas->width() * 0.16, canvas->height() * 0.49));
		QTRY_VERIFY(panel->isVisible());
		QVERIFY(breadcrumb->text().contains(QStringLiteral("href=\"pattern\"")));
		auto* patternTrack = Engine::patternStore()->tracks().front();
		patternTrack->setName("Kick Lead");
		patternTrack->setColor(QColor("#F57C58"));
		QTRY_VERIFY(breadcrumb->text().endsWith(QStringLiteral("Kick Lead")));
		capture(window, "node-workflow-breadcrumb-pattern-member");
		auto* clip = static_cast<MidiClip*>(patternTrack->getClip(0));
		QVERIFY(clip);
		clip->setStep(0, true);
		for (auto* button : canvas->window()->findChild<QWidget*>("nodeWorkflowContent")->findChildren<QToolButton*>())
		{
			if (button->accessibleName() == QStringLiteral("Signal Flow"))
			{
				QTest::mouseClick(button, Qt::LeftButton);
				QVERIFY(button->isChecked());
				break;
			}
		}
		{
			auto* engine = Engine::audioEngine();
			bool available = false;
			engine->storeAudioDevice();
			engine->setAudioDevice(new UiManualAudioDevice(available, engine), false);
			const auto restoreAudio = qScopeGuard([engine] { engine->restoreAudioDevice(); });
			QVERIFY(available);
			QTest::qWait(230);
			song->playPattern();
			const auto stop = qScopeGuard([song] { song->stop(); });
			for (int period = 0; period < 16 && !Engine::mixer()->nodeMasterSignalActive(0); ++period)
			{
				engine->renderNextPeriod();
			}
			QVERIFY(Engine::mixer()->nodeMasterSignalActive(0));
			canvas->repaint();
			const auto screenshot = window->screen()->grabWindow(window->winId());
			QVERIFY(!screenshot.isNull());
			QVERIFY(screenshot.save(m_output + "/node-workflow-track-color-signal.png"));
		}

		const auto font = breadcrumb->fontMetrics();
		const auto patternLinkX = font.horizontalAdvance(QStringLiteral("Project  ›  "))
			+ font.horizontalAdvance(QStringLiteral("Pattern")) / 2;
		QTest::mouseClick(breadcrumb, Qt::LeftButton, Qt::NoModifier,
			QPoint(patternLinkX, breadcrumb->height() / 2));
		QTRY_VERIFY(!panel->isVisible());
		QVERIFY(breadcrumb->text().contains(QStringLiteral(">Root Graph</a>")));
		QTest::mouseClick(breadcrumb, Qt::LeftButton, Qt::NoModifier,
			QPoint(font.horizontalAdvance(QStringLiteral("Project")) / 2, breadcrumb->height() / 2));
		QTRY_VERIFY(breadcrumb->text().endsWith(QStringLiteral("Root Graph")));
	}

	void nodeWorkflowStandaloneStep()
	{
		auto* window = m_gui->mainWindow();
		auto* song = Engine::getSong();
		QAction* createProject = nullptr;
		for (auto* action : window->findChildren<QAction*>())
		{
			if (action->text() == QStringLiteral("New Node Workflow Project"))
			{
				createProject = action;
				break;
			}
		}
		QVERIFY(createProject);
		song->setModified(false);
		createProject->trigger();
		auto* canvas = window->findChild<QWidget*>("nodeWorkflowCanvas");
		QVERIFY(canvas);
		QTRY_VERIFY(canvas->isVisible());
		QToolButton* add = nullptr;
		for (auto* button : canvas->window()->findChild<QWidget*>("nodeWorkflowContent")->findChildren<QToolButton*>())
		{
			if (button->accessibleName() == QStringLiteral("Add Node"))
			{
				add = button;
				break;
			}
		}
		QVERIFY(add);
		const auto songTracks = song->tracks().size();
		auto* patterns = Engine::patternStore();
		const auto patternTracks = patterns->tracks().size();

		QTimer::singleShot(0, canvas, [] {
			if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()))
			{
				for (auto* action : menu->actions())
				{
					if (action->text() == QStringLiteral("Empty Step Sequencer"))
					{
						menu->setActiveAction(action);
						QTest::keyClick(menu, Qt::Key_Return);
						return;
					}
				}
				menu->close();
			}
		});
		QTest::mouseClick(add, Qt::LeftButton);
		QCOMPARE(song->nodeStepPatterns().size(), 1);
		QCOMPARE(song->tracks().size(), songTracks);
		QCOMPARE(patterns->tracks().size(), patternTracks);
		QCOMPARE(song->nodeStepPatterns().front().activeSteps, 0u);
		QVERIFY(song->nodeStepPatterns().front().targets.isEmpty());
		capture(window, "node-workflow-standalone-step-empty");

		const QRectF graph(-260, 30, 1160, 297);
		const auto scale = std::min({(canvas->width() - 80) / graph.width(),
			(canvas->height() - 80) / graph.height(), qreal{1.5}});
		const auto canvasPoint = [canvas, graph, scale](const QPointF& world) {
			return (QPointF(canvas->width(), canvas->height()) / 2 + (world - graph.center()) * scale).toPoint();
		};
		QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(QPointF(-156, 54)));
		auto* inspectorTitle = window->findChild<QLabel*>("nodeWorkflowInspectorTitle");
		QVERIFY(inspectorTitle);
		QCOMPARE(inspectorTitle->text(), QStringLiteral("Step Sequencer"));
		auto* inspector = inspectorTitle->parentWidget();
		const auto values = inspector->findChildren<QLabel*>("nodeWorkflowInspectorValue");
		QTRY_VERIFY(std::any_of(values.cbegin(), values.cend(), [](const auto* label) {
			return label->isVisible() && label->text() == QStringLiteral("0 / 16");
		}));
		bool edited = false;
		QTimer::singleShot(0, canvas, [&] {
			if (auto* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
			{
				const auto buttons = editor->findChildren<QToolButton*>();
				if (buttons.size() == Song::NodeStepPattern::StepCount
					&& std::none_of(buttons.cbegin(), buttons.cend(), [](const auto* button) { return button->isChecked(); }))
				{
					QTest::mouseClick(buttons.front(), Qt::LeftButton);
					edited = buttons.front()->isChecked();
				}
				editor->accept();
			}
		});
		QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(QPointF(-156, 54)));
		QVERIFY(edited);
		QCOMPARE(song->nodeStepPatterns().front().activeSteps, 1u);

		const auto output = canvasPoint(QPointF(-55, 80));
		const auto input = canvasPoint(QPointF(20, 271));
		QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, output);
		QTest::mouseMove(canvas, input);
		QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, input);
		QVERIFY(!patterns->tracks().empty());
		const auto target = patterns->tracks().front()->nodeWorkflowId().toString(QUuid::WithoutBraces);
		QCOMPARE(song->nodeStepPatterns().front().targets, QStringList{target});
		capture(window, "node-workflow-standalone-step-connected");

		QTemporaryDir copies;
		QVERIFY(copies.isValid());
		for (const auto& suffix : {QStringLiteral(".mmpn"), QStringLiteral(".mmpnz")})
		{
			const auto path = copies.filePath("standalone" + suffix);
			QVERIFY(song->saveProjectFile(path));
			const DataFile copy(path);
			const auto step = copy.nodeWorkflow().firstChildElement("step-pattern");
			QCOMPARE(step.attribute("activeSteps"), QStringLiteral("1"));
			QCOMPARE(step.firstChildElement("target").attribute("track"), target);
		}
	}

	void nodeWorkflowStandalonePianoRolls()
	{
		auto* window = m_gui->mainWindow();
		auto* song = Engine::getSong();
		const auto actions = window->findChildren<QAction*>();
		const auto create = std::find_if(actions.cbegin(), actions.cend(), [](const auto* action) {
			return action->text() == QStringLiteral("New Node Workflow Project");
		});
		QVERIFY(create != actions.cend());
		song->setModified(false);
		(*create)->trigger();
		auto* canvas = window->findChild<QWidget*>("nodeWorkflowCanvas");
		QVERIFY(canvas);
		QTRY_VERIFY(canvas->isVisible());
		const auto buttons = canvas->window()->findChild<QWidget*>("nodeWorkflowContent")->findChildren<QToolButton*>();
		const auto add = std::find_if(buttons.cbegin(), buttons.cend(), [](const auto* button) {
			return button->accessibleName() == QStringLiteral("Add Node");
		});
		QVERIFY(add != buttons.cend());
		const auto trackCount = song->tracks().size();
		const auto patternTrackCount = Engine::patternStore()->tracks().size();
		for (const auto& name : {QStringLiteral("Empty MIDI Piano Roll"), QStringLiteral("Empty SVS Piano Roll")})
		{
			QTimer::singleShot(0, canvas, [name] {
				if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()))
				{
					const auto entries = menu->actions();
					const auto entry = std::find_if(entries.cbegin(), entries.cend(), [&name](const auto* action) {
						return action->text() == name;
					});
					if (entry != entries.cend())
					{
						menu->setActiveAction(*entry);
						QTest::keyClick(menu, Qt::Key_Return);
					}
					else
					{
						menu->close();
					}
				}
			});
			QTest::mouseClick(*add, Qt::LeftButton);
		}
		const auto rolls = song->nodePianoRolls();
		QCOMPARE(rolls.size(), 2);
		QVERIFY(!rolls.front().singingVoice && rolls.back().singingVoice);
		QCOMPARE(song->tracks().size(), trackCount);
		QCOMPARE(Engine::patternStore()->tracks().size(), patternTrackCount);
		capture(window, "node-workflow-standalone-piano-rolls");

		const QFontMetricsF portMetrics(canvas->font());
		const auto trackName = song->tracks().front()->name();
		const auto trackWidth = std::ceil(portMetrics.horizontalAdvance(trackName + QStringLiteral(" · MIDI")))
			+ std::ceil(portMetrics.horizontalAdvance(trackName + QStringLiteral(" L"))) + 46;
		const QRectF graph(-260, 30, 1160 + std::max(qreal{0}, trackWidth - 205), 297);
		const auto scale = std::min({(canvas->width() - 80) / graph.width(),
			(canvas->height() - 80) / graph.height(), qreal{1.5}});
		const auto canvasPoint = [canvas, graph, scale](const QPointF& world) {
			return (QPointF(canvas->width(), canvas->height()) / 2 + (world - graph.center()) * scale).toPoint();
		};
		QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(QPointF(-160, 54)));
		auto* midi = dynamic_cast<MidiClip*>(song->nodePianoRollClip(rolls.front().id));
		QVERIFY(midi && midi->notes().empty());
		QVERIFY(!midi->instrumentTrack()->instrument());
		QCOMPARE(m_gui->pianoRoll()->currentMidiClip(), midi);
		QVERIFY(m_gui->pianoRoll()->isVisible());
		midi->addNote(Note(TimePos(24), TimePos(0)));
		m_gui->pianoRoll()->parentWidget()->hide();
		const auto midiOutput = canvasPoint(QPointF(-55, 80));
		const auto instrumentInput = canvasPoint(QPointF(20, 78));
		QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, midiOutput);
		QTest::mouseMove(canvas, instrumentInput);
		QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, instrumentInput);
		const auto instrumentId = song->tracks().front()->nodeWorkflowId().toString(QUuid::WithoutBraces);
		QCOMPARE(song->nodePianoRolls().front().targets, QStringList{instrumentId});

		QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(QPointF(-160, 176)));
		auto* voice = dynamic_cast<SVSClip*>(song->nodePianoRollClip(rolls.back().id));
		QVERIFY(voice && voice->notes().isEmpty());
		const auto* voiceTrack = static_cast<SVSTrack*>(voice->getTrack());
		QVERIFY(voiceTrack->pluginId().isEmpty() && voiceTrack->voiceId().isEmpty());
		const auto voiceEditors = window->findChildren<SVSPianoRoll*>();
		QVERIFY(std::any_of(voiceEditors.cbegin(), voiceEditors.cend(), [](const auto* editor) {
			return editor->isVisible();
		}));
		svs::Note voiceNote;
		voiceNote.id = QStringLiteral("standalone-voice-note");
		voiceNote.duration = 48;
		voiceNote.pitch = 60;
		voiceNote.lyric = QStringLiteral("啦");
		voice->setNotes({voiceNote});
		QCOMPARE(song->tracks().size(), trackCount);
		QCOMPARE(Engine::patternStore()->tracks().size(), patternTrackCount);
		for (auto* editor : voiceEditors)
		{
			if (editor->isVisible())
			{
				editor->parentWidget()->hide();
			}
		}
		auto* destination = new SVSTrack(song);
		destination->setName(QStringLiteral("Voice target"));
		QCoreApplication::processEvents();
		const auto voiceOutput = canvasPoint(QPointF(-55, 202));
		qreal voiceRow = 78;
		for (auto* track : song->tracks())
		{
			if (track == destination)
			{
				break;
			}
			if (track->type() == Track::Type::Instrument || track->type() == Track::Type::Sample
				|| track->type() == Track::Type::SVS || track->type() == Track::Type::SVC)
			{
				voiceRow += 44;
			}
		}
		const auto voiceInput = canvasPoint(QPointF(20, voiceRow));
		const auto drag = [canvas](const QPoint& source, const QPoint& target) {
			QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, source);
			QTest::mouseMove(canvas, target);
			QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, target);
		};
		drag(voiceOutput, instrumentInput);
		QVERIFY(song->nodePianoRolls().back().targets.isEmpty());
		drag(voiceOutput, voiceInput);
		const auto voiceTrackId = destination->nodeWorkflowId().toString(QUuid::WithoutBraces);
		QCOMPARE(song->nodePianoRolls().back().targets, QStringList{voiceTrackId});
		QCOMPARE(destination->numOfClips(), 0);
		capture(window, "node-workflow-standalone-svs-connected");
		QTemporaryDir copies;
		QVERIFY(copies.isValid());
		for (const auto& suffix : {QStringLiteral(".mmpn"), QStringLiteral(".mmpnz")})
		{
			const auto path = copies.filePath("piano-rolls" + suffix);
			QVERIFY(song->saveProjectFile(path));
			const DataFile copy(path);
			const auto savedMidi = copy.nodeWorkflow().firstChildElement("piano-roll");
			QVERIFY(!savedMidi.firstChildElement("midiclip").firstChildElement("note").isNull());
			QCOMPARE(savedMidi.firstChildElement("target").attribute("track"), instrumentId);
			const auto savedSvs = savedMidi.nextSiblingElement("piano-roll");
			QCOMPARE(savedSvs.attribute("kind"), QStringLiteral("svs"));
			QCOMPARE(savedSvs.firstChildElement("target").attribute("track"), voiceTrackId);
			QVERIFY(!savedSvs.firstChildElement("svsclip").firstChildElement("notes")
				.firstChildElement("note").isNull());
		}
		QTimer::singleShot(0, canvas, [] {
			if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()))
			{
				if (menu->actions().size() == 1)
				{
					menu->setActiveAction(menu->actions().front());
					QTest::keyClick(menu, Qt::Key_Return);
				}
				else
				{
					menu->close();
				}
			}
		});
		QTest::mouseClick(canvas, Qt::RightButton, Qt::NoModifier, voiceOutput);
		QVERIFY(song->nodePianoRolls().back().targets.isEmpty());
		QCOMPARE(song->nodePianoRolls().front().targets, QStringList{instrumentId});

		const auto selectNodeAction = [canvas](const QPoint& point, const QString& label, bool confirm = false) {
			const auto selected = std::make_shared<bool>(false);
			QTimer::singleShot(0, canvas, [canvas, label, confirm, selected] {
				auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
				if (!menu)
				{
					return;
				}
				for (auto* action : menu->actions())
				{
					if (action->text() != label || !action->isEnabled())
					{
						continue;
					}
					if (confirm)
					{
						QTimer::singleShot(0, canvas, [] {
							if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
							{
								if (auto* yes = dialog->button(QMessageBox::Yes))
								{
									yes->click();
								}
							}
						});
					}
					menu->setActiveAction(action);
					*selected = true;
					QTest::keyClick(menu, Qt::Key_Return);
					return;
				}
				menu->close();
			});
			QTest::mouseClick(canvas, Qt::RightButton, Qt::NoModifier, point);
			return *selected;
		};
		const auto midiNode = canvasPoint(QPointF(-160, 54));
		QVERIFY(selectNodeAction(midiNode, QStringLiteral("Open Piano Roll")));
		QVERIFY(m_gui->pianoRoll()->isVisible());
		QCOMPARE(m_gui->pianoRoll()->currentMidiClip(), midi);
		m_gui->pianoRoll()->parentWidget()->hide();
		QVERIFY(!song->copyStandaloneNode(QStringLiteral("piano-roll"), rolls.front().id).isNull());
		QVERIFY(selectNodeAction(midiNode, QStringLiteral("Copy Node")));
		QVERIFY(selectNodeAction(midiNode, QStringLiteral("Paste Node")));
		const auto withCopy = song->nodePianoRolls();
		QCOMPARE(withCopy.size(), 3);
		QVERIFY(withCopy.back().id != rolls.front().id && !withCopy.back().singingVoice);
		QVERIFY(withCopy.back().targets.isEmpty());
		auto* midiCopy = dynamic_cast<MidiClip*>(song->nodePianoRollClip(withCopy.back().id));
		QVERIFY(midiCopy && midiCopy->notes().size() == midi->notes().size());
		QVERIFY(midiCopy->id() != midi->id());
		QVERIFY(selectNodeAction(midiNode, QStringLiteral("Delete Node"), true));
		const auto afterDelete = song->nodePianoRolls();
		QCOMPARE(afterDelete.size(), 2);
		QCOMPARE(afterDelete.back().id, withCopy.back().id);
		QVERIFY(afterDelete.front().singingVoice);
	}

	void nodeWorkflowSongClipPreviews()
	{
		auto* window = m_gui->mainWindow();
		const auto actions = window->findChildren<QAction*>();
		const auto create = std::find_if(actions.cbegin(), actions.cend(), [this](const auto* action) {
			return action->text() == tr("New Node Workflow Project");
		});
		QVERIFY(create != actions.cend());
		auto* song = Engine::getSong();
		song->setModified(false);
		(*create)->trigger();

		auto* midi = static_cast<MidiClip*>(song->tracks().front()->createClip(TimePos{}));
		auto* voiceTrack = Track::create(Track::Type::SVS, song);
		QVERIFY(voiceTrack);
		auto* voice = static_cast<SVSClip*>(voiceTrack->createClip(TimePos{}));
		auto* editor = m_gui->songEditor();
		auto* frame = qobject_cast<QMdiSubWindow*>(editor->parentWidget());
		QVERIFY(frame);
		frame->show();
		window->workspace()->setActiveSubWindow(frame);
		const auto findMidiView = [editor, midi]() -> MidiClipView* {
			for (auto* view : editor->findChildren<MidiClipView*>())
			{
				if (view->getMidiClip() == midi) { return view; }
			}
			return nullptr;
		};
		const auto findVoiceView = [editor, voice]() -> SVSClipView* {
			for (auto* view : editor->findChildren<SVSClipView*>())
			{
				if (view->getClip() == voice) { return view; }
			}
			return nullptr;
		};
		QTRY_VERIFY(findMidiView());
		QTRY_VERIFY(findVoiceView());
		auto* midiView = findMidiView();
		auto* voiceView = findVoiceView();
		QTRY_VERIFY(!midiView->visibleRegion().isEmpty() && !voiceView->visibleRegion().isEmpty());
		const auto renderedClip = [window](QWidget* clipView) {
			const auto rect = clipView->visibleRegion().boundingRect();
			const auto origin = clipView->mapTo(window, rect.topLeft());
			return window->screen()->grabWindow(window->winId(), origin.x(), origin.y(),
				rect.width(), rect.height()).toImage();
		};
		const auto emptyMidi = renderedClip(midiView);
		const auto emptyVoice = renderedClip(voiceView);
		QVERIFY(!emptyMidi.isNull() && !emptyVoice.isNull());

		midi->addNote(Note(TimePos(48), TimePos(0)));
		svs::Note voiceNote;
		voiceNote.id = QStringLiteral("song-preview-note");
		voiceNote.duration = 48;
		voiceNote.pitch = 60;
		voiceNote.lyric = QStringLiteral("啦");
		voice->setNotes({voiceNote});
		QTRY_VERIFY(renderedClip(midiView) != emptyMidi);
		QTRY_VERIFY(renderedClip(voiceView) != emptyVoice);
		capture(window, "node-workflow-song-clip-previews");

		QTest::mouseDClick(midiView, Qt::LeftButton, Qt::NoModifier, midiView->rect().center());
		QCOMPARE(m_gui->pianoRoll()->currentMidiClip(), midi);
		m_gui->pianoRoll()->parentWidget()->hide();
		window->workspace()->setActiveSubWindow(frame);
		QTest::mouseDClick(voiceView, Qt::LeftButton, Qt::NoModifier, voiceView->rect().center());
		const auto voiceEditors = window->findChildren<SVSPianoRoll*>();
		QVERIFY(std::any_of(voiceEditors.cbegin(), voiceEditors.cend(),
			[](const auto* roll) { return roll->isVisible(); }));
	}

	void nodeWorkflowRootRouteEditing()
	{
		auto* window = m_gui->mainWindow();
		const auto label = tr("New Node Workflow Project");
		QAction* newNodeProject = nullptr;
		for (auto* action : window->findChildren<QAction*>())
		{
			if (action->text() == label)
			{
				newNodeProject = action;
				break;
			}
		}
		QVERIFY(newNodeProject);
		// Startup can mark the isolated, disposable project dirty.
		Engine::getSong()->setModified(false);
		newNodeProject->trigger();
		auto* canvas = window->findChild<QWidget*>("nodeWorkflowCanvas");
		QVERIFY(canvas);
		QTRY_VERIFY(canvas->isVisible() && !canvas->visibleRegion().isEmpty());
		auto* rootTrack = Engine::getSong()->tracks().front();
		const QRectF rootBounds(20, 30, 880, 297);
		const auto rootScale = std::min({(canvas->width() - 80) / rootBounds.width(),
			(canvas->height() - 80) / rootBounds.height(), qreal{1.5}});
		const auto rootPoint = [canvas, rootBounds, rootScale](const QPointF& point) {
			return (QPointF(canvas->width(), canvas->height()) / 2
				+ (point - rootBounds.center()) * rootScale).toPoint();
		};
		QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, rootPoint(QPointF(85, 55)));
		auto* inspectorTitle = window->findChild<QLabel*>("nodeWorkflowInspectorTitle");
		QVERIFY(inspectorTitle);
		QCOMPARE(inspectorTitle->text(), QStringLiteral("Multitrack"));
		capture(window, "node-workflow-root-selected");
		QTimer::singleShot(0, canvas, [] {
			if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()))
			{
				menu->setActiveAction(menu->actions().front());
				QTest::keyClick(menu, Qt::Key_Return);
			}
		});
		const auto rootOutput = rootPoint(QPointF(225, 78));
		QTest::mouseClick(canvas, Qt::RightButton, Qt::NoModifier, rootOutput);
		QVERIFY(!Engine::getSong()->nodeTrackMixerEndpoint(*rootTrack, 0));
		QVERIFY(Engine::getSong()->nodeTrackMixerEndpoint(*rootTrack, 1));
		capture(window, "node-workflow-root-left-disconnected");
		const auto rootInput = rootPoint(QPointF(395, 132));
		QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, rootOutput);
		QTest::mouseMove(canvas, rootInput);
		QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, rootInput);
		const auto leftRoute = Engine::getSong()->nodeTrackMixerEndpoint(*rootTrack, 0);
		QVERIFY(leftRoute && leftRoute->channel == 0 && leftRoute->side == 0);
		QVERIFY(Engine::getSong()->nodeTrackMixerEndpoint(*rootTrack, 1));
		capture(window, "node-workflow-root-left-reconnected");
		const auto dragWire = [canvas](const QPoint& from, const QPoint& to) {
			QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, from);
			QTest::mouseMove(canvas, to);
			QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, to);
		};
		const auto wireMiddle = (rootOutput + rootInput) / 2;
		QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, wireMiddle);
		QVERIFY(Engine::getSong()->nodeTrackMixerEndpoint(*rootTrack, 0));
		dragWire(wireMiddle, rootPoint(QPointF(310, 30)));
		QVERIFY(!Engine::getSong()->nodeTrackMixerEndpoint(*rootTrack, 0));
		QVERIFY(Engine::getSong()->nodeTrackMixerEndpoint(*rootTrack, 1));
		dragWire(rootOutput, rootInput);
		QVERIFY(Engine::getSong()->nodeTrackMixerEndpoint(*rootTrack, 0));
		const auto masterSource = rootPoint(QPointF(600, 133));
		const auto masterTarget = rootPoint(QPointF(750, 133));
		dragWire(masterTarget, rootPoint(QPointF(675, 75)));
		QCOMPARE(Engine::mixer()->nodeMasterOutputMask(), 2u);
		dragWire(masterSource, masterTarget);
		QCOMPARE(Engine::mixer()->nodeMasterOutputMask(), 3u);
		const auto nodeBody = rootPoint(QPointF(85, 55));
		const QPoint dragOffset(45, -32);
		QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, nodeBody);
		QTest::mouseMove(canvas, nodeBody + dragOffset);
		QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, nodeBody + dragOffset);
		capture(window, "node-workflow-root-node-dragged");
		bool movedPortHit = false;
		QTimer::singleShot(0, canvas, [&] {
			if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()))
			{
				movedPortHit = menu->actions().size() == 1
					&& menu->actions().front()->text() == QStringLiteral("Disconnect");
				menu->close();
			}
		});
		QTest::mouseClick(canvas, Qt::RightButton, Qt::NoModifier, rootOutput + dragOffset);
		QVERIFY(movedPortHit);
		auto* song = Engine::getSong();
		const auto moved = song->nodeLayoutPosition(QStringLiteral("root/song"));
		QVERIFY(moved);
		QVERIFY(song->classicConversionChanges().contains(
			tr("Custom node positions will be omitted.")));
		QVERIFY(QLineF(*moved, QPointF(20, 30) + QPointF(dragOffset) / rootScale).length() < 1);
		auto* breadcrumb = window->findChild<QLabel*>("nodeWorkflowBreadcrumb");
		QVERIFY(breadcrumb);
		breadcrumb->linkActivated(QStringLiteral("song"));
		QCOMPARE(inspectorTitle->text(), QStringLiteral("Multitrack"));
		QVERIFY(song->nodeTrackBoundaryConnected(*rootTrack, 0));
		QVERIFY(song->setNodeTrackBoundaryConnected(*rootTrack, 0, false));
		QVERIFY(!song->nodeTrackBoundaryConnected(*rootTrack, 0));
		QVERIFY(song->nodeTrackBoundaryConnected(*rootTrack, 1));
		const auto parentRoute = song->nodeTrackMixerEndpoint(*rootTrack, 0);
		QVERIFY(parentRoute);
		QCOMPARE(parentRoute->channel, leftRoute->channel);
		QCOMPARE(parentRoute->side, leftRoute->side);
		QVERIFY(song->setNodeTrackBoundaryConnected(*rootTrack, 0, true));
		const auto trackKey = QStringLiteral("song/track/") + rootTrack->nodeWorkflowId().toString(QUuid::WithoutBraces);
		const QRectF subgraphBounds(QPointF(35, 40), QPointF(990, 263));
		const auto subgraphScale = std::min({(canvas->width() - 80) / subgraphBounds.width(),
			(canvas->height() - 80) / subgraphBounds.height(), qreal{1.5}});
		const auto trackBody = (QPointF(canvas->width(), canvas->height()) / 2
			+ (QPointF(75, 57) - subgraphBounds.center()) * subgraphScale).toPoint();
		const auto movedTrackBody = trackBody + QPoint(56, 34);
		QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, trackBody);
		QTest::mouseMove(canvas, movedTrackBody);
		QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, movedTrackBody);
		const auto movedTrack = song->nodeLayoutPosition(trackKey);
		QVERIFY(movedTrack);
		breadcrumb->linkActivated(QStringLiteral("root"));
		QCOMPARE(song->nodeLayoutPosition(QStringLiteral("root/song")), moved);
		const auto restoredBounds = rootBounds.united(QRectF(*moved, QSizeF(205, 100)));
		const auto restoredScale = std::min({(canvas->width() - 80) / restoredBounds.width(),
			(canvas->height() - 80) / restoredBounds.height(), qreal{1.5}});
		const auto restoredNode = (QPointF(canvas->width(), canvas->height()) / 2
			+ (*moved + QPointF(65, 25) - restoredBounds.center()) * restoredScale).toPoint();
		QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, restoredNode);
		QCOMPARE(inspectorTitle->text(), QStringLiteral("Multitrack"));
		capture(window, "node-workflow-root-layout-restored");
		breadcrumb->linkActivated(QStringLiteral("song"));
		QCOMPARE(song->nodeLayoutPosition(trackKey), movedTrack);
		capture(window, "node-workflow-song-layout-restored");
		auto* aggregate = window->findChild<QToolButton*>("nodeWorkflowAggregation");
		QVERIFY(aggregate && aggregate->menu());
		auto* merge = aggregate->menu()->findChild<QAction*>("nodeWorkflowAggregationEnabled");
		auto* threshold = aggregate->menu()->findChild<QSpinBox*>("nodeWorkflowAggregationThreshold");
		QVERIFY(merge && threshold);
		merge->setChecked(true);
		threshold->setValue(7);
		QTemporaryDir copies;
		QVERIFY(copies.isValid());
		for (const auto& extension : {QStringLiteral(".mmpn"), QStringLiteral(".mmpnz")})
		{
			const auto path = copies.filePath(QStringLiteral("layout") + extension);
			QVERIFY(song->saveProjectFile(path));
			const DataFile data(path);
			QCOMPARE(data.nodeWorkflow().attribute("aggregationEnabled"), QStringLiteral("1"));
			QCOMPARE(data.nodeWorkflow().attribute("aggregationThreshold"), QStringLiteral("7"));
			const auto position = data.nodeWorkflow().firstChildElement("node-position");
			QCOMPARE(position.attribute("key"), QStringLiteral("root/song"));
			QCOMPARE(QPointF(position.attribute("x").toDouble(), position.attribute("y").toDouble()), *moved);
		}
		const auto saved = copies.filePath(QStringLiteral("layout.mmpnz"));
		song->loadProject(saved);
		QVERIFY(song->isNodeProject());
		const auto aggregation = song->nodeConnectionAggregation();
		QVERIFY(aggregation);
		QVERIFY(aggregation->first);
		QCOMPARE(aggregation->second, 7);
		QCOMPARE(song->nodeLayoutPosition(QStringLiteral("root/song")), moved);
		QCOMPARE(song->nodeLayoutPosition(trackKey), movedTrack);
	}

	void nodeWorkflowInstrumentLayers()
	{
		auto* window = m_gui->mainWindow();
		auto* song = Engine::getSong();
		QAction* newProject = nullptr;
		for (auto* action : window->findChildren<QAction*>())
		{
			if (action->text() == QStringLiteral("New Node Workflow Project"))
			{
				newProject = action;
				break;
			}
		}
		QVERIFY(newProject);
		song->setModified(false);
		newProject->trigger();
		auto* breadcrumb = window->findChild<QLabel*>("nodeWorkflowBreadcrumb");
		auto* canvas = window->findChild<QWidget*>("nodeWorkflowCanvas");
		QVERIFY(breadcrumb && canvas);
		breadcrumb->linkActivated(QStringLiteral("pattern"));
		QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier,
			QPoint(canvas->width() * 0.16, canvas->height() * 0.49));
		auto* owner = static_cast<InstrumentTrack*>(Engine::patternStore()->tracks().front());
		QTRY_VERIFY(breadcrumb->text().contains(owner->name()));
		ConfigManager::inst()->setValue("ui", "hideextendedsourcewarning", "1");
		auto* add = canvas->window()->findChild<QToolButton*>("nodeWorkflowAdd");
		if (!add)
		{
			for (auto* button : canvas->window()->findChildren<QToolButton*>())
			{
				if (button->accessibleName() == QStringLiteral("Add Node"))
				{
					add = button;
					break;
				}
			}
		}
		QVERIFY(add);
		QTimer::singleShot(0, add, [] {
			auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
			if (!menu)
			{
				return;
			}
			for (auto* action : menu->actions())
			{
				if (action->text() == QStringLiteral("Instrument Plugins") && action->menu())
				{
					for (auto* plugin : action->menu()->actions())
					{
						if (plugin->text() == QStringLiteral("Kicker"))
						{
							menu->close();
							plugin->trigger();
							return;
						}
					}
				}
			}
			menu->close();
		});
		QTest::mouseClick(add, Qt::LeftButton);
		QCOMPARE(song->nodeInstrumentLayers(*owner).size(), 1);
		auto* first = song->nodeInstrumentLayers(*owner).front().instrument;
		auto* second = song->addNodeInstrumentLayer(*owner, QStringLiteral("kicker"), false);
		QVERIFY(first && second && first != second);
		first->setVolume(75);
		second->setVolume(45);
		const auto firstId = song->nodeInstrumentLayers(*owner).front().id;
		const auto secondId = song->nodeInstrumentLayers(*owner).back().id;
		QVERIFY(song->setNodeInstrumentLayerEvent(*owner, firstId, true, true));
		QVERIFY(song->setNodeInstrumentLayerEvent(*owner, secondId, true, true));
		QVERIFY(song->setNodeInstrumentLayerOutput(*owner, firstId, 0, 0));
		QVERIFY(song->setNodeInstrumentLayerOutput(*owner, secondId, 1, 1));
		QVERIFY(song->setNodeSourceAudioOutput(*owner, 0, std::nullopt));
		QVERIFY(song->setNodeSourceAudioOutput(*owner, 1, std::nullopt));
		static_cast<MidiClip*>(owner->getClip(0))->setStep(0, true);

		auto* engine = Engine::audioEngine();
		bool available = false;
		engine->storeAudioDevice();
		engine->setAudioDevice(new UiManualAudioDevice(available, engine), false);
		auto* signalFlow = window->findChild<QToolButton*>("nodeWorkflowSignalFlow");
		QVERIFY(signalFlow && signalFlow->isChecked() && NodeSignalActivity::monitoring());
		const auto cleanup = qScopeGuard([song, engine] {
			song->stop();
			engine->restoreAudioDevice();
		});
		QVERIFY(available);
		song->playPattern();
		for (int period = 0; period < 16 && !Engine::mixer()->nodeMasterSignalActive(0); ++period)
		{
			engine->renderNextPeriod();
		}
		QVERIFY(Engine::mixer()->nodeMasterSignalActive(0));
		QVERIFY(Engine::mixer()->nodeMasterSignalActive(1));
		QVERIFY(song->setNodeInstrumentLayerOutput(*owner, secondId, 1, std::nullopt));
		engine->renderNextPeriod();
		QVERIFY(Engine::mixer()->nodeMasterSignalActive(0));
		QVERIFY(!Engine::mixer()->nodeMasterSignalActive(1));
		QVERIFY(song->setNodeInstrumentLayerOutput(*owner, secondId, 1, 1));

		QTemporaryDir fixture(m_output + "/node-layers-XXXXXX");
		QVERIFY(fixture.isValid());
		const auto fileName = fixture.filePath("layers.mmpn");
		QVERIFY(song->saveProjectFile(fileName));
		const DataFile saved(fileName);
		const auto firstElement = saved.nodeWorkflow().firstChildElement("instrument-layer");
		const auto secondElement = firstElement.nextSiblingElement("instrument-layer");
		QCOMPARE(firstElement.attribute("id"), firstId);
		QCOMPARE(secondElement.attribute("id"), secondId);
		QCOMPARE(firstElement.attribute("steps"), QStringLiteral("1"));
		QCOMPARE(secondElement.attribute("steps"), QStringLiteral("1"));
		QCOMPARE(firstElement.attribute("left"), QStringLiteral("0"));
		QCOMPARE(secondElement.attribute("right"), QStringLiteral("1"));
		QCOMPARE(firstElement.firstChildElement("track").firstChildElement("instrumenttrack").attribute("vol"),
			QStringLiteral("75"));
		QCOMPARE(secondElement.firstChildElement("track").firstChildElement("instrumenttrack").attribute("vol"),
			QStringLiteral("45"));
		breadcrumb->linkActivated(QStringLiteral("pattern"));
		QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier,
			QPoint(canvas->width() * 0.16, canvas->height() * 0.49));
		capture(window, "node-workflow-instrument-layers");
	}

	void nodeWorkflowExplicitStereoBoundary()
	{
		auto* song = Engine::getSong();
		song->createNodeProject();
		auto* window = m_gui->mainWindow();
		window->showNodeWorkflow();
		auto* editor = window->findChild<QMdiSubWindow*>("nodeWorkflowEditor");
		QVERIFY(editor);
		auto* aggregate = editor->findChild<QToolButton*>("nodeWorkflowAggregation");
		auto* signal = editor->findChild<QToolButton*>("nodeWorkflowSignalFlow");
		auto* breadcrumb = editor->findChild<QLabel*>("nodeWorkflowBreadcrumb");
		auto* canvas = editor->findChild<QWidget*>("nodeWorkflowCanvas");
		QVERIFY(aggregate && signal && breadcrumb && canvas);
		QCOMPARE(signal->popupMode(), aggregate->popupMode());
		const auto buttons = canvas->window()->findChildren<QToolButton*>();
		const auto add = std::find_if(buttons.cbegin(), buttons.cend(), [](const auto* button) {
			return button->accessibleName() == QCoreApplication::translate("lmms::gui::MainWindow", "Add Node");
		});
		QVERIFY(add != buttons.cend());
		QStringList entries;
		const auto inspectMenu = [&] {
			entries.clear();
			QTimer::singleShot(0, canvas, [&] {
				if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()))
				{
					for (auto* action : menu->actions())
					{
						if (!action->isSeparator())
						{
							entries.append(action->text());
						}
					}
					menu->close();
				}
			});
			QTest::mouseClick(*add, Qt::LeftButton);
		};
		const auto label = [](const char* text) {
			return QCoreApplication::translate("lmms::gui::MainWindow", text);
		};
		inspectMenu();
		QVERIFY(entries.contains(label("Effect Plugin…")));
		QVERIFY(!entries.contains(label("Empty Step Sequencer")));
		QVERIFY(!entries.contains(label("Empty MIDI Piano Roll")));
		QVERIFY(!entries.contains(label("Empty SVS Piano Roll")));
		breadcrumb->linkActivated(QStringLiteral("pattern"));
		inspectMenu();
		QVERIFY(entries.contains(label("Output Port")));
		QVERIFY(!entries.contains(label("Add Step Subgraph")));
		QVERIFY(!entries.contains(label("Instrument Plugins")));
		breadcrumb->linkActivated(QStringLiteral("song"));
		inspectMenu();
		QVERIFY(entries.contains(label("Instrument Plugins")));
		QVERIFY(entries.contains(label("SVS Voices")));
		QVERIFY(entries.contains(label("SVC Models")));
		QVERIFY(!entries.contains(label("Add Instrument Subgraph")));
		QVERIFY(aggregate->menu());
		auto* pcb = aggregate->menu()->findChild<QAction*>("nodeWorkflowPcbBusRouting");
		QVERIFY(pcb && pcb->isCheckable() && pcb->isChecked());
		pcb->setChecked(false);
		QCOMPARE(song->nodePcbBusRouting(), std::optional<bool>{false});
		pcb->setChecked(true);
		QVERIFY(song->nodePcbBusRouting().value_or(false));
		QTemporaryDir savedGraph;
		QVERIFY(savedGraph.isValid());
		const auto savedPath = savedGraph.filePath(QStringLiteral("pcb.mmpn"));
		QVERIFY(song->saveProjectFile(savedPath));
		QCOMPARE(DataFile(savedPath).nodeWorkflow().attribute("pcbBusRouting"), QStringLiteral("1"));

		auto* midiOwner = song->tracks().front();
		QCOMPARE(midiOwner->type(), Track::Type::Instrument);
		const auto midiScope = QStringLiteral("source/")
			+ midiOwner->nodeWorkflowId().toString(QUuid::WithoutBraces) + '/';
		breadcrumb->linkActivated(QStringLiteral("song"));
		const auto graphPoint = [canvas](const QRectF& bounds, const QPointF& point) {
			const auto scale = std::min({(canvas->width() - 80) / bounds.width(),
				(canvas->height() - 80) / bounds.height(), qreal{1.5}});
			return (QPointF(canvas->width(), canvas->height()) / 2 + (point - bounds.center()) * scale).toPoint();
		};
		QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier,
			graphPoint(QRectF(QPointF(35, 40), QPointF(990, 263)), QPointF(75, 57)));
		QTRY_VERIFY(breadcrumb->text().contains(midiOwner->name()));
		const auto midiNode = graphPoint(QRectF(QPointF(20, 80), QPointF(845, 192)), QPointF(100, 105));
		const auto selectNodeAction = [canvas, midiNode](const char* label) {
			bool selected = false;
			QTimer chooseAction;
			chooseAction.setSingleShot(true);
			connect(&chooseAction, &QTimer::timeout, canvas, [&] {
				auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
				if (!menu)
				{
					return;
				}
				for (auto* action : menu->actions())
				{
					if (action->text() == QCoreApplication::translate("lmms::gui::NodeWorkflowCanvas", label)
						&& action->isEnabled())
					{
						selected = true;
						menu->setActiveAction(action);
						QTest::keyClick(menu, Qt::Key_Return);
						return;
					}
				}
				menu->close();
			});
			chooseAction.start(0);
			QTest::mouseClick(canvas, Qt::RightButton, Qt::NoModifier, midiNode);
			return selected;
		};
		QVERIFY(selectNodeAction("Copy Node"));
		QVERIFY(selectNodeAction("Paste Node"));
		const auto copiedMidi = song->nodeBoundaryPorts(midiScope);
		QCOMPARE(copiedMidi.size(), 1);
		QCOMPARE(copiedMidi.front().signal, QStringLiteral("midi"));
		QVERIFY(!copiedMidi.front().output && copiedMidi.front().track.isEmpty());
		song->removeNodeBoundaryPort(midiScope, copiedMidi.front().id);

		const auto scope = QStringLiteral("pattern/");
		QVERIFY(song->nodeBoundaryPorts(scope).isEmpty());
		const auto stereo = song->createNodeBoundaryPort(scope, QStringLiteral("audio"), true, true);
		QVERIFY(!stereo.isEmpty());
		const auto ports = song->nodeBoundaryPorts(scope);
		QCOMPARE(ports.size(), 2);
		QCOMPARE(ports.at(0).stereoGroup, stereo);
		QCOMPARE(ports.at(1).stereoGroup, stereo);
		QCOMPARE(ports.at(0).stereoSide, 0);
		QCOMPARE(ports.at(1).stereoSide, 1);
		QVERIFY(ports.at(0).id != ports.at(1).id);
		QVERIFY(ports.at(0).output && ports.at(1).output);
		song->removeNodeBoundaryPort(scope, stereo);
		QVERIFY(song->nodeBoundaryPorts(scope).isEmpty());

		const auto parent = song->addNodeCustomGraph(scope, QStringLiteral("Container"));
		QVERIFY(!parent.isEmpty());
		const auto parentScope = QStringLiteral("custom/") + parent + '/';
		const auto child = song->addNodeCustomGraph(parentScope, QStringLiteral("Child"));
		QVERIFY(!child.isEmpty());
		const auto childScope = QStringLiteral("custom/") + child + '/';
		QVERIFY(song->renameNodeCustomGraph(child, QStringLiteral("Nested")));
		QCOMPARE(song->nodeCustomGraphs(parentScope).front().name, QStringLiteral("Nested"));
		const auto input = song->createNodeBoundaryPort(parentScope, QStringLiteral("midi"), false, false);
		const auto childInput = song->createNodeBoundaryPort(childScope, QStringLiteral("midi"), false, false);
		QVERIFY(song->setNodeCustomConnection(parentScope, input, childInput, true));
		QVERIFY(song->renameNodeBoundaryPort(childScope, childInput, QStringLiteral("Notes")));
		QCOMPARE(song->nodeCustomConnections(parentScope).front().destination, childInput);
		QCOMPARE(song->nodeBoundaryPorts(childScope).front().name, QStringLiteral("Notes"));
		const auto graphCopy = song->pasteNodeCustomGraph(scope, song->copyNodeCustomGraph(parent));
		QVERIFY(!graphCopy.isEmpty() && graphCopy != parent);
		const auto copiedScope = QStringLiteral("custom/") + graphCopy + '/';
		QCOMPARE(song->nodeCustomGraphs(copiedScope).size(), 1);
		const auto copiedChild = song->nodeCustomGraphs(copiedScope).front().id;
		const auto copiedPorts = song->nodeBoundaryPorts(QStringLiteral("custom/") + copiedChild + '/');
		QCOMPARE(copiedPorts.size(), 1);
		QCOMPARE(copiedPorts.front().name, QStringLiteral("Notes"));
		QVERIFY(copiedPorts.front().id != childInput);
		QCOMPARE(song->nodeCustomConnections(copiedScope).size(), 1);
		QCOMPARE(song->nodeCustomConnections(copiedScope).front().destination, copiedPorts.front().id);
		song->removeNodeBoundaryPort(childScope, childInput);
		QVERIFY(song->nodeCustomConnections(parentScope).isEmpty());

		const auto root = QStringLiteral("root/");
		const auto instrumentGraph = song->addNodeCustomGraph(root, QStringLiteral("Audio fan-out"));
		QVERIFY(!instrumentGraph.isEmpty());
		const auto instrumentScope = QStringLiteral("custom/") + instrumentGraph + '/';
		const auto instrumentId = song->addNodeCustomInstrument(instrumentScope, QStringLiteral("kicker"), {});
		QVERIFY(!instrumentId.isEmpty());
		auto* original = song->nodeCustomInstruments(instrumentScope).front().second;
		original->setVolume(75);
		const auto copiedState = song->copyNodeInstrumentLayer(*original, {});
		const auto copiedId = song->pasteNodeCustomInstrument(instrumentScope, copiedState);
		QVERIFY(!copiedId.isEmpty() && copiedId != instrumentId);
		auto* copy = song->nodeCustomInstruments(instrumentScope).back().second;
		QVERIFY(copy != original);
		QCOMPARE(copy->instrumentName(), original->instrumentName());
		QCOMPARE(copy->volumeModel()->value(), original->volumeModel()->value());
		const auto midiIn = song->createNodeBoundaryPort(instrumentScope, QStringLiteral("midi"), false, false);
		const auto audioOut = song->createNodeBoundaryPort(instrumentScope, QStringLiteral("audio"), true, false);
		song->addNodeLogicalInput(QStringLiteral("midi"));
		const auto inputId = song->nodeLogicalInputs().back().id;
		const auto instrumentEndpoint = QStringLiteral("instrument/") + instrumentId + '/';
		auto* mixer = Engine::mixer();
		const auto masterEndpoint = QStringLiteral("mixer/")
			+ mixer->mixerChannel(0)->nodeWorkflowId().toString(QUuid::WithoutBraces);
		QVERIFY(song->setNodeCustomConnection(root, QStringLiteral("logical-midi/") + inputId, midiIn, true));
		QVERIFY(song->setNodeCustomConnection(instrumentScope, midiIn, instrumentEndpoint + "midi", true));
		QVERIFY(song->setNodeCustomConnection(instrumentScope, instrumentEndpoint + "out-L", audioOut, true));
		QVERIFY(song->setNodeCustomConnection(root, audioOut, masterEndpoint + "/in-L", true));
		QVERIFY(song->setNodeCustomConnection(root, audioOut, masterEndpoint + "/in-R", true));

		const auto copiedInstrumentGraph = song->pasteNodeCustomGraph(root, song->copyNodeCustomGraph(instrumentGraph));
		QVERIFY(!copiedInstrumentGraph.isEmpty() && copiedInstrumentGraph != instrumentGraph);
		const auto copiedInstrumentScope = QStringLiteral("custom/") + copiedInstrumentGraph + '/';
		QCOMPARE(song->nodeCustomInstruments(copiedInstrumentScope).size(), 2);
		QCOMPARE(song->nodeCustomConnections(copiedInstrumentScope).size(), 2);
		QCOMPARE(song->nodeCustomConnections(root).size(), 3);

		{
			auto* engine = Engine::audioEngine();
			bool available = false;
			engine->storeAudioDevice();
			engine->setAudioDevice(new UiManualAudioDevice(available, engine), false);
			const auto restoreAudio = qScopeGuard([engine] { engine->restoreAudioDevice(); });
			QVERIFY(available && NodeSignalActivity::monitoring());
			const MidiEvent note(MidiNoteOn, 0, 60, 100, nullptr, MidiEvent::Source::Internal);
			QCOMPARE(song->dispatchNodeMidiInput(inputId, note, TimePos{}, 0), 1);
			for (int period = 0; period < 16 && !mixer->nodeMasterSignalActive(0); ++period)
			{
				engine->renderNextPeriod();
			}
			QVERIFY(mixer->nodeMasterSignalActive(0));
			QVERIFY(mixer->nodeMasterSignalActive(1));
			QVERIFY(song->setNodeCustomConnection(root, audioOut, masterEndpoint + "/in-R", false));
			engine->renderNextPeriod();
			QVERIFY(mixer->nodeMasterSignalActive(0));
			QVERIFY(!mixer->nodeMasterSignalActive(1));

			QVERIFY(song->setNodeCustomConnection(root, audioOut, masterEndpoint + "/in-L", false));
			auto* owner = Engine::patternStore()->tracks().front();
			const auto ownerId = owner->nodeWorkflowId().toString(QUuid::WithoutBraces);
			const auto trackScope = QStringLiteral("source/") + ownerId + '/';
			const auto trackOutput = QStringLiteral("track-output/") + ownerId + QStringLiteral("/in-L");
			const auto snapshot = song->copyNodeCustomGraph(instrumentGraph);
			const auto addBranch = [&] {
				const auto id = song->pasteNodeCustomGraph(trackScope, snapshot);
				const auto scope = QStringLiteral("custom/") + id + '/';
				const auto ports = song->nodeBoundaryPorts(scope);
				const auto port = std::find_if(ports.cbegin(), ports.cend(), [](const auto& item) { return item.output; });
				if (port == ports.cend() || !song->setNodeCustomConnection(trackScope, port->id, trackOutput, true))
				{
					return QString{};
				}
				song->nodeCustomInstruments(scope).front().second->processInEvent(note, TimePos{}, 0);
				return port->id;
			};
			const auto firstBranch = addBranch();
			const auto secondBranch = addBranch();
			QVERIFY(!firstBranch.isEmpty() && !secondBranch.isEmpty());
			QCOMPARE(song->nodeCustomConnections(trackScope).size(), 2);
			engine->renderNextPeriod();
			QVERIFY(mixer->nodeMasterSignalActive(0));
			QVERIFY(!mixer->nodeMasterSignalActive(1));
			QVERIFY(song->setNodeCustomConnection(trackScope, firstBranch, trackOutput, false));
			QCOMPARE(song->nodeCustomConnections(trackScope).size(), 1);
			QCOMPARE(song->nodeCustomConnections(trackScope).front().source, secondBranch);
			engine->renderNextPeriod();
			QVERIFY(mixer->nodeMasterSignalActive(0));
		}

		const auto firstMidiIn = song->createNodeBoundaryPort(midiScope, QStringLiteral("midi"), false, false);
		const auto secondMidiIn = song->createNodeBoundaryPort(midiScope, QStringLiteral("midi"), false, false);
		song->addNodeLogicalInput(QStringLiteral("midi"));
		const auto firstInputId = song->nodeLogicalInputs().back().id;
		song->addNodeLogicalInput(QStringLiteral("midi"));
		const auto secondInputId = song->nodeLogicalInputs().back().id;
		QVERIFY(song->setNodeSourceMidiBoundary(firstMidiIn, *midiOwner));
		QVERIFY(song->setNodeSourceMidiBoundary(secondMidiIn, *midiOwner));
		QVERIFY(song->setNodeLogicalInputTarget(firstInputId, QStringLiteral("midi"),
			*midiOwner, QStringLiteral("midi-in/") + firstMidiIn, true));
		QVERIFY(song->setNodeLogicalInputTarget(secondInputId, QStringLiteral("midi"),
			*midiOwner, QStringLiteral("midi-in/") + secondMidiIn, true));
		const MidiEvent note(MidiNoteOn, 0, 60, 100, nullptr, MidiEvent::Source::Internal);
		QCOMPARE(song->dispatchNodeMidiInput(firstInputId, note, TimePos{}, 0), 1);
		QCOMPARE(song->dispatchNodeMidiInput(secondInputId, note, TimePos{}, 0), 1);
		QVERIFY(song->nodeSourceConnections(*midiOwner).defaultMidiInput);
		QVERIFY(song->removeDefaultNodeSourceMidiInput(*midiOwner));
		QVERIFY(!song->nodeSourceConnections(*midiOwner).defaultMidiInput);
		QCOMPARE(song->dispatchNodeMidiInput(secondInputId, note, TimePos{}, 0), 1);
		QVERIFY(song->setNodeSourceMidiBoundary(firstMidiIn, *midiOwner, false));
		QCOMPARE(song->dispatchNodeMidiInput(firstInputId, note, TimePos{}, 0), 0);
		QCOMPARE(song->dispatchNodeMidiInput(secondInputId, note, TimePos{}, 0), 1);
		QVERIFY(song->addNodeInstrumentLayer(*midiOwner, QStringLiteral("kicker"), false));
		QCOMPARE(song->nodeInstrumentLayers(*midiOwner).size(), 1);
		const auto songGraph = song->addNodeCustomGraph(midiScope, QStringLiteral("Song instrument routing"));
		QVERIFY(!songGraph.isEmpty());
		const auto songGraphScope = QStringLiteral("custom/") + songGraph + '/';
		QVERIFY(!song->addNodeCustomInstrument(songGraphScope, QStringLiteral("kicker")).isEmpty());

		auto* sample = Track::create(Track::Type::Sample, Engine::patternStore());
		const auto sampleScope = QStringLiteral("source/") + sample->nodeWorkflowId().toString(QUuid::WithoutBraces) + '/';
		const auto sampleGraph = song->addNodeCustomGraph(sampleScope, QStringLiteral("Sample routing"));
		QVERIFY(!sampleGraph.isEmpty());
		const auto sampleGraphScope = QStringLiteral("custom/") + sampleGraph + '/';
		const auto nestedGraph = song->addNodeCustomGraph(sampleGraphScope, QStringLiteral("Nested sample routing"));
		QVERIFY(!nestedGraph.isEmpty());
		const auto nestedScope = QStringLiteral("custom/") + nestedGraph + '/';
		QVERIFY(song->addNodeCustomInstrument(nestedScope, QStringLiteral("kicker")).isEmpty());
		const auto snapshot = song->copyNodeCustomGraph(instrumentGraph);
		QVERIFY(!snapshot.isNull());
		QVERIFY(song->pasteNodeCustomGraph(nestedScope, snapshot).isEmpty());
		QVERIFY(song->nodeCustomGraphs(nestedScope).isEmpty());

		auto* voice = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		QVERIFY(voice);
		const auto voiceScope = QStringLiteral("source/") + voice->nodeWorkflowId().toString(QUuid::WithoutBraces) + '/';
		QVERIFY(song->addNodeCustomGraph(voiceScope, QStringLiteral("Not allowed")).isEmpty());
		QVERIFY(!song->createNodeBoundaryPort(voiceScope, QStringLiteral("midi"), false, false).isEmpty());
		const std::unique_ptr<QDialog> settings(createSVSPluginSettings(voice, window, true));
		settings->show();
		QVERIFY(QTest::qWaitForWindowExposed(settings.get()));
		auto* speakers = settings->findChild<QComboBox*>("svsPluginSpeaker");
		QVERIFY(speakers);
		if (speakers->currentIndex() >= 0)
		{
			QVERIFY(speakers->currentData().toString().contains('\n'));
		}
		for (int index = 0; index < speakers->count(); ++index)
		{
			QVERIFY(speakers->itemData(index).toString().contains('\n'));
		}
	}

	void nodeWorkflowTriggerLcd()
	{
		auto* window = m_gui->mainWindow();
		auto* song = Engine::getSong();
		song->setModified(false);
		song->createNodeProject();
		window->showNodeWorkflow();
		auto* currentEditor = window->findChild<QMdiSubWindow*>("nodeWorkflowEditor");
		QVERIFY(currentEditor);
		auto* canvas = currentEditor->findChild<QWidget*>("nodeWorkflowCanvas");
		auto* lcd = currentEditor->findChild<QScrollArea*>("nodeWorkflowTriggerLcd");
		auto* breadcrumb = currentEditor->findChild<QLabel*>("nodeWorkflowBreadcrumb");
		QVERIFY(canvas && lcd && breadcrumb);
		QVERIFY(!lcd->isVisible());
		breadcrumb->linkActivated(QStringLiteral("pattern"));
		QVERIFY(lcd->isVisible());
		capture(window, "node-trigger-lcd-entry");
		QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier,
			QPoint(canvas->width() * 0.14, canvas->height() * 0.45));
		auto* stepPanel = currentEditor->findChild<QWidget*>("nodeWorkflowPatternPanel");
		QTRY_VERIFY(stepPanel && stepPanel->isVisible());
		QVERIFY(lcd->isVisible());
		QCOMPARE(lcd->height(), 148);

		auto* patterns = Engine::patternStore();
		auto* instrument = patterns->tracks().front();
		auto* stepClip = static_cast<MidiClip*>(instrument->getClip(0));
		stepClip->setSteps(48);
		stepClip->setStep(4, true);
		stepClip->setStep(36, true);
		const auto rows = [lcd] {
			QList<QLabel*> labels;
			for (auto* label : lcd->findChildren<QLabel*>())
			{
				if (label->property("nodeWorkflowTriggerLcdRow").toBool())
				{
					labels.append(label);
				}
			}
			return labels;
		};
		const auto selectCable = [canvas, lcd](qreal xBegin, qreal xEnd, qreal yBegin, qreal yEnd) {
			for (qreal y = yBegin; y <= yEnd; y += 0.01)
			{
				for (qreal x = xBegin; x <= xEnd; x += 0.01)
				{
					QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
						QPoint(qRound(canvas->width() * x), qRound(canvas->height() * y)));
					if (lcd->property("wireSelected").toBool())
					{
						return true;
					}
				}
			}
			return false;
		};

		auto* engine = Engine::audioEngine();
		bool available = false;
		engine->storeAudioDevice();
		engine->setAudioDevice(new UiManualAudioDevice(available, engine), false);
		const auto restoreAudio = qScopeGuard([song, engine] {
			if (!song->isStopped())
			{
				song->stop();
			}
			engine->restoreAudioDevice();
		});
		QVERIFY(available);
		song->playPattern();
		QVERIFY(!lcd->property("wireSelected").toBool());
		QVERIFY(lcd->widget());
		QCOMPARE(lcd->widget()->objectName(), QStringLiteral("nodeWorkflowTriggerLcdIdle"));
		(void) patterns->play(TimePos::stepPosition(3), engine->framesPerPeriod(), 0, 0);
		QTest::qWait(100);
		QVERIFY(rows().isEmpty());
		QVERIFY(patterns->play(TimePos::stepPosition(4), engine->framesPerPeriod(), 0, 0));
		QTRY_COMPARE(rows().size(), 1);
		auto* strip = lcd->findChild<QWidget*>("nodeWorkflowTriggerStepStrip");
		QVERIFY(strip);
		QCOMPARE(strip->geometry().right() + 1, strip->parentWidget()->width());
		QVERIFY(!strip->findChild<QScrollBar*>());
		QVERIFY(rows().front()->isVisible());
		QVERIFY(rows().front()->width() > 0);
		QTRY_COMPARE(strip->property("stepIndex").toInt(), 4);
		QTest::qWait(2100);
		QCOMPARE(strip->property("stepIndex").toInt(), 4);
		capture(window, "node-trigger-lcd-step");

		(void) patterns->play(TimePos::stepPosition(35), engine->framesPerPeriod(), 0, 0);
		QTRY_COMPARE(strip->property("stepIndex").toInt(), 35);
		QVERIFY(patterns->play(TimePos::stepPosition(36), engine->framesPerPeriod(), 0, 0));
		QTRY_COMPARE(strip->property("stepIndex").toInt(), 36);
		QCOMPARE(lcd->height(), 148);
		capture(window, "node-trigger-lcd-step-scrolled");

		QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
		QTRY_VERIFY(!lcd->property("wireSelected").toBool());
		song->stop();
		QTRY_COMPARE(lcd->widget()->objectName(), QStringLiteral("nodeWorkflowTriggerLcdIdle"));
		QCOMPARE(lcd->height(), 148);
		capture(window, "node-trigger-lcd-idle");

		breadcrumb->linkActivated(QStringLiteral("pattern"));
		QVERIFY(lcd->isVisible());
		QList<PatternTrack*> columns;
		for (int index = 0; index < 8; ++index)
		{
			auto* column = static_cast<PatternTrack*>(Track::create(Track::Type::Pattern, song));
			column->setName(QStringLiteral("Chord %1 – long Pattern name").arg(index + 1));
			columns.append(column);
			auto* notes = static_cast<MidiClip*>(instrument->getClip(column->patternIndex()));
			notes->setClipType(MidiClip::Type::MelodyClip);
			for (int key = 48; key <= 72; key += 2)
			{
				notes->addNote(Note(TimePos(24), TimePos(0), key));
			}
		}
		auto* sampleTrack = Track::create(Track::Type::Sample, patterns);
		sampleTrack->setName(QStringLiteral("LCD sample"));
		const auto samplePath = qEnvironmentVariable("LMMS_DATA_DIR")
			+ QStringLiteral("/samples/beats/rave_kick01 - 180 BPM.ogg");
		QVERIFY(QFile::exists(samplePath));
		static_cast<SampleClip*>(sampleTrack->getClip(columns.front()->patternIndex()))->setSampleFile(samplePath);
		patterns->setCurrentPattern(columns.front()->patternIndex());
		breadcrumb->linkActivated(QStringLiteral("pattern"));
		const auto memberPoint = [canvas](int y) {
			const QRectF bounds(QPointF(35, 40), QPointF(990, 263));
			const auto scale = std::min({(canvas->width() - 80) / bounds.width(),
				(canvas->height() - 80) / bounds.height(), qreal{1.5}});
			return (QPointF(canvas->width(), canvas->height()) / 2
				+ (QPointF(75, y) - bounds.center()) * scale).toPoint();
		};
		QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, memberPoint(58));
		QTRY_VERIFY(breadcrumb->text().contains(instrument->name()));
		QCoreApplication::processEvents();
		QVERIFY(selectCable(0.46, 0.55, 0.46, 0.51));
		song->playPattern();
		for (auto* column : columns)
		{
			QVERIFY(patterns->play(TimePos(0), engine->framesPerPeriod(), 0, column->patternIndex()));
		}
		QTRY_COMPARE(rows().size(), columns.size());
		QTRY_VERIFY(lcd->verticalScrollBar()->isVisible());
		auto* chordRow = rows().front();
		auto* noteField = chordRow->parentWidget()->findChild<QLabel*>("nodeWorkflowTriggerLcdDetail");
		QVERIFY(noteField);
		QVERIFY(noteField->text().startsWith(QStringLiteral("C3 / ")));
		QVERIFY(noteField->text().endsWith(QStringLiteral("C5")));
		QCOMPARE(noteField->text().split(QStringLiteral(" / ")).size(), 13);
		QCOMPARE(noteField->width(), 53);
		QTRY_VERIFY(noteField->property("lcdScrollOffset").toInt() > 0);
		QVERIFY(noteField->toolTip().isEmpty());
		QTRY_VERIFY(chordRow->property("lcdScrollOffset").toInt() > 0);
		QCOMPARE(lcd->findChildren<QWidget*>("nodeWorkflowTriggerNotePreview").size(), columns.size());
		auto* preview = chordRow->parentWidget()->findChild<QWidget*>("nodeWorkflowTriggerNotePreview");
		QVERIFY(preview);
		QCOMPARE(preview->geometry().right() + 1, preview->parentWidget()->width());
		(void) patterns->play(TimePos(96), engine->framesPerPeriod(), 0, columns.front()->patternIndex());
		QTRY_COMPARE(preview->property("playheadPosition").toDouble(), 0.5);
		QVERIFY(lcd->verticalScrollBar()->maximum() > 0);
		const auto lcdRows = rows();
		const auto rowPosition = [lcd](const QLabel* label, const QPoint& point) {
			return lcd->viewport()->mapFromGlobal(label->mapToGlobal(point));
		};
		QVERIFY(lcd->viewport()->rect().contains(rowPosition(lcdRows.at(5), lcdRows.at(5)->rect().bottomLeft())));
		QVERIFY(!lcd->viewport()->rect().contains(rowPosition(lcdRows.at(6), QPoint{})));
		lcd->verticalScrollBar()->setValue(lcd->verticalScrollBar()->maximum());
		QVERIFY(lcd->viewport()->rect().contains(rowPosition(lcdRows.back(), lcdRows.back()->rect().bottomLeft())));
		lcd->verticalScrollBar()->setValue(0);
		QCOMPARE(lcd->height(), 148);
		capture(window, "node-trigger-lcd-notes");

		breadcrumb->linkActivated(QStringLiteral("pattern"));
		QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, memberPoint(178));
		QTRY_VERIFY(breadcrumb->text().contains(sampleTrack->name()));
		QVERIFY(selectCable(0.61, 0.70, 0.46, 0.61));
		auto* sampleClip = static_cast<SampleClip*>(sampleTrack->getClip(columns.front()->patternIndex()));
		sampleClip->setIsPlaying(false);
		QVERIFY(patterns->play(TimePos(0), engine->framesPerPeriod(), 0, columns.front()->patternIndex()));
		QTRY_COMPARE(rows().size(), 1);
		auto* sampleField = rows().front()->parentWidget()->findChild<QLabel*>("nodeWorkflowTriggerLcdDetail");
		QVERIFY(sampleField);
		QCOMPARE(sampleField->text(), QFileInfo(samplePath).fileName());
		QCOMPARE(sampleField->geometry().right() + 1, sampleField->parentWidget()->width());
		QVERIFY(sampleField->toolTip().isEmpty());
		QCOMPARE(lcd->height(), 148);
		capture(window, "node-trigger-lcd-sample");
		breadcrumb->linkActivated(QStringLiteral("pattern"));
		QVERIFY(lcd->isVisible());
		QVERIFY(!lcd->property("wireSelected").toBool());
		sampleClip->setIsPlaying(false);
		QVERIFY(patterns->play(TimePos(0), engine->framesPerPeriod(), 0, columns.front()->patternIndex()));
		QVERIFY(patterns->play(TimePos::stepPosition(4), engine->framesPerPeriod(), 0, 0));
		QTRY_COMPARE(rows().size(), 3);
		QStringList sources;
		for (const auto* row : rows())
		{
			sources.append(row->text());
		}
		QVERIFY(sources.contains(columns.front()->name() + QStringLiteral(" · ") + instrument->name()));
		QVERIFY(sources.contains(columns.front()->name() + QStringLiteral(" · ") + sampleTrack->name()));
		QVERIFY(lcd->findChild<QWidget*>("nodeWorkflowTriggerStepStrip"));
		QCOMPARE(lcd->height(), 148);
		capture(window, "node-trigger-lcd-pattern");

		auto* signalFlow = currentEditor->findChild<QToolButton*>("nodeWorkflowSignalFlow");
		QVERIFY(signalFlow && signalFlow->menu());
		auto* hideDelay = signalFlow->menu()->findChild<QSpinBox*>("nodeWorkflowTriggerHideDelay");
		QVERIFY(hideDelay);
		QCOMPARE(hideDelay->value(), 1000);
		QCOMPARE(hideDelay->minimum(), 0);
		QCOMPARE(hideDelay->maximum(), 30000);
		hideDelay->setValue(5000);
		columns.back()->setMuted(true);
		patterns->setCurrentPattern(columns.back()->patternIndex());
		const auto samplePeriods = qCeil(static_cast<double>(sampleClip->sample().sampleSize())
			* engine->outputSampleRate() / sampleClip->sample().sampleRate() / engine->framesPerPeriod());
		for (int period = 0; period < samplePeriods + 64; ++period)
		{
			engine->renderNextPeriod();
		}
		QVERIFY(!song->isStopped());
		QTest::qWait(100);
		QVERIFY(!rows().isEmpty());
		hideDelay->setValue(0);
		QTRY_VERIFY(rows().isEmpty());
		QVERIFY(lcd->isVisible());
		QVERIFY(!lcd->verticalScrollBar()->isVisible());
		QCOMPARE(lcd->height(), 148);
		QCOMPARE(lcd->widget()->objectName(), QStringLiteral("nodeWorkflowTriggerLcdIdle"));
		hideDelay->setValue(1000);
		breadcrumb->linkActivated(QStringLiteral("root"));
		QVERIFY(!lcd->isVisible());
	}

	void nodeWorkflowDnBTriggerLcd()
	{
		auto* song = Engine::getSong();
		const auto fixture = QDir::fromNativeSeparators(qEnvironmentVariable("LMMS_DATA_DIR"))
			+ QStringLiteral("/projects/demos/DnB.mmpz");
		QVERIFY(QFile::exists(fixture));
		song->loadProject(fixture);
		QCOMPARE(song->projectFileName(), fixture);
		QTemporaryDir converted(m_output + "/node-lcd-dnb-XXXXXX");
		QVERIFY(converted.isValid());
		QVERIFY(song->convertToNodeProject(converted.filePath("DnB.mmpn")));

		auto* patterns = Engine::patternStore();
		patterns->setCurrentPattern(12);
		const auto& members = patterns->tracks();
		const auto source = std::find_if(members.cbegin(), members.cend(), [](const auto* track) {
			return track->name() == QStringLiteral("Fisa_Cym.ds");
		});
		QVERIFY(source != members.cend());
		const auto index = std::distance(members.cbegin(), source);
		auto* window = m_gui->mainWindow();
		window->showNodeWorkflow();
		auto* canvas = window->findChild<QWidget*>("nodeWorkflowCanvas");
		auto* breadcrumb = window->findChild<QLabel*>("nodeWorkflowBreadcrumb");
		auto* lcd = window->findChild<QScrollArea*>("nodeWorkflowTriggerLcd");
		QVERIFY(canvas && breadcrumb && lcd);
		breadcrumb->linkActivated(QStringLiteral("pattern"));
		const QRectF bounds(QPointF(35, 40), QPointF(990, 143 + 120.0 * (members.size() - 1)));
		const auto scale = std::min({(canvas->width() - 80) / bounds.width(),
			(canvas->height() - 80) / bounds.height(), qreal{1.5}});
		const auto point = (QPointF(canvas->width(), canvas->height()) / 2
			+ (QPointF(75, 58 + 120.0 * index) - bounds.center()) * scale).toPoint();
		QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, point);
		QTRY_VERIFY(breadcrumb->text().contains(QStringLiteral("Fisa_Cym.ds")));
		QVERIFY(lcd->isVisible());

		for (int y = 40; y < canvas->height() - 40 && !lcd->property("wireSelected").toBool(); y += 3)
		{
			QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(canvas->width() / 3, y));
		}
		QVERIFY(lcd->property("wireSelected").toBool());

		auto* engine = Engine::audioEngine();
		bool available = false;
		engine->storeAudioDevice();
		engine->setAudioDevice(new UiManualAudioDevice(available, engine), false);
		const auto cleanup = qScopeGuard([song, engine] {
			song->stop();
			engine->restoreAudioDevice();
		});
		QVERIFY(available);
		song->setPlayPos(6528, Song::PlayMode::Song);
		song->playSong();
		for (int period = 0; period < 4; ++period)
		{
			engine->renderNextPeriod();
		}
		const auto showsPlayingPattern = [lcd] {
			for (auto* row : lcd->findChildren<QLabel*>())
			{
				if (row->property("nodeWorkflowTriggerLcdRow").toBool()
					&& row->text() == QStringLiteral("domdom"))
				{
					return true;
				}
			}
			return false;
		};
		QTRY_VERIFY(showsPlayingPattern());
		QTest::qWait(2100);
		QVERIFY(showsPlayingPattern());
		QCOMPARE(patterns->currentPattern(), 12);
		capture(window, "node-workflow-dnb-trigger-lcd");
	}


	void nodeWorkflowAudioSignalThreshold()
	{
		auto* window = m_gui->mainWindow();
		auto* song = Engine::getSong();
		if (!song->isNodeProject())
		{
			const auto actions = window->findChildren<QAction*>();
			const auto create = std::find_if(actions.cbegin(), actions.cend(), [](const auto* action) {
				return action->text() == QStringLiteral("New Node Workflow Project");
			});
			QVERIFY(create != actions.cend());
			song->setModified(false);
			(*create)->trigger();
		}
		auto* threshold = window->findChild<QDoubleSpinBox*>("nodeWorkflowAudioSilenceThreshold");
		QVERIFY(threshold);
		QCOMPARE(threshold->value(), -70.0);
		auto* engine = Engine::audioEngine();
		bool available = false;
		engine->storeAudioDevice();
		engine->setAudioDevice(new UiManualAudioDevice(available, engine), false);
		const bool wasMonitoring = NodeSignalActivity::monitoring();
		const auto cleanup = qScopeGuard([engine, wasMonitoring] {
			NodeSignalActivity::setMonitoring(wasMonitoring);
			engine->restoreAudioDevice();
		});
		QVERIFY(available);
		NodeSignalActivity::setMonitoring(true);
		const auto frames = engine->framesPerPeriod();
		AudioBuffer signal(frames, 2);
		std::ranges::fill(signal.buffer(0), 0.00031f);
		std::ranges::fill(signal.buffer(1), -0.00032f);
		signal.updateAllSilenceFlags();
		auto* mixer = Engine::mixer();
		const auto mix = [&] {
			mixer->mixMonoToChannel(signal, 0, 0, 0);
			mixer->mixMonoToChannel(signal, 1, 0, 1);
			std::vector<SampleFrame> output(frames);
			mixer->masterMix(output.data());
			return output;
		};
		const auto output = mix();
		QVERIFY(!mixer->nodeMasterSignalActive(0));
		QVERIFY(mixer->nodeMasterSignalActive(1));
		QVERIFY(output.front()[0] > 0.0f);
		QVERIFY(output.front()[1] < 0.0f);
		threshold->setValue(-60.0);
		const auto quieterDisplay = mix();
		QVERIFY(!mixer->nodeMasterSignalActive(0));
		QVERIFY(!mixer->nodeMasterSignalActive(1));
		QCOMPARE(quieterDisplay.front()[0], output.front()[0]);
		QCOMPARE(quieterDisplay.front()[1], output.front()[1]);
		QCOMPARE(ConfigManager::inst()->value("ui", "nodeaudiosilencedb"), QStringLiteral("-60"));
		threshold->setValue(-70.0);
	}

	void nodeWorkflowAggregateEdits()
	{
		NodeWorkflowCanvas canvas;
		canvas.resize(960, 600);
		canvas.setConnectionAggregation(true, 1);
		const QColor color("#77C795");
		const QList<NodeWorkflowCanvas::Node> nodes{
			{QRectF(20, 40, 130, 300), QStringLiteral("Sources"), {}},
			{QRectF(650, 40, 130, 300), QStringLiteral("Targets"), {}}
		};
		const QList<QPointF> outputs{{150, 110}, {150, 185}, {150, 260}};
		const QList<QPointF> inputs{{650, 110}, {650, 260}};
		QMap<int, int> links{{0, 0}, {1, 0}};
		const auto initial = links;
		std::function<void()> render;
		render = [&] {
			QList<NodeWorkflowCanvas::Port> ports;
			QList<NodeWorkflowCanvas::Connection> wires;
			for (int index = 0; index < outputs.size(); ++index)
			{
				ports.append({outputs.at(index), color, QStringLiteral("Output %1").arg(index),
					true, {}, "audio", static_cast<unsigned>(index), [&, index](unsigned target) {
						links[index] = static_cast<int>(target);
						render();
					}});
			}
			for (int index = 0; index < inputs.size(); ++index)
			{
				ports.append({inputs.at(index), color, QStringLiteral("Input %1").arg(index),
					false, {}, "audio", static_cast<unsigned>(index), {}});
			}
			for (auto it = links.cbegin(); it != links.cend(); ++it)
			{
				wires.append({outputs.at(it.key()), inputs.at(it.value()), color, {}, [&, source = it.key()] {
					links.remove(source);
					render();
				}});
			}
			canvas.setGraph(nodes, std::move(wires), std::move(ports));
		};
		render();
		canvas.show();
		QVERIFY(QTest::qWaitForWindowExposed(&canvas));
		canvas.fitGraph();
		const auto scale = (canvas.width() - 80) / 760.0;
		const auto point = (QPointF(canvas.width(), canvas.height()) / 2
			+ (QPointF(580, 110) - QPointF(400, 190)) * scale).toPoint();
		for (const bool save : {false, true})
		{
			bool staged = false;
			QTimer::singleShot(0, &canvas, [&] {
				auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
				if (!menu || menu->actions().isEmpty())
				{
					return;
				}
				QTimer::singleShot(0, &canvas, [&] {
					auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
					if (!dialog)
					{
						return;
					}
					auto* sources = dialog->findChild<QComboBox*>("nodeWorkflowConnectionEditSource");
					auto* destinations = dialog->findChild<QComboBox*>("nodeWorkflowConnectionEditDestination");
					auto* buttons = dialog->findChild<QDialogButtonBox*>();
					auto* reconnect = dialog->findChild<QPushButton*>("nodeWorkflowConnectionReconnect");
					auto* rows = dialog->findChild<QTableWidget*>("nodeWorkflowConnectionRows");
					if (!sources || !destinations || !buttons || !reconnect || dialog->findChildren<QComboBox*>().size() != 2 || !rows || rows->rowCount() < 2)
					{
						dialog->reject();
						return;
					}
					QTest::mouseClick(rows->viewport(), Qt::LeftButton, Qt::NoModifier,
						rows->visualItemRect(rows->item(1, 1)).center());
					const auto chooseNext = [](QComboBox* combo) {
						combo->showPopup();
						auto* view = combo->view();
						const auto choice = combo->model()->index(1, combo->modelColumn());
						view->scrollTo(choice);
						QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
							view->visualRect(choice).center());
						QCoreApplication::processEvents();
					};
					if (rows->currentRow() != 1 || !sources->currentText().endsWith(QStringLiteral("Output 1"))
						|| sources->count() < 2 || destinations->count() < 2)
					{
						dialog->reject();
						return;
					}
					chooseNext(sources);
					chooseNext(destinations);
					if (!reconnect->isEnabled() || !rows->item(1, 0)->text().endsWith(QStringLiteral("Output 1")))
					{
						dialog->reject();
						return;
					}
					QTest::mouseClick(reconnect, Qt::LeftButton);
					staged = links == initial && rows->item(1, 0)->text().endsWith(QStringLiteral("Output 2"))
						&& rows->item(1, 1)->text().endsWith(QStringLiteral("Input 1"));
					QTest::mouseClick(buttons->button(save ? QDialogButtonBox::Save : QDialogButtonBox::Cancel),
						Qt::LeftButton);
				});
				menu->setActiveAction(menu->actions().first());
				QTest::keyClick(menu, Qt::Key_Return);
			});
			QTest::mouseClick(&canvas, Qt::RightButton, Qt::NoModifier, point);
			QVERIFY(staged);
			if (!save)
			{
				QCOMPARE(links, initial);
			}
		}
		QCOMPARE(links.size(), 2);
		QCOMPARE(links.value(2, -1), 1);
		QVERIFY(links.contains(0) && !links.contains(1));
	}

	void nodeWorkflowAggregatePorts()
	{
		NodeWorkflowCanvas canvas;
		canvas.resize(960, 600);
		const QColor typeColor("#77C795");
		QList<NodeWorkflowCanvas::Node> nodes{
			{QRectF(20, 40, 130, 430), QStringLiteral("Sources"), {}},
			{QRectF(650, 40, 130, 430), QStringLiteral("Targets"), {}},
		};
		QList<NodeWorkflowCanvas::Port> ports;
		QList<NodeWorkflowCanvas::Connection> wires;
		for (int index = 0; index < 6; ++index)
		{
			const qreal y = 110 + 55 * index;
			ports.append({QPointF(150, y), typeColor, QStringLiteral("Source %1 L").arg(index + 1), true, {}});
			ports.append({QPointF(650, y), typeColor, QStringLiteral("Target %1 L").arg(index + 1), false, {}});
		}
		ports.append({QPointF(150, 453), typeColor, QStringLiteral("Source R"), true, {}});
		ports.append({QPointF(650, 453), typeColor, QStringLiteral("Target R"), false, {}});
		for (int source = 0; source < 6; ++source)
		{
			for (int destination = 0; destination < 6; ++destination)
			{
				wires.append({QPointF(150, 110 + 55 * source), QPointF(650, 110 + 55 * destination),
					QColor("#D76D6D")});
			}
		}
		wires.append({QPointF(150, 453), QPointF(650, 453), typeColor});
		canvas.setGraph(std::move(nodes), std::move(wires), std::move(ports));
		canvas.setConnectionAggregation(true, 4);
		canvas.show();
		QVERIFY(QTest::qWaitForWindowExposed(&canvas));
		canvas.fitGraph();
		const auto scale = std::min({(canvas.width() - 80) / 760.0, (canvas.height() - 80) / 430.0, 1.5});
		const auto screen = [&canvas, scale](QPointF point) {
			return (QPointF(canvas.width(), canvas.height()) / 2 + (point - QPointF(400, 255)) * scale).toPoint();
		};
		const auto checkBundle = [&](QPointF point, int sharedColumn, const QString& sharedLabel) {
			bool correct = false;
			QTimer::singleShot(0, &canvas, [&] {
				auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
				if (!menu || menu->actions().isEmpty())
				{
					return;
				}
				QTimer::singleShot(0, &canvas, [&] {
					auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
					if (!dialog)
					{
						return;
					}
					auto* table = dialog->findChild<QTableWidget*>("nodeWorkflowConnectionRows");
					correct = table && table->rowCount() == 6;
					for (int row = 0; correct && row < table->rowCount(); ++row)
					{
						correct = table->item(row, sharedColumn)->text().endsWith(sharedLabel);
					}
					dialog->reject();
				});
				menu->setActiveAction(menu->actions().first());
				QTest::keyClick(menu, Qt::Key_Return);
			});
			QTest::mouseClick(&canvas, Qt::RightButton, Qt::NoModifier, screen(point));
			return correct;
		};
		QVERIFY(checkBundle(QPointF(220, 110), 0, QStringLiteral("Source 1 L")));
		QVERIFY(checkBundle(QPointF(580, 110), 1, QStringLiteral("Target 1 L")));
		capture(&canvas, "node-workflow-aggregate-input-output");
		canvas.setPcbBusRouting(true);
		QVERIFY(checkBundle(QPointF(150 + 140, 170), 0, QStringLiteral("Source 1 L")));
		QVERIFY(checkBundle(QPointF(650 - 140, 170), 1, QStringLiteral("Target 1 L")));

		QList<NodeWorkflowCanvas::Node> stereoNodes{
			{QRectF(20, 40, 130, 430), QStringLiteral("Sources"), {}},
			{QRectF(650, 40, 130, 430), QStringLiteral("Mixer"), {}},
		};
		QList<NodeWorkflowCanvas::Port> stereoPorts;
		QList<NodeWorkflowCanvas::Connection> stereoWires;
		for (int channel = 0; channel < 2; ++channel)
		{
			const auto label = channel == 0 ? QStringLiteral("Master L") : QStringLiteral("Master R");
			const QPointF target(650, 110 + channel * 26);
			stereoPorts.append({target, typeColor, label, false, {}});
			for (int index = 0; index < 6; ++index)
			{
				const QPointF source(150, 110 + channel * 26 + index * 55);
				stereoPorts.append({source, typeColor, label, true, {}});
				stereoWires.append({source, target, typeColor});
			}
		}
		canvas.setGraph(std::move(stereoNodes), std::move(stereoWires), std::move(stereoPorts));
		canvas.fitGraph();
		const qreal leftTrunk = 650 - 140;
		QVERIFY(checkBundle(QPointF(leftTrunk, 230), 1, QStringLiteral("Master L")));
		QVERIFY(checkBundle(QPointF(leftTrunk - 20, 230), 1, QStringLiteral("Master R")));
		const auto grab = screen(QPointF(710, 58));
		const auto drop = screen(QPointF(740, 58));
		QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, grab);
		QTest::mouseMove(&canvas, drop);
		QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, drop);
		const qreal movedTrunk = 680 - 140;
		QVERIFY(checkBundle(QPointF(movedTrunk, 230), 1, QStringLiteral("Master L")));
		QVERIFY(checkBundle(QPointF(movedTrunk - 20, 230), 1, QStringLiteral("Master R")));
		capture(&canvas, "node-workflow-aggregate-pcb");
	}

	void nodeWorkflowRoot()
	{
		auto* window = m_gui->mainWindow();
		const auto label = tr("New Node Workflow Project");
		QAction* newNodeProject = nullptr;
		for (auto* action : window->findChildren<QAction*>())
		{
			if (action->text() == label)
			{
				newNodeProject = action;
				break;
			}
		}
		QVERIFY(newNodeProject);
		// Startup can mark the isolated, disposable project dirty.
		Engine::getSong()->setModified(false);
		newNodeProject->trigger();
		auto* canvas = window->findChild<QWidget*>("nodeWorkflowCanvas");
		QVERIFY(canvas);
		QTRY_VERIFY(canvas->isVisible() && !canvas->visibleRegion().isEmpty());
		QCOMPARE(canvas->property("audioPortColor").value<QColor>(), QColor("#77C795"));
		QCOMPARE(canvas->property("midiPortColor").value<QColor>(), QColor("#82BFFF"));
		QCOMPARE(canvas->property("voicePortColor").value<QColor>(), QColor("#CBABFA"));
		capture(window, "node-workflow-root");
		auto* panel = window->findChild<QWidget*>("nodeWorkflowPatternPanel");
		QVERIFY(panel);
		QVERIFY(!panel->isVisible());
		QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier,
			QPoint(canvas->width() * 0.16, canvas->height() * 0.67));
		QVERIFY(!panel->isVisible());
		capture(window, "node-workflow-pattern-graph");

		QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier,
			QPoint(canvas->width() * 0.16, canvas->height() * 0.49));
		QTRY_VERIFY(panel->isVisible());
		QVERIFY(panel->mapToGlobal(panel->rect().topLeft()).y()
			>= canvas->mapToGlobal(canvas->rect().bottomLeft()).y());
		QCOMPARE(panel->height(), 31);
		auto* steps = panel->findChild<QWidget*>("nodeWorkflowPatternSteps");
		QVERIFY(steps);
		QTRY_VERIFY(steps->isVisible());
		auto* patterns = Engine::patternStore();
		QCOMPARE(patterns->currentPattern(), 0);
		QVERIFY(!patterns->tracks().empty());
		auto* clip = dynamic_cast<MidiClip*>(patterns->tracks().front()->getClip(0));
		QVERIFY(clip);
		QCOMPARE(clip->steps(), 16);
		for (int step = 0; step < clip->steps(); ++step)
		{
			QVERIFY(!clip->noteAtStep(step));
		}
		capture(window, "node-workflow-pattern-kicker");
		const QPoint firstStep(steps->width() / (2 * clip->steps()), steps->height() / 2);
		QTest::mouseClick(steps, Qt::LeftButton, Qt::NoModifier, firstStep);
		QVERIFY(clip->noteAtStep(0));

		QToolButton* signalFlow = nullptr;
		QToolButton* rootGraph = nullptr;
		for (auto* button : canvas->window()->findChild<QWidget*>("nodeWorkflowContent")->findChildren<QToolButton*>())
		{
			if (!button->property("nodeWorkflowTool").toBool())
			{
				continue;
			}
			if (button->accessibleName() == QStringLiteral("Signal Flow"))
			{
				signalFlow = button;
			}
			else if (button->accessibleName() == QStringLiteral("Root Graph"))
			{
				rootGraph = button;
			}
		}
		QVERIFY(signalFlow && rootGraph);
		QVERIFY(signalFlow->isChecked());
		auto* engine = Engine::audioEngine();
		bool available = false;
		engine->storeAudioDevice();
		engine->setAudioDevice(new UiManualAudioDevice(available, engine), false);
		const auto restoreAudio = qScopeGuard([engine] { engine->restoreAudioDevice(); });
		QVERIFY(available);
		auto* song = Engine::getSong();
		// Capture after the animation has advanced past the source port.
		QTest::qWait(250);
		song->playPattern();
		const auto stopPlayback = qScopeGuard([song] { song->stop(); });
		for (int period = 0; period < 16 && !Engine::mixer()->nodeMasterSignalActive(0); ++period)
		{
			engine->renderNextPeriod();
		}
		QVERIFY(Engine::mixer()->nodeMasterSignalActive(0));
		QVERIFY(Engine::mixer()->nodeMasterSignalActive(1));
		canvas->repaint();
		const auto subgraphSignal = window->screen()->grabWindow(window->winId());
		QVERIFY(!subgraphSignal.isNull());
		QVERIFY(subgraphSignal.save(m_output + "/node-workflow-pattern-signal.png"));
		QTest::qWait(230);
		for (int period = 0; period < 4; ++period)
		{
			engine->renderNextPeriod();
		}
		QVERIFY(Engine::mixer()->nodeMasterSignalActive(0));
		QVERIFY(Engine::mixer()->nodeMasterSignalActive(1));
		canvas->repaint();
		const auto advancedSignal = window->screen()->grabWindow(window->winId());
		QVERIFY(!advancedSignal.isNull());
		QVERIFY(advancedSignal.save(m_output + "/node-workflow-pattern-signal-advanced.png"));

		song->setNodeMasterOutputConnected(0, false);
		engine->renderNextPeriod();
		QVERIFY(!Engine::mixer()->nodeMasterSignalActive(0));
		QVERIFY(Engine::mixer()->nodeMasterSignalActive(1));
		song->setNodeMasterOutputConnected(1, false);
		engine->renderNextPeriod();
		QVERIFY(!Engine::mixer()->nodeMasterSignalActive(0));
		QVERIFY(!Engine::mixer()->nodeMasterSignalActive(1));
		song->setNodeMasterOutputConnected(0, true);
		song->setNodeMasterOutputConnected(1, true);

		QTest::mouseClick(steps, Qt::LeftButton, Qt::NoModifier, firstStep);
		QVERIFY(!clip->noteAtStep(0));
		QTest::mouseClick(rootGraph, Qt::LeftButton);
		QVERIFY(!panel->isVisible());
		QVERIFY(signalFlow->isChecked());
		QTest::mouseClick(signalFlow, Qt::LeftButton);
		QVERIFY(!signalFlow->isChecked());

		song->stop();
		clip->setClipType(MidiClip::Type::MelodyClip);
		QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier,
			QPoint(canvas->width() * 0.16, canvas->height() * 0.67));
		bool matchedPatternMenu = false;
		bool capturedPatternMenu = false;
		QTimer::singleShot(300, canvas, [&] {
			auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
			if (!menu)
			{
				return;
			}
			const auto actions = menu->actions();
			const QStringList expected{"Copy Node", "Paste Node", "Open Piano Roll", "Delete Node"};
			QStringList labels;
			for (const auto* action : actions)
			{
				labels.append(action->text());
			}
			matchedPatternMenu = labels == expected && actions[0]->isEnabled()
				&& !actions[1]->isEnabled() && actions[2]->isEnabled() && actions[3]->isEnabled();
			if (matchedPatternMenu)
			{
				auto* screen = menu->screen();
				const QRect area = menu->frameGeometry().translated(-screen->geometry().topLeft());
				const auto popup = screen->grabWindow(0, area.x(), area.y(), area.width(), area.height());
				capturedPatternMenu = popup.save(m_output + "/node-workflow-pattern-note-menu.png");
				menu->setActiveAction(actions[2]);
				QTest::keyClick(menu, Qt::Key_Return);
			}
			else
			{
				menu->close();
			}
		});
		QTest::mouseClick(canvas, Qt::RightButton, Qt::NoModifier,
			QPoint(canvas->width() * 0.16, canvas->height() * 0.49));
		QVERIFY(matchedPatternMenu);
		QVERIFY(capturedPatternMenu);
		QCOMPARE(m_gui->pianoRoll()->currentMidiClip(), clip);
		m_gui->pianoRoll()->parentWidget()->hide();
		clip->setClipType(MidiClip::Type::BeatClip);
		auto* nativePattern = m_gui->patternEditor();
		auto* patternEditor = nativePattern->m_editor;
		auto* patternWindow = qobject_cast<QMdiSubWindow*>(nativePattern->parentWidget());
		QVERIFY(patternWindow);
		patternWindow->setGeometry(QRect(QPoint(0, 0),
			QSize(1000, 600).boundedTo(window->workspace()->viewport()->size())));
		nativePattern->show();
		patternWindow->show();
		patternWindow->raise();
		QTRY_VERIFY(patternEditor->isVisible());
		const auto pixelsPerBar = patternEditor->pixelsPerBar();
		QCOMPARE(pixelsPerBar, TimePos::stepsPerBar() * PatternStepRow::StepWidth);
		patternEditor->cloneSteps();
		patternEditor->cloneSteps();
		QCOMPARE(clip->steps(), 64);
		QCOMPARE(patternEditor->pixelsPerBar(), pixelsPerBar);
		auto* scroll = patternEditor->findChild<QScrollBar*>("patternContentHorizontalScroll");
		QVERIFY(scroll);
		QTRY_VERIFY(scroll->isVisible() && scroll->maximum() > 0);
		MidiClipView* clipView = nullptr;
		for (auto* view : patternEditor->findChildren<MidiClipView*>())
		{
			if (view->getMidiClip() == clip && view->isVisible())
			{
				clipView = view;
				break;
			}
		}
		QVERIFY(clipView);
		auto* trackRow = clipView->parentWidget()->parentWidget();
		const auto headerOrigin = trackRow->mapToGlobal(QPoint(0, 0));
		scroll->setValue(scroll->maximum());
		QCOMPARE(trackRow->mapToGlobal(QPoint(0, 0)), headerOrigin);
		const int last = clip->steps() - 1;
		const QPoint lastStep(lmms::gui::ClipView::BORDER_WIDTH + last * PatternStepRow::StepWidth
				+ PatternStepRow::StepWidth / 2, 12);
		QTRY_VERIFY2(clipView->visibleRegion().contains(lastStep),
			qPrintable(QStringLiteral("Step=(%1,%2) clipWidth=%3 trackWidth=%4 visibleRight=%5 scroll=%6/%7")
				.arg(lastStep.x()).arg(lastStep.y()).arg(clipView->width()).arg(clipView->parentWidget()->width())
				.arg(clipView->visibleRegion().boundingRect().right()).arg(scroll->value()).arg(scroll->maximum())));
		QTest::mouseClick(clipView, Qt::LeftButton, Qt::NoModifier, lastStep);
		QVERIFY(clip->noteAtStep(last));
		capture(window, "node-workflow-native-pattern-scroll");
	}

	void aiCacheManagement()
	{
		QStandardPaths::setTestModeEnabled(true);
		auto* config = ConfigManager::inst();
		QCOMPARE(svs::Cache::instance().diskLimit(), 2LL * 1024 * 1024 * 1024);
		for (const auto& language : QStringList{"zh_CN", "ja", "en", "ko"})
		{
			QTranslator translator;
			QVERIFY(translator.load(QString("%1/locale/%2.qm").arg(qEnvironmentVariable("LMMS_DATA_DIR"), language)));
			QCoreApplication::installTranslator(&translator);
			config->setValue("aiCache", "limitMiB", "2048");
			{
				const auto activeWorkingDirectory = config->workingDir();
				const auto cacheSetting = config->value("paths", "aicache");
				config->setWorkingDir(QCoreApplication::applicationDirPath() + "/lmms-workspace/");
				config->setValue("paths", "aicache", QCoreApplication::applicationDirPath() + "/cache");
				SetupDialog paths(SetupDialog::ConfigTab::PathsSettings);
				auto* editor = paths.findChild<QLineEdit*>("aiCacheDirectory");
				QVERIFY(editor);
				QCOMPARE(QDir::fromNativeSeparators(editor->text()), QString("cache"));
				QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(
					paths.findChild<QLineEdit*>("workingDirectory")->text())), QString("lmms-workspace"));
				QCOMPARE(QDir::fromNativeSeparators(paths.findChild<QLineEdit*>("themeDirectory")->text()),
					QString("data/themes/default/"));
				if (language != "en")
				{
					QVERIFY(QCoreApplication::translate("lmms::gui::SetupDialog", "AI cache directory")
						!= "AI cache directory");
				}
				capture(&paths, "ai-cache-path-" + language);
				if (language == "zh_CN")
				{
					auto* scroll = paths.findChild<QWidget*>("pathsSettingsPage")->findChild<QScrollArea*>();
					scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
					capture(&paths, "ai-cache-path-theme-zh_CN");
				}
				paths.close();
				config->setWorkingDir(activeWorkingDirectory);
				config->setValue("paths", "aicache", cacheSetting);
			}
			svs::Cache::instance().setDiskLimit(2LL * 1024 * 1024 * 1024);
			const auto svcRoot = QDir(config->aiCacheDir()).filePath("svc/Reference/input");
			const auto svsRoot = svs::Cache::instance().engineDirectory("org.lmms.svs.diffsinger");
			QVERIFY(QDir().mkpath(svcRoot) && QDir().mkpath(svsRoot));
			for (const auto& path : {QDir(svcRoot).filePath(QString(64, 'a') + ".wav"),
					 QDir(svsRoot).filePath(QString(64, 'b') + ".tensor")})
			{
				QFile file(path);
				QVERIFY(file.open(QIODevice::WriteOnly));
				const qint64 size = path.endsWith(".wav") ? 6 * 1024 * 1024 : 4 * 1024 * 1024;
				QCOMPARE(file.write(QByteArray(size, 'x')), size);
			}
			SetupDialog settings(SetupDialog::ConfigTab::AiCacheSettings);
			auto* page = static_cast<AICacheSettingsPage*>(settings.findChild<QWidget*>("aiCacheSettingsPage"));
			QVERIFY(page && page->isVisible());
			auto* icon = settings.findChild<QPushButton*>("aiCacheSettingsTab");
			QVERIFY(icon && !icon->icon().isNull());
			QCOMPARE(icon->toolTip(), QCoreApplication::translate("lmms::gui::SetupDialog", "AI cache"));
			if (language != "en")
			{
				QVERIFY(icon->toolTip() != "AI cache");
				for (const auto* source : {"Clear all caches", "Maximum total cache size", "Current cache size",
						 "Clear SVC cache", "Clear SVS cache", "Total cache size: %1", "Cache cleared.",
						 "Some cache files are in use or could not be removed. Remaining usage is shown above."})
				{
					QVERIFY(QCoreApplication::translate("lmms::gui::AICacheSettingsPage", source) != source);
				}
			}
			auto* limit = page->findChild<QDoubleSpinBox*>("aiCacheLimit");
			QVERIFY(limit);
			QCOMPARE(limit->value(), 2.0);
			QVERIFY(!page->findChild<QDoubleSpinBox*>("svcCacheLimit"));
			QVERIFY(!page->findChild<QDoubleSpinBox*>("svsCacheLimit"));
			QCOMPARE(page->findChild<QLabel*>("aiCacheTotalUsage")->text(),
				QLocale().formattedDataSize(10 * 1024 * 1024, 2));
			QCOMPARE(page->findChild<QLabel*>("aiCacheTotalCapacity")->text(),
				QCoreApplication::translate("lmms::gui::AICacheSettingsPage", "Limit: %1")
					.arg(QLocale().formattedDataSize(2LL * 1024 * 1024 * 1024, 2)));
			limit->setValue(0.02);
			capture(&settings, "ai-cache-" + language);
			auto* bar = page->findChild<AICacheUsageBar*>("aiCacheUsageBar");
			QVERIFY(bar);
			const auto barImage = [bar] {
				const auto point = bar->mapTo(bar->window(), QPoint());
				return bar->screen()->grabWindow(bar->window()->winId(),
					point.x(), point.y(), bar->width(), bar->height()).toImage();
			};
			const auto verifyColors = [&] {
				const auto pixels = barImage();
				QCOMPARE(pixels.pixelColor(bar->width() / 6, 6), bar->svcColor());
				QCOMPARE(pixels.pixelColor(bar->width() * 2 / 5, 6), bar->svsColor());
				QCOMPARE(pixels.pixelColor(bar->width() * 4 / 5, 6), bar->freeColor());
			};
			verifyColors();
			if (language == "zh_CN")
			{
				bar->setStyleSheet("lmms--gui--AICacheUsageBar { qproperty-svcColor: #ff7040; "
					"qproperty-svsColor: #7050e0; qproperty-freeColor: #52606c; }");
				capture(&settings, "ai-cache-custom-colors");
				QCOMPARE(bar->svcColor(), QColor("#ff7040"));
				QCOMPARE(bar->svsColor(), QColor("#7050e0"));
				QCOMPARE(bar->freeColor(), QColor("#52606c"));
				verifyColors();
			}
			QTest::mouseClick(page->findChild<QPushButton*>("svcCacheClear"), Qt::LeftButton);
			QCOMPARE(svc::cacheBytes(config->aiCacheDir()), qint64(0));
			QCOMPARE(svs::Cache::instance().diskBytes(), qint64(4 * 1024 * 1024));
			QTest::mouseClick(page->findChild<QPushButton*>("svsCacheClear"), Qt::LeftButton);
			QCOMPARE(svs::Cache::instance().diskBytes(), qint64(0));
			for (const auto& path : {QDir(svcRoot).filePath(QString(64, 'a') + ".wav"),
					 QDir(svsRoot).filePath(QString(64, 'b') + ".tensor")})
			{
				QFile file(path);
				QVERIFY(file.open(QIODevice::WriteOnly));
				QCOMPARE(file.write(QByteArray(1024, 'x')), qint64(1024));
			}
			QTest::mouseClick(page->findChild<QPushButton*>("aiCacheClearAll"), Qt::LeftButton);
			QCOMPARE(svc::cacheBytes(config->aiCacheDir()), qint64(0));
			QCOMPARE(svs::Cache::instance().diskBytes(), qint64(0));
			QCOMPARE(page->findChild<QLabel*>("aiCacheStatus")->text(),
				QCoreApplication::translate("lmms::gui::AICacheSettingsPage", "Cache cleared."));
			limit->setValue(1.25);
			page->save();
			QCOMPARE(svc::cacheLimit(), qint64(1280) * 1024 * 1024);
			QCOMPARE(svs::Cache::instance().diskLimit(), qint64(1280) * 1024 * 1024);
			{
				AICacheSettingsPage reopened;
				QCOMPARE(reopened.findChild<QDoubleSpinBox*>("aiCacheLimit")->value(), 1.25);
			}
			settings.close();
			QCoreApplication::removeTranslator(&translator);
		}
	}
	void svsLyricNameAndPianoAudition()
	{
		auto* track = new SVSTrack(Engine::getSong());
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note first;
		first.id = "first";
		first.tick = 0;
		first.duration = 48;
		first.pitch = 60;
		first.lyric = QString::fromUtf8("你");
		auto second = first;
		second.id = "second";
		second.tick = 48;
		second.lyric = QString::fromUtf8("好");
		clip->setNotes({second, first});
		QCOMPARE(clip->name(), QString::fromUtf8("你好"));
		first.lyric = QString::fromUtf8("真");
		clip->setNotes({first, second});
		QCOMPARE(clip->name(), QString::fromUtf8("真好"));
		QDomDocument document;
		auto saved = document.createElement("svsclip");
		clip->saveSettings(document, saved);
		clip->loadSettings(saved);
		second.lyric = QString::fromUtf8("美");
		clip->setNotes({first, second});
		QCOMPARE(clip->name(), QString::fromUtf8("真美"));
		clip->setName("My phrase");
		first.lyric = "hello";
		second.lyric = "world";
		clip->setNotes({first, second});
		QCOMPARE(clip->name(), QString("My phrase"));
		clip->setName("SVS");
		clip->setNotes({first});
		clip->setNotes({first, second});
		QCOMPARE(clip->name(), QString("hello world"));

		{
			SVSCanvas canvas(clip);
			canvas.resize(800, 450);
			canvas.setScroll(0, 65);
			canvas.show();
			QVERIFY(QTest::qWaitForWindowExposed(&canvas));
			canvas.beginLyric("first");
			auto* lyric = canvas.findChild<QLineEdit*>("svsInlineLyric");
			QVERIFY(lyric && lyric->isVisible());
			lyric->setText(QString::fromUtf8("你好"));
			QTest::keyClick(lyric, Qt::Key_Return);
			QCOMPARE(clip->name(), QString::fromUtf8("你好world"));
			capture(&canvas, "svs-lyric-name-piano");
			auto* engine = Engine::audioEngine();
			bool available = false;
			engine->storeAudioDevice();
			engine->setAudioDevice(new UiManualAudioDevice(available, engine), false);
			const auto restoreAudio = qScopeGuard([engine] { engine->restoreAudioDevice(); });
			QVERIFY(available);
			const auto render = [engine](int periods) {
				std::vector<float> result;
				for (int period = 0; period < periods; ++period)
				{
					for (const auto& frame : engine->renderNextPeriod())
					{
						result.push_back(frame[0]);
					}
				}
				return result;
			};
			const auto peak = [](const std::vector<float>& samples) {
				float value = 0;
				for (float sample : samples)
				{
					value = std::max(value, std::abs(sample));
				}
				return value;
			};
			render(8);
			const auto key = [&](int pitch) { return QPoint(20, int(canvas.pointAt(0, pitch).y() + 8)); };
			QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, key(60));
			const auto c4 = render(24);
			QVERIFY(peak(c4) > .01f);
			const auto fundamental = [engine](const std::vector<float>& samples) {
				const auto rate = engine->outputSampleRate();
				double best = -1;
				int bestLag = 0;
				for (int lag = int(rate / 300); lag < int(rate / 230); ++lag)
				{
					double correlation = 0;
					for (size_t index = 1024; index + lag < samples.size(); ++index)
					{
						correlation += double(samples[index]) * samples[index + lag];
					}
					if (correlation > best)
					{
						best = correlation;
						bestLag = lag;
					}
				}
				return double(rate) / bestLag;
			};
			QVERIFY(std::abs(fundamental(c4) - 261.63) < 4);
			QTest::mouseMove(&canvas, key(61));
			const auto cSharp4 = render(24);
			QVERIFY(peak(cSharp4) > .01f);
			QVERIFY(std::abs(fundamental(cSharp4) - 277.18) < 4);
			qInfo() << "Native piano audition C4/C#4:" << fundamental(c4) << fundamental(cSharp4);
			QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, key(61));
			render(24);
			QVERIFY(peak(render(8)) < .0001f);
			QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, key(60));
			QVERIFY(peak(render(12)) > .01f);
			canvas.hide();
			render(24);
			QVERIFY(peak(render(8)) < .0001f);
		}
		delete track;
	}
	void batchLyricCounts()
	{
		auto* track = new SVSTrack(Engine::getSong());
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		QVector<svs::Note> notes;
		QSet<QString> selection;
		for (int index = 0; index < 4; ++index)
		{
			svs::Note note;
			note.id = QString::number(index);
			note.tick = index * 48;
			note.duration = 48;
			note.pitch = 60;
			note.lyric = "la";
			notes.append(note);
			selection.insert(note.id);
		}
		clip->setNotes(notes);
		QCOMPARE(SVSLyricEditor::splitLyrics(QString::fromUtf8("你-好")),
			QStringList({QString::fromUtf8("你"), "-", QString::fromUtf8("好")}));
		for (const auto& language : QStringList{"zh_CN", "ja", "en", "ko"})
		{
			QTranslator translator;
			QVERIFY(translator.load(QString("%1/locale/%2.qm").arg(qEnvironmentVariable("LMMS_DATA_DIR"), language)));
			QCoreApplication::installTranslator(&translator);
			SVSLyricEditor dialog(clip, selection);
			auto* text = dialog.findChild<QPlainTextEdit*>("svsBatchLyricText");
			auto* skip = dialog.findChild<QCheckBox*>("svsBatchSkipContinuation");
			auto* table = dialog.findChild<QTableWidget*>("svsBatchLyricPreview");
			auto* diagnostic = dialog.findChild<QLabel*>();
			QVERIFY(text && skip && table && diagnostic);
			QVERIFY(!skip->isChecked());
			const auto verifyCounts = [&](int assigned, int unused) {
				QCOMPARE(diagnostic->text(), QCoreApplication::translate("lmms::gui::SVSLyricEditor",
					"%1 tokens assigned; %2 notes unused. Manual readings and phonemes are preserved. Changes apply together on "
					"confirmation.").arg(assigned).arg(unused));
			};
			text->clear();
			verifyCounts(0, 4);
			text->setPlainText(QString::fromUtf8("你"));
			verifyCounts(1, 3);
			capture(&dialog, "batch-lyric-unused-" + language);
			text->setPlainText(QString::fromUtf8("你好世界"));
			verifyCounts(4, 0);
			text->setPlainText(QString::fromUtf8("你好世界呀"));
			verifyCounts(4, 0);
			text->setPlainText(QString::fromUtf8("你-好"));
			verifyCounts(3, 1);
			QCOMPARE(table->item(1, 2)->text(), QString("-"));
			dialog.accept();
			QCOMPARE(clip->notes()[1].lyric, QString("-"));
			clip->setNotes(notes);
			QCoreApplication::removeTranslator(&translator);
		}
		delete track;
	}
	void aiCachePathPersistence()
	{
		auto* config = ConfigManager::inst();
		const auto activeCacheDirectory = config->aiCacheDir();
		const auto oldTheme = config->themeDir();
		const auto oldCacheSetting = config->value("paths", "aicache");
		{
			SetupDialog paths(SetupDialog::ConfigTab::PathsSettings);
			paths.findChild<QLineEdit*>("aiCacheDirectory")->setText("cache/custom");
			QVERIFY(QMetaObject::invokeMethod(&paths, "accept", Qt::DirectConnection));
			QCOMPARE(config->aiCacheDir(), activeCacheDirectory);
			QFile saved(m_config.filePath("ui-config.xml"));
			QVERIFY(saved.open(QIODevice::ReadOnly));
			QDomDocument document;
			QVERIFY(document.setContent(saved.readAll()));
			const auto pathValues = document.documentElement().firstChildElement("paths");
			QCOMPARE(pathValues.attribute("aicache"), QString("cache/custom"));
			QCOMPARE(pathValues.attribute("theme"), QString("data/themes/default/"));
			paths.close();
		}
		{
			SetupDialog reopened(SetupDialog::ConfigTab::PathsSettings);
			QCOMPARE(QDir::fromNativeSeparators(reopened.findChild<QLineEdit*>("aiCacheDirectory")->text()),
				QString("cache/custom"));
			reopened.close();
		}
		config->setThemeDir(oldTheme);
		config->setValue("paths", "aicache", oldCacheSetting);
		config->saveConfigFile();
	}
	void svsCacheManagement()
	{
		QTemporaryDir directory;
		svs::Cache cache(directory.path());
		svs::Input input;
		input.rate = 48000;
		input.secondsPerTick = .01;
		input.duration = .2;
		auto audio = std::make_shared<svs::Audio>();
		audio->samples.resize(2000, .25f);
		const auto oldKey = QString(64, 'a');
		cache.put(oldKey, input, audio);
		const auto oldBytes = cache.diskBytes();
		QVERIFY(oldBytes > 0);
		const auto root = cache.engineDirectory({});
		for (const auto& file : QDir(root).entryInfoList(QDir::Files))
		{
			QFile old(file.absoluteFilePath());
			QVERIFY(old.open(QIODevice::ReadWrite));
			QVERIFY(old.setFileTime(QDateTime::currentDateTimeUtc().addDays(-1), QFileDevice::FileModificationTime));
		}
		audio->samples.assign(2000, .5f);
		const auto newKey = QString(64, 'b');
		cache.put(newKey, input, audio);
		const auto newBytes = cache.diskBytes() - oldBytes;
		cache.setDiskLimit(newBytes);
		QVERIFY(!QFile::exists(QDir(root).filePath(oldKey + ".svsmeta")));
		QVERIFY(QFile::exists(QDir(root).filePath(newKey + ".svsmeta")));
		QCOMPARE(cache.diskBytes(), newBytes);
		for (const auto& name : {QString(64, 'c') + ".tensor", QString(64, 'd') + ".wav", QString("keep.txt")})
		{
			QFile file(QDir(root).filePath(name));
			QVERIFY(file.open(QIODevice::WriteOnly));
			QCOMPARE(file.write(QByteArray(1024, 'x')), qint64(1024));
		}
		QCOMPARE(cache.diskBytes(), newBytes + 2048);
		cache.setDiskLimit(1024);
		QVERIFY(cache.diskBytes() <= 1024);
		QVERIFY(cache.clear());
		QCOMPARE(cache.diskBytes(), qint64(0));
		QCOMPARE(cache.memoryBytes(), qint64(0));
		QVERIFY(QFile::exists(QDir(root).filePath("keep.txt")));
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
	void synthesisStatusStrip()
	{
		auto* track = new SVSTrack(Engine::getSong());
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		SVSPianoRoll editor(clip);
		editor.resize(1100, 660);
		capture(&editor, "synthesis-strip-empty");
		auto* canvas = editor.findChild<SVSCanvas*>("svsNoteCanvas");
		QVERIFY(canvas && !editor.findChild<QLabel*>("svsSynthesisStatus"));
		auto* strip = canvas->findChild<QWidget*>("svsSynthesisProgressStrip");
		QVERIFY(strip && !strip->isVisible());
		svs::Note initialNote;
		initialNote.id = QUuid::createUuid().toString();
		initialNote.duration = 96;
		initialNote.pitch = 60;
		initialNote.lyric = "a";
		clip->setNotes({initialNote});
		QVERIFY(strip->isVisible());
		capture(&editor, "synthesis-strip-pending");
		QCOMPARE(strip->height(), 4);
		auto stripImage = [&] {
			const auto origin = strip->mapToGlobal(QPoint());
			return strip->screen()->grabWindow(0, origin.x(), origin.y(), strip->width(), strip->height()).toImage();
		};
		QCOMPARE(stripImage().pixelColor(100, 2), QColor("#e5c35b"));
		track->bindVoice("org.lmms.svs.example", "full");
		QTRY_VERIFY_WITH_TIMEOUT(track->capabilitiesReady(), 10000);
		QVector<svs::Note> notes;
		for (int index = 0; index < 64; ++index)
		{
			svs::Note note;
			note.id = QUuid::createUuid().toString();
			note.duration = 768;
			note.pitch = 60;
			note.lyric = "a";
			notes.append(note);
		}
		clip->setNotes(notes);
		clip->synthesize();
		const auto renderingPrefix = QCoreApplication::translate("NativeSVS", "Rendering %1/%2").section("%1", 0, 0);
		QTRY_VERIFY_WITH_TIMEOUT(clip->status() == "Rendering" || clip->status().startsWith(renderingPrefix), 10000);
		QTest::qWait(80);
		const auto first = stripImage();
		QTest::qWait(160);
		QVERIFY(clip->status() == "Rendering" || clip->status().startsWith(renderingPrefix));
		QVERIFY(stripImage() != first);
		QVERIFY(editor.screen()->grabWindow(editor.winId()).save(m_output + "/synthesis-strip-rendering.png"));
		QTRY_COMPARE_WITH_TIMEOUT(clip->status(), QString("Ready"), 30000);
		QTest::qWait(100);
		QCOMPARE(stripImage().pixelColor(100, 2), QColor("#69c78f"));
		capture(&editor, "synthesis-strip-ready");
		const auto completed = stripImage();
		QTest::qWait(160);
		QCOMPARE(stripImage(), completed);
		clip->setNotes({});
		QVERIFY(!strip->isVisible());
		capture(&editor, "synthesis-strip-deleted");
		clip->setNotes(notes);
		QVERIFY(strip->isVisible());
		QTRY_COMPARE_WITH_TIMEOUT(clip->status(), QString("Ready"), 30000);
		canvas->setScroll(700, canvas->topPitch());
		QTest::qWait(100);
		QCOMPARE(stripImage().pixelColor(100, 2), QColor("#69c78f"));
		QVERIFY(stripImage().pixelColor(250, 2) != QColor("#69c78f"));
		canvas->setScroll(0, canvas->topPitch());
		QDomDocument document;
		auto node = document.createElement("svsclip");
		clip->saveSettings(document, node);
		node.setAttribute("seed", "invalid");
		clip->loadSettings(node);
		capture(&editor, "synthesis-strip-error");
		QCOMPARE(stripImage().pixelColor(100, 2), QColor("#e77474"));
		editor.setStyleSheet("lmms--gui--SVSPianoRoll { qproperty-synthesisErrorColor: #c04080; }");
		QTest::qWait(100);
		QCOMPARE(stripImage().pixelColor(100, 2), QColor("#c04080"));
		editor.close();
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
			QVERIFY(!editor.findChild<QLabel*>("svsSynthesisStatus"));
			auto* ruler = editor.findChild<QWidget*>("svsClipTimeline");
			QVERIFY(ruler);
			const auto fullText = nativeTranslation::svsStatus(clip->status());
			QCOMPARE(ruler->toolTip(), fullText);
			hoverCapture(ruler, fullText, prefix + "-svs-status-tooltip");
			editor.resize(900, 600);
			capture(&editor, prefix + "-svs-narrow");
			QCOMPARE(ruler->toolTip(), fullText);
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
