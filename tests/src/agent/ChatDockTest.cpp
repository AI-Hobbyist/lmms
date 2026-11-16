#include "agent/chat/ChatDock.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

#include "ConfigManager.h"
#include "LmmsPalette.h"
#include "LmmsStyle.h"
#include "agent/chat/ChatSessionsDialog.h"

class ChatDockTest : public QObject
{
	Q_OBJECT
private slots:
	void nativeDockLifecycle()
	{
		QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
		QStandardPaths::setTestModeEnabled(true);
		QCoreApplication::setApplicationName("LMMS-AgentChatTest");
		QTemporaryDir profile;
		const auto configPath = profile.filePath("config.xml");
		lmms::ConfigManager::inst()->loadConfigFile(configPath);
		QDir::addSearchPath("resources", lmms::ConfigManager::inst()->themeDir());
		QDir::addSearchPath("resources", lmms::ConfigManager::inst()->defaultThemeDir());
		QDir::addSearchPath("artwork", lmms::ConfigManager::inst()->themeDir());
		QDir::addSearchPath("artwork", lmms::ConfigManager::inst()->defaultThemeDir());
		auto* style = new lmms::gui::LmmsStyle;
		QApplication::setStyle(style);
		lmms::gui::LmmsPalette palette(nullptr, style);
		QApplication::setPalette(palette.palette());
		QMainWindow window;
		window.setWindowTitle(QStringLiteral("LMMS Agent 原生窗口验收"));
		window.resize(1600, 878);
		window.setCentralWidget(new QWidget(&window));
		auto* dock = new lmms::agent::chat::ChatDock(&window);
		window.show();
		QVERIFY(QTest::qWaitForWindowExposed(&window));
		window.resizeDocks({dock}, {640}, Qt::Horizontal);
		QTRY_COMPARE(dock->size(), QSize(640, 878));
		auto* newSession = dock->findChild<QToolButton*>("agentNewSession");
		auto* sessionsToggle = dock->findChild<QToolButton*>("agentSessionToggle");
		QVERIFY(newSession);
		QVERIFY(sessionsToggle);
		// Latest acceptance requires the actual Penpot structure: the three header
		// actions share one row; New Session is below it at (14, 70).
		QCOMPARE(newSession->mapTo(dock, QPoint{}), QPoint(14, 70));
		QCOMPARE(newSession->size(), QSize(152, 36));
		auto* detach = dock->findChild<QToolButton*>("agentDetach");
		auto* collapse = dock->findChild<QToolButton*>("agentCollapse");
		QVERIFY(detach);
		QVERIFY(collapse);
		QCOMPARE(detach->mapTo(dock, QPoint{}).y(), sessionsToggle->mapTo(dock, QPoint{}).y());
		QCOMPARE(collapse->mapTo(dock, QPoint{}).y(), sessionsToggle->mapTo(dock, QPoint{}).y());
		auto* composer = dock->findChild<QWidget*>("agentComposer");
		QVERIFY(composer);
		QCOMPARE(composer->mapTo(dock, QPoint{}), QPoint(200, 681));
		QCOMPARE(composer->size(), QSize(420, 155));
		auto* input = dock->findChild<QPlainTextEdit*>("agentInput");
		QVERIFY(input);
		input->setFocus();
		input->clear();
		QTest::keyClicks(input, "C major ");
		input->insertPlainText(QStringLiteral("五声音阶"));
		QCOMPARE(input->toPlainText(), QStringLiteral("C major 五声音阶"));
		dock->setCollapsed(true);
		QTRY_VERIFY(!input->isVisible());
		QTRY_COMPARE(dock->width(), 48);
		const auto evidence = qEnvironmentVariable("LMMS_CHAT_SCREENSHOT");
		QTest::qWait(200);
		if (!evidence.isEmpty())
		{
			QVERIFY(window.screen()
						->grabWindow(window.winId(), dock->x(), dock->y(), dock->width(), dock->height())
						.save(QFileInfo(evidence).absolutePath() + "/agent-chat-collapsed-native.png"));
		}
		dock->setCollapsed(false);
		QTRY_VERIFY(input->isVisible());
		window.resizeDocks({dock}, {560}, Qt::Horizontal);
		QTRY_COMPARE(dock->width(), 560);
		auto* approval = dock->findChild<QToolButton*>("agentApproval");
		auto* send = dock->findChild<QToolButton*>("agentSend");
		QVERIFY(approval);
		QVERIFY(send);
		QVERIFY(approval->geometry().right() < send->geometry().left());
		window.resizeDocks({dock}, {640}, Qt::Horizontal);
		dock->setFloating(true);
		QTRY_VERIFY(dock->isFloating());
		QVERIFY(QTest::qWaitForWindowExposed(dock));
		dock->resize(1100, 1000);
		dock->move(dock->screen()->availableGeometry().topLeft() + QPoint(20, 20));
		QTest::qWait(300);
		QCOMPARE(newSession->mapTo(dock, QPoint{}), QPoint(14, 168));
		QCOMPARE(newSession->size(), QSize(192, 36));
		QCOMPARE(composer->mapTo(dock, QPoint{}), QPoint(240, 779));
		QCOMPARE(composer->size(), QSize(840, 155));
		auto* redock = dock->findChild<QToolButton*>("agentRedock");
		QVERIFY(redock);
		QCOMPARE(redock->mapTo(dock, QPoint{}), QPoint(925, 49));
		if (!evidence.isEmpty())
		{
			QVERIFY(dock->screen()
						->grabWindow(dock->winId())
						.save(QFileInfo(evidence).absolutePath() + "/agent-chat-floating-native.png"));
		}
		QTest::mouseClick(redock, Qt::LeftButton);
		QTRY_VERIFY(!dock->isFloating());
		window.resizeDocks({dock}, {640}, Qt::Horizontal);
		QFile prompt(profile.filePath("prompt.txt"));
		QVERIFY(prompt.open(QIODevice::WriteOnly));
		prompt.write(QStringLiteral("测试音乐助手").toUtf8());
		prompt.close();
		bool defaultApproval = false;
		bool settingsSaved = false;
		bool settingsGeometry = false;
		QTimer::singleShot(0, &window, [&] {
			auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
			if (!dialog)
			{
				return;
			}
			auto* path = dialog->findChild<QLineEdit*>("agentPromptPath");
			auto* request = dialog->findChild<QAbstractButton*>("agentRequestApproval");
			auto* noApproval = dialog->findChild<QAbstractButton*>("agentNoApproval");
			auto* approvalTab = dialog->findChild<QAbstractButton*>("agentSettingsTab3");
			auto* globalTab = dialog->findChild<QAbstractButton*>("agentSettingsTab2");
			auto* buttons = dialog->findChild<QDialogButtonBox*>();
			if (!path || !request || !noApproval || !buttons || !approvalTab || !globalTab)
			{
				dialog->reject();
				return;
			}
			QTest::qWait(300);
			settingsGeometry = path->mapTo(dialog, QPoint{}) == QPoint(306, 352) && path->size() == QSize(610, 43);
			const auto evidence = qEnvironmentVariable("LMMS_CHAT_SCREENSHOT");
			const auto directory = QFileInfo(evidence).absolutePath();
			if (!evidence.isEmpty())
			{
				settingsGeometry = settingsGeometry
					&& dialog->screen()
						   ->grabWindow(dialog->winId())
						   .save(directory + "/agent-chat-settings-native.png");
			}
			QTest::mouseClick(approvalTab, Qt::LeftButton);
			QTest::qWait(300);
			defaultApproval = request->isChecked();
			settingsGeometry = settingsGeometry && request->mapTo(dialog, QPoint{}) == QPoint(280, 208)
				&& request->size() == QSize(850, 222);
			if (!evidence.isEmpty())
			{
				settingsGeometry = settingsGeometry
					&& dialog->screen()
						   ->grabWindow(dialog->winId())
						   .save(directory + "/agent-chat-approval-native.png");
			}
			QTest::mouseClick(noApproval, Qt::LeftButton);
			QTest::mouseClick(globalTab, Qt::LeftButton);
			path->setText(prompt.fileName());
			buttons->button(QDialogButtonBox::Save)->click();
			settingsSaved = dialog->result() == QDialog::Accepted;
		});
		auto* settings = dock->findChild<QToolButton*>("agentSettings");
		QVERIFY(settings);
		QTest::mouseClick(settings, Qt::LeftButton);
		QVERIFY(defaultApproval);
		QVERIFY(settingsSaved);
		QVERIFY(settingsGeometry);
		lmms::ConfigManager::inst()->setValue("agentchat", "approval", "request");
		lmms::ConfigManager::inst()->loadConfigFile(configPath);
		QCOMPARE(lmms::ConfigManager::inst()->value("agentchat", "approval"), QStringLiteral("none"));
		QCOMPARE(lmms::ConfigManager::inst()->value("agentchat", "promptPath"), prompt.fileName());
		QTest::qWait(700);
		const auto path = qEnvironmentVariable("LMMS_CHAT_SCREENSHOT");
		if (!path.isEmpty())
		{
			QVERIFY(window.screen()
						->grabWindow(window.winId(), dock->x(), dock->y(), dock->width(), dock->height())
						.save(path));
		}
		window.close();
	}

	void nativeSessionManagement()
	{
		QTemporaryDir directory;
		lmms::agent::chat::ChatStore store(directory.path());
		QStringList ids;
		for (const auto& title :
			 {QStringLiteral("和弦与 Pad 编配"), QStringLiteral("整理鼓组力度"), QStringLiteral("主旋律音色建议"),
			  QStringLiteral("混音电平检查"), QStringLiteral("Lo-fi 鼓组素材搜索")})
		{
			auto session = lmms::agent::chat::ChatStore::newSession();
			session.insert("title", title);
			QString error;
			QVERIFY2(store.saveSession(session, error), qPrintable(error));
			ids.append(session.value("id").toString());
		}
		lmms::agent::chat::ChatSessionsDialog dialog(store);
		dialog.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dialog));
		auto* search = dialog.findChild<QLineEdit*>("agentManagerSearch");
		QVERIFY(search);
		QCOMPARE(search->mapTo(&dialog, QPoint{}), QPoint(48, 215));
		QCOMPARE(search->size(), QSize(716, 44));
		const auto titles = dialog.findChildren<QLabel*>(QRegularExpression("^agentManagerTitle_"));
		QCOMPARE(titles.size(), 5);
		int firstTitleY = 1000;
		for (const auto* title : titles)
		{
			QCOMPARE(title->mapTo(&dialog, QPoint{}).x(), 133);
			firstTitleY = qMin(firstTitleY, title->mapTo(&dialog, QPoint{}).y());
		}
		QCOMPARE(firstTitleY, 491);
		for (int i = 0; i < 2; ++i)
		{
			auto* check = dialog.findChild<QCheckBox*>("agentManagerSelect_" + ids[i]);
			QVERIFY(check);
			check->click();
		}
		QTest::qWait(400);
		const auto evidence = qEnvironmentVariable("LMMS_CHAT_SCREENSHOT");
		if (!evidence.isEmpty())
		{
			QVERIFY(dialog.screen()
						->grabWindow(dialog.winId())
						.save(QFileInfo(evidence).absolutePath() + "/agent-chat-sessions-native.png"));
		}
		auto* archive = dialog.findChild<QToolButton*>("agentManagerArchive");
		QVERIFY(archive);
		QVERIFY(archive->isEnabled());
		archive->click();
		QVERIFY(store.session(ids[0]).value("archived").toBool());
		QVERIFY(store.session(ids[1]).value("archived").toBool());
		search->setText(QStringLiteral("主旋律"));
		auto* open = dialog.findChild<QToolButton*>("agentManagerOpen_" + ids[2]);
		QVERIFY(open);
		open->click();
		QCOMPARE(dialog.selectedSession(), ids[2]);
		QCOMPARE(dialog.result(), int(QDialog::Accepted));
	}
};

QTEST_MAIN(ChatDockTest)
#include "ChatDockTest.moc"
