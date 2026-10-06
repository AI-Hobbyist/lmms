#include <QtTest>
#include <QTemporaryDir>
#include <QScreen>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QWheelEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QTreeWidget>
#include <QToolButton>
#include "PluginBrowser.h"
#include "SongEditor.h"
#include "TrackLabelButton.h"
#include "Clipboard.h"
#include <QMimeData>
#include <QDropEvent>
#include <QDragEnterEvent>
#include "Knob.h"
#include "LcdSpinBox.h"
#include "LcdFloatSpinBox.h"
#include "Fader.h"
#include "LedCheckBox.h"
#include "PianoView.h"
#include "Piano.h"
#include "NotePlayHandle.h"
#include "InstrumentTrack.h"
#include "Instrument.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "ProjectJournal.h"
#include "Graph.h"
#include "AutomationClip.h"
#include "AudioDummy.h"
#include "ConfigManager.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "Song.h"
#include "ComboBox.h"
#include "TabWidget.h"
#include "GroupBox.h"
#include "SubWindow.h"
#include "LmmsPalette.h"
#include "LmmsStyle.h"

using namespace lmms;
using namespace lmms::gui;

class ThemeWidgetTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_config;
	std::unique_ptr<GuiApplication> m_gui;
	QString m_theme;
	QString m_themeFile;
	void capture(QWidget* window, const QString& name)
	{
		window->show(); window->raise(); window->activateWindow();
		QVERIFY(QTest::qWaitForWindowExposed(window->window())); QTest::qWait(400);
		const auto output=qEnvironmentVariable("LMMS_UI_EVIDENCE");
		QVERIFY(QDir().mkpath(output));
		QVERIFY(window->screen()->grabWindow(window->window()->winId()).save(output+'/'+name+".png"));
	}
	void wheel(QWidget* widget, int delta)
	{
		const auto point=QPoint(8,5);
		QWheelEvent event(point,widget->mapToGlobal(point),{},QPoint(0,delta),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
		QApplication::sendEvent(widget,&event);
	}
private slots:
	void initTestCase()
	{
		QCOMPARE(QGuiApplication::platformName(),QString("windows")); QVERIFY(m_config.isValid());
			NotePlayHandleManager::init();
		auto* config=ConfigManager::inst();config->loadConfigFile(m_config.filePath("theme.xml"));
		config->setWorkingDir(m_config.path()+'/');config->setValue("app","configured","1");
		config->setValue("audioengine","audiodev",AudioDummy::name());
		const auto themeDir=m_config.filePath("theme");QVERIFY(QDir().mkpath(themeDir));
		const auto source=qEnvironmentVariable("LMMS_DATA_DIR")+"/themes/default/";
		m_themeFile=themeDir+"/style.css";QVERIFY(QFile::copy(source+"style.css",m_themeFile));
		QVERIFY(QFile::copy(source+"ui-down.svg",themeDir+"/ui-down.svg"));config->setThemeDir(themeDir+'/');
		QDir::setSearchPaths("resources",{themeDir,config->defaultThemeDir()});
		m_gui=std::make_unique<GuiApplication>();m_gui->mainWindow()->resize(1100,750);
		m_gui->mainWindow()->show();QVERIFY(QTest::qWaitForWindowExposed(m_gui->mainWindow()));
		m_theme=qApp->styleSheet();QVERIFY(LmmsStyle::s_flatFrames);
		QCOMPARE(qApp->palette().color(QPalette::Window),QColor("#20262D"));
	}
	void modelsTabsAndPopup()
	{
		QWidget window;QVBoxLayout layout(&window);
		ComboBox combo;combo.model()->addItem("First / 中文");combo.model()->addItem("Second");combo.model()->addItem("Third");layout.addWidget(&combo);
		TabWidget tabs("Caption",&window,true);tabs.setMinimumHeight(160);layout.addWidget(&tabs);
		for(int id:{2,7,11}) tabs.addTab(new QWidget(&tabs),QString::number(id),"gear",id);
		tabs.setActiveTab(2);window.resize(500,240);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
		QVERIFY(combo.property("flatStyle").toBool());QVERIFY(tabs.flatStyle());
		QTest::mouseClick(&combo,Qt::LeftButton,Qt::NoModifier,QPoint(8,8));QCOMPARE(combo.model()->value(),1);
		QTest::mouseClick(&combo,Qt::RightButton,Qt::NoModifier,QPoint(8,8));QCOMPARE(combo.model()->value(),0);
		wheel(&combo,-120);QCOMPARE(combo.model()->value(),1);
		bool popupChosen=false;
		QTimer::singleShot(100,[&] {
			if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget()))
			{
				popupChosen=menu->isVisible()&&menu->actions().size()==3;
				menu->actions().at(2)->trigger();menu->close();
			}
		});
		QTest::mouseClick(&combo,Qt::LeftButton,Qt::NoModifier,QPoint(combo.width()-4,8));
		QVERIFY(popupChosen);QCOMPARE(combo.model()->value(),2);
		wheel(&tabs,-120);QCOMPARE(tabs.activeTab(),7);wheel(&tabs,-120);QCOMPARE(tabs.activeTab(),11);
		wheel(&tabs,120);QCOMPARE(tabs.activeTab(),7);
		window.resize(620,240);
		QPoint target;
		for(int x=0;x<tabs.width();++x) if(tabs.findTabAtPos(QPoint(x,5))==11) {target=QPoint(x+2,5);break;}
		QVERIFY(!target.isNull());QTest::mouseClick(&tabs,Qt::LeftButton,Qt::NoModifier,target);QCOMPARE(tabs.activeTab(),11);
		capture(&window,"F2-widgets");window.close();
	}
		void instrumentBrowser()
		{
			auto* browser=m_gui->mainWindow()->findChild<PluginBrowser*>();QVERIFY(browser);
			QToolButton* opener=nullptr;
			for(auto* button:m_gui->mainWindow()->findChildren<QToolButton*>())
				if(button->toolTip()==browser->title()) {opener=button;break;}
			QVERIFY(opener);auto* tree=browser->findChild<QTreeWidget*>();QVERIFY(tree);
			for(int iteration=0;iteration<3;++iteration)
			{
				QTest::mouseClick(opener,Qt::LeftButton);QTRY_VERIFY(browser->isVisible());
				QElapsedTimer elapsed;elapsed.start();bool responsive=false;
				QTimer::singleShot(100,[&]{responsive=true;});QTRY_VERIFY_WITH_TIMEOUT(responsive,2000);
				QVERIFY(elapsed.elapsed()<2000);QVERIFY(tree->topLevelItemCount()>0);
				tree->expandAll();QTest::qWait(200);tree->collapseAll();tree->expandAll();
				auto* search=browser->findChild<QLineEdit*>();QVERIFY(search);
				search->setFocus();QTest::keyClicks(search,"Triple");QTest::qWait(100);
				QTest::keyClick(search,Qt::Key_A,Qt::ControlModifier);QTest::keyClick(search,Qt::Key_Backspace);
				if(iteration==0) {capture(m_gui->mainWindow(),"F3-instrument-browser");}
				QTest::mouseClick(opener,Qt::LeftButton);QTRY_VERIFY(!browser->isVisible());
			}
		}
		void droppedInstrumentWindow()
		{
			auto* editor=m_gui->songEditor()->m_editor;
			m_gui->songEditor()->show();m_gui->songEditor()->parentWidget()->show();
			QTest::qWait(200);
			for(const auto& name:{QString("tripleoscillator"),QString("kicker")})
			{
				QMimeData mime;mime.setData(Clipboard::mimeType(Clipboard::MimeType::StringPair),("instrument:"+name).toUtf8());
				QDragEnterEvent enter(QPoint(500,200),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
				QApplication::sendEvent(editor,&enter);QVERIFY(enter.isAccepted());
				QDropEvent drop(QPointF(500,200),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
				QApplication::sendEvent(editor,&drop);QVERIFY(drop.isAccepted());
				InstrumentTrackView* view=nullptr;
				QTRY_VERIFY_WITH_TIMEOUT(([&]{for(auto* candidate:editor->findChildren<InstrumentTrackView*>())
					{auto* instrument=candidate->model()->instrument();if(instrument&&QString::fromUtf8(instrument->descriptor()->name)==name){view=candidate;return true;}}return false;})(),5000);
				auto* label=view->findChild<TrackLabelButton*>();QVERIFY(label);QTRY_VERIFY(label->isEnabled());
				QTest::mouseClick(label,Qt::LeftButton,Qt::NoModifier,QPoint(10,label->height()/2));
				auto* window=view->getInstrumentTrackWindow();QTRY_VERIFY(window->isVisible());
				capture(m_gui->mainWindow(),"F3-dropped-"+name);
				QTest::mouseClick(label,Qt::LeftButton,Qt::NoModifier,QPoint(10,label->height()/2));
				QTRY_VERIFY(!window->isVisible());
			}
		}
	void groupAndWindowLifecycle()
	{
		auto* panel=new QWidget;auto* layout=new QVBoxLayout(panel);
		auto* group=new GroupBox("Enabled / 启用",panel);group->setMinimumSize(320,140);layout->addWidget(group);
		auto* label=new QLabel("Content",group);label->move(6,group->titleBarHeight()+6);
		auto* frame=m_gui->mainWindow()->addWindowedWidget(panel);frame->resize(450,230);frame->show();
		QTest::qWait(200);QVERIFY(group->flatStyle());QVERIFY(label->y()>group->titleBarHeight());
		const bool before=group->model()->value();QTest::mouseClick(group,Qt::LeftButton,Qt::NoModifier,QPoint(40,5));QCOMPARE(group->model()->value(),!before);
		frame->showMaximized();QTRY_VERIFY(frame->isMaximized());frame->showNormal();QTRY_VERIFY(!frame->isMaximized());
		frame->detach();QVERIFY(frame->isDetached());QVERIFY(QTest::qWaitForWindowExposed(panel));capture(panel,"F2-detached");
		frame->attach();QVERIFY(!frame->isDetached());QVERIFY(frame->isVisible());capture(m_gui->mainWindow(),"F2-attached");
		frame->close();QVERIFY(!frame->isVisible());delete panel;QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
	}
	void reloadThemeAssets()
	{
		const auto previous=embed::getIconPixmap("ui-down").toImage();
		const auto iconPath=m_config.filePath("theme/ui-down.svg");QFile icon(iconPath);QVERIFY(icon.open(QIODevice::ReadOnly));
		auto svg=icon.readAll();icon.close();svg.replace("#FFFFFF","#E05B65");QVERIFY(icon.open(QIODevice::WriteOnly));icon.write(svg);icon.close();
		QFile theme(m_themeFile);QVERIFY(theme.open(QIODevice::WriteOnly));
		auto changed=m_theme;changed.replace("qproperty-background: #20262D","qproperty-background: #14181D");theme.write(changed.toUtf8());theme.close();
		QTRY_COMPARE(qApp->palette().color(QPalette::Window),QColor("#14181D"));
		QVERIFY(embed::getIconPixmap("ui-down").toImage()!=previous);
		ComboBox combo;combo.model()->addItem("Reloaded / 重载");combo.resize(220,22);capture(&combo,"F2-reloaded-assets");combo.close();
		QVERIFY(theme.open(QIODevice::WriteOnly));theme.write(m_theme.toUtf8());theme.close();
		QTRY_COMPARE(qApp->palette().color(QPalette::Window),QColor("#20262D"));
	}
	void numericControls()
	{
		QWidget window;QHBoxLayout layout(&window);
		FloatModel gain(40,0,100,1);FloatModel logarithmic(440,20,20000,1);logarithmic.setScaleType(AutomatableModel::ScaleType::Logarithmic);
		AutomationClip automation(nullptr);QVERIFY(automation.addObject(&gain));const auto endpointId=gain.id();
		Knob knob(KnobType::Bright26,"Gain",&window);knob.setModel(&gain);layout.addWidget(&knob);
		Knob logKnob(KnobType::Bright26,"Hz",&window);logKnob.setModel(&logarithmic);layout.addWidget(&logKnob);
		IntModel integer(123,0,999);LcdSpinBox lcd(3,&window);lcd.setModel(&integer);lcd.setLabel("Integer");layout.addWidget(&lcd);
		FloatModel fractional(-.25f,-99,99,.01f);LcdFloatSpinBox decimal(3,2,"Decimal",&window);decimal.setModel(&fractional);decimal.setLabel("Decimal");layout.addWidget(&decimal);
		FloatModel volume(1,0,2,.001f);Fader fader(&volume,"Volume",&window);fader.setFixedSize(32,160);layout.addWidget(&fader);
		LedCheckBox led("Enabled / 启用",&window);layout.addWidget(&led);
		Graph graph(&window);graph.setMinimumSize(132,104);graph.model()->setWaveToSine();layout.addWidget(&graph);
		window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTest::qWait(200);
		QVERIFY(knob.property("flatStyle").toBool());QVERIFY(lcd.textMode());QVERIFY(decimal.textMode());QVERIFY(fader.property("flatStyle").toBool());
		wheel(&lcd,120);QCOMPARE(integer.value(),124);
		const QPoint fractionPoint(decimal.width()-4,5);
		QWheelEvent fractionWheel(fractionPoint,decimal.mapToGlobal(fractionPoint),{},QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
		QApplication::sendEvent(&decimal,&fractionWheel);QVERIFY(qAbs(fractional.value()+.24f)<.001);
		wheel(&decimal,120);QVERIFY(qAbs(fractional.value()-.76f)<.001);
		wheel(&knob,120);const float modernGain=gain.value();gain.setValue(40);knob.setProperty("flatStyle",false);
		wheel(&knob,120);QCOMPARE(gain.value(),modernGain);knob.setProperty("flatStyle",true);
		wheel(&logKnob,120);const float modernLog=logarithmic.value();logarithmic.setValue(440);logKnob.setProperty("flatStyle",false);
		wheel(&logKnob,120);QCOMPARE(logarithmic.value(),modernLog);logKnob.setProperty("flatStyle",true);
		Engine::projectJournal()->setJournalling(true);gain.setJournalling(true);gain.setValue(40);
		QTest::mousePress(&knob,Qt::LeftButton,Qt::NoModifier,QPoint(12,18));
		QTest::mouseMove(&knob,QPoint(12,4));QTest::mouseRelease(&knob,Qt::LeftButton,Qt::NoModifier,QPoint(12,4));
		QVERIFY(gain.value()!=40);Engine::projectJournal()->undo();QCOMPARE(gain.value(),40.f);
		bool entered=false;QTimer::singleShot(100,[&]{if(auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {entered=true;dialog->setDoubleValue(65);dialog->accept();}});
		QTest::mouseDClick(&knob,Qt::LeftButton);QVERIFY(entered);QCOMPARE(gain.value(),65.f);
		QCOMPARE(gain.id(),endpointId);QCOMPARE(automation.objects().front().data(),static_cast<AutomatableModel*>(&gain));
		QTest::mouseClick(&fader,Qt::LeftButton,Qt::NoModifier,QPoint(16,159));QCOMPARE(volume.value(),0.f);QVERIFY(fader.toolTip().contains("-inf"));
		fader.adjustByDecibelDelta(1);QVERIFY(volume.value()>0);volume.setValue(1);QVERIFY(fader.toolTip().contains("0.00"));
		fader.adjustByDecibelDelta(1);QVERIFY(qAbs(volume.value()-dbfsToAmp(1))<.002);
		fader.setPeak_L(.5f);fader.setPeak_R(1.2f);QCOMPARE(fader.getPeak_L(),.5f);QCOMPARE(fader.getPeak_R(),1.2f);
		QSignalSpy ledChanges(led.model(),SIGNAL(dataChanged()));const bool previous=led.model()->value();
		QTest::mouseClick(&led,Qt::LeftButton);QCOMPARE(led.model()->value(),!previous);QCOMPARE(ledChanges.size(),1);
		capture(&window,"F3-numeric-controls");knob.setEnabled(false);lcd.setEnabled(false);led.setEnabled(false);capture(&window,"F3-disabled-controls");
		auto larger=lcd.font();larger.setPixelSize(20);lcd.setFont(larger);led.setFont(larger);
		QVERIFY(lcd.cellHeight()>=QFontMetrics(larger).height());QVERIFY(led.height()>=QFontMetrics(larger).height());
		capture(&window,"F3-font-change");
		window.close();
	}
	void pianoKeys()
	{
		auto* track=new InstrumentTrack(Engine::getSong());QVERIFY(track->loadInstrument("tripleoscillator"));
			QCoreApplication::processEvents();
		InstrumentTrackView* view=nullptr;
		for(auto* candidate:m_gui->mainWindow()->findChildren<InstrumentTrackView*>()) if(candidate->model()==track) {view=candidate;break;}
		QVERIFY(view);auto* window=view->getInstrumentTrackWindow();auto* piano=window->findChild<PianoView*>();QVERIFY(piano);
		window->show();auto* frame=qobject_cast<QMdiSubWindow*>(window->parentWidget());QVERIFY(frame);frame->show();frame->raise();
		QVERIFY(QTest::qWaitForWindowExposed(m_gui->mainWindow()));QTest::qWait(200);QVERIFY(piano->property("flatStyle").toBool());
		for(const auto key:{Qt::Key_Z,Qt::Key_S})
		{
				const quint32 scan=key==Qt::Key_Z?44:31;
				QKeyEvent press(QEvent::KeyPress,key,Qt::NoModifier,scan,key,0);
				const int note=PianoView::getKeyFromKeyEvent(&press);QVERIFY(note>=0&&note<NumKeys);
				QApplication::sendEvent(piano,&press);QVERIFY(track->pianoModel()->isKeyPressed(note));capture(m_gui->mainWindow(),QString("F3-piano-%1").arg(key));
				QKeyEvent release(QEvent::KeyRelease,key,Qt::NoModifier,scan,key,0);
				QApplication::sendEvent(piano,&release);QVERIFY(!track->pianoModel()->isKeyPressed(note));
		}
		window->close();frame->hide();
			// Keep the song-owned model alive until the actual GUI views are destroyed.
	}
	void legacyAndRepolish()
	{
		qApp->setStyleSheet("");LmmsPalette legacyPalette(nullptr,qApp->style());QVERIFY(!legacyPalette.flatFrames());QVERIFY(!LmmsStyle::s_flatFrames);
		QWidget legacy;QVBoxLayout layout(&legacy);ComboBox combo;combo.model()->addItem("Legacy");layout.addWidget(&combo);
		GroupBox group("Legacy",&legacy);group.setMinimumHeight(60);layout.addWidget(&group);
		TabWidget tabs("",&legacy);tabs.setMinimumHeight(80);tabs.addTab(new QWidget(&tabs),"Old");layout.addWidget(&tabs);
		Knob oldKnob(KnobType::Bright26,"Legacy",&legacy);layout.addWidget(&oldKnob);
		LcdSpinBox oldLcd(3,&legacy);layout.addWidget(&oldLcd);LcdFloatSpinBox oldDecimal(3,2,"Legacy",&legacy);layout.addWidget(&oldDecimal);
		LedCheckBox oldLed("Legacy",&legacy);layout.addWidget(&oldLed);
		legacy.resize(360,400);legacy.show();QVERIFY(QTest::qWaitForWindowExposed(&legacy));
		QVERIFY(!oldKnob.property("flatStyle").toBool());QVERIFY(!oldLcd.textMode());QVERIFY(!oldDecimal.textMode());QVERIFY(!oldLed.flatStyle());
		QVERIFY(!combo.property("flatStyle").toBool());QVERIFY(!group.flatStyle());QVERIFY(!tabs.flatStyle());QCOMPARE(group.titleBarHeight(),11);
		capture(&legacy,"F2-legacy");legacy.close();
		qApp->setStyleSheet(m_theme);LmmsPalette modernPalette(nullptr,qApp->style());QVERIFY(modernPalette.flatFrames());
		qApp->setPalette(modernPalette.palette());QTest::qWait(100);
		ComboBox reopened;reopened.model()->addItem("Reopened");reopened.resize(200,22);reopened.show();
		QVERIFY(QTest::qWaitForWindowExposed(&reopened));QVERIFY(reopened.property("flatStyle").toBool());capture(&reopened,"F2-reopened");reopened.close();
	}
	void cleanup()
	{
		qApp->setStyleSheet(m_theme);LmmsPalette palette(nullptr,qApp->style());qApp->setPalette(palette.palette());
	}
	void cleanupTestCase()
	{
		Engine::getSong()->setModified(false);delete static_cast<QWidget*>(m_gui->mainWindow());m_gui.reset();
	}
};
QTEST_MAIN(ThemeWidgetTest)
#include "ThemeWidgetTest.moc"
