#include <QtTest>
#include <QTemporaryDir>
#include <QScreen>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QWheelEvent>
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
	void legacyAndRepolish()
	{
		qApp->setStyleSheet("");LmmsPalette legacyPalette(nullptr,qApp->style());QVERIFY(!legacyPalette.flatFrames());QVERIFY(!LmmsStyle::s_flatFrames);
		QWidget legacy;QVBoxLayout layout(&legacy);ComboBox combo;combo.model()->addItem("Legacy");layout.addWidget(&combo);
		GroupBox group("Legacy",&legacy);group.setMinimumHeight(60);layout.addWidget(&group);
		TabWidget tabs("",&legacy);tabs.setMinimumHeight(80);tabs.addTab(new QWidget(&tabs),"Old");layout.addWidget(&tabs);
		legacy.resize(360,220);legacy.show();QVERIFY(QTest::qWaitForWindowExposed(&legacy));
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
