#include "agent/chat/ChatDock.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QtTest>

#include "ConfigManager.h"
#include "Engine.h"
#include "LmmsPalette.h"
#include "LmmsStyle.h"
#include "agent/chat/ChatAdvancedDialog.h"
#include "agent/chat/ChatApiSettingsPage.h"
#include "agent/chat/ChatAttachmentsDialog.h"
#include "agent/chat/ChatController.h"
#include "agent/chat/ChatGenerationPage.h"
#include "agent/chat/ChatSessionsDialog.h"
#include "agent/chat/ChatSettingsDialog.h"
#include "agent/chat/ChatTranscript.h"

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

	void nativeApiSettings()
	{
		QTranslator translator;
		QVERIFY(translator.load(qEnvironmentVariable("LMMS_DATA_DIR") + "/locale/zh_CN.qm"));
		QVERIFY(QCoreApplication::installTranslator(&translator));
		const auto removeTranslator = qScopeGuard([&] { QCoreApplication::removeTranslator(&translator); });
		QTemporaryDir profile;
		lmms::agent::chat::ChatConnectionStore store(profile.path());
		QDialog window;
		window.resize(1600, 1000);
		auto* page = new lmms::agent::chat::ChatApiSettingsPage(store, &window);
		page->setGeometry(232, 66, 1368, 874);
		window.show();
		QVERIFY(QTest::qWaitForWindowExposed(&window));
		auto* preset = page->findChild<QToolButton*>("agentPreset_deepseek");
		QVERIFY(preset);
		QCOMPARE(preset->mapTo(&window, QPoint{}), QPoint(280, 264));
		QCOMPARE(preset->size(), QSize(302, 98));
		QTest::qWait(250);
		const auto evidence = qEnvironmentVariable("LMMS_CHAT_SCREENSHOT");
		if (!evidence.isEmpty())
		{
			QVERIFY(window.screen()
						->grabWindow(window.winId(), 232, 66, 1368, 874)
						.save(QFileInfo(evidence).absolutePath() + "/agent-chat-presets-native.png"));
		}
		QTest::mouseClick(preset, Qt::LeftButton);
		auto* name = page->findChild<QLineEdit*>("agentConnectionName");
		auto* address = page->findChild<QLineEdit*>("agentApiAddress");
		auto* key = page->findChild<QLineEdit*>("agentApiKey");
		auto* model = page->findChild<QComboBox*>("agentModelId");
		auto* format = page->findChild<QComboBox*>("agentRequestFormat");
		QVERIFY(name && address && key && model && format);
		QCOMPARE(name->mapTo(&window, QPoint{}), QPoint(530, 231));
		QCOMPARE(address->mapTo(&window, QPoint{}), QPoint(530, 442));
		QCOMPARE(key->mapTo(&window, QPoint{}), QPoint(530, 539));
		QCOMPARE(model->mapTo(&window, QPoint{}), QPoint(530, 636));
		QCOMPARE(name->size(), QSize(642, 43));
		QCOMPARE(format->count(), 8);
		QCOMPARE(key->echoMode(), QLineEdit::Password);
		QCOMPARE(address->text(), QStringLiteral("https://api.deepseek.com"));
		QCOMPARE(page->findChild<QLabel*>("agentProviderTitle")->mapTo(&window, QPoint{}), QPoint(574, 100));
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		QByteArray received;
		connect(&server, &QTcpServer::newConnection, &window, [&] {
			auto* socket = server.nextPendingConnection();
			connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
			connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
				received.append(socket->readAll());
				if (!received.endsWith("\r\n\r\n"))
				{
					return;
				}
				const QByteArray body = R"({"data":[{"id":"model-a"},{"id":"model-b"}]})";
				socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
							  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
				socket->disconnectFromHost();
			});
		});
		address->setText(QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()));
		key->setFocus();
		QTest::keyClicks(key, "test-only-api-page-key");
		model->setEditText("manual-choice");
		auto* fetch = page->findChild<QToolButton*>("agentGetModels");
		QVERIFY(fetch);
		QTest::mouseClick(fetch, Qt::LeftButton);
		QTRY_COMPARE(model->count(), 2);
		QCOMPARE(model->currentText(), QStringLiteral("manual-choice"));
		QVERIFY(received.startsWith("GET /v1/models HTTP/1.1\r\n"));
		QVERIFY(received.contains("Bearer test-only-api-page-key"));
		QVERIFY(page->findChild<QLabel*>("agentApiRequestPreview")->text().contains(address->text()));
		const auto id = page->selectedConnectionId();
		const auto removeKey = qScopeGuard([&] {
			QString error;
			store.removeKey(id, error);
		});
		QString error;
		QVERIFY2(page->save(error), qPrintable(error));
		QCOMPARE(store.connections(error).size(), 1);
		QCOMPARE(store.connections(error).first().model, QStringLiteral("manual-choice"));
		QCOMPARE(store.readKey(id, error), QStringLiteral("test-only-api-page-key"));
		QTest::qWait(300);
		if (!evidence.isEmpty())
		{
			QVERIFY(window.screen()
						->grabWindow(window.winId(), 232, 66, 1368, 874)
						.save(QFileInfo(evidence).absolutePath() + "/agent-chat-api-native.png"));
		}
		{
			QDialog canceled;
			canceled.resize(1600, 1000);
			auto* draft = new lmms::agent::chat::ChatApiSettingsPage(store, &canceled);
			draft->setGeometry(232, 66, 1368, 874);
			canceled.show();
			QVERIFY(QTest::qWaitForWindowExposed(&canceled));
			draft->findChild<QLineEdit*>("agentConnectionName")->setText("must-not-be-saved");
			canceled.reject();
		}
		QCOMPARE(store.connections(error).first().name, QStringLiteral("DeepSeek"));
		window.close();
	}

	void nativeAdvancedSettings()
	{
		QTranslator translator;
		QVERIFY(translator.load(qEnvironmentVariable("LMMS_DATA_DIR") + "/locale/zh_CN.qm"));
		QVERIFY(QCoreApplication::installTranslator(&translator));
		const auto removeTranslator = qScopeGuard([&] { QCoreApplication::removeTranslator(&translator); });
		auto connection = lmms::agent::chat::ChatConnection::create();
		connection.baseUrl = "https://example.com/v1";
		lmms::agent::chat::ChatAdvancedDialog dialog(connection);
		dialog.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dialog));
		auto* header = dialog.findChild<QLineEdit*>("agentKeyHeader");
		auto* path = dialog.findChild<QLineEdit*>("agentRequestPath");
		auto* options = dialog.findChild<QPlainTextEdit*>("agentRequestMapping");
		auto* auth = dialog.findChild<QComboBox*>("agentKeyLocation");
		QVERIFY(header && path && options && auth);
		QCOMPARE(header->mapTo(&dialog, QPoint{}), QPoint(660, 378));
		QCOMPARE(path->mapTo(&dialog, QPoint{}), QPoint(660, 477));
		QCOMPARE(auth->size(), QSize(878, 43));
		QCOMPARE(dialog.windowTitle(), QStringLiteral("请求格式"));
		auto* gemini = dialog.findChild<QToolButton*>("agentAdvancedProtocol_gemini");
		QVERIFY(gemini);
		QTest::mouseClick(gemini, Qt::LeftButton);
		QCOMPARE(header->text(), QStringLiteral("x-goog-api-key"));
		path->setText("custom/messages");
		options->setPlainText(R"({"apiVersion":"v1","capabilities":{"temperature":true},"mapping":{}})");
		auth->setCurrentIndex(auth->findData("query"));
		QTest::qWait(300);
		const auto evidence = qEnvironmentVariable("LMMS_CHAT_SCREENSHOT");
		if (!evidence.isEmpty())
		{
			QVERIFY(dialog.screen()
						->grabWindow(dialog.winId())
						.save(QFileInfo(evidence).absolutePath() + "/agent-chat-advanced-native.png"));
		}
		QVERIFY(QMetaObject::invokeMethod(&dialog, "accept", Qt::DirectConnection));
		QCOMPARE(dialog.result(), int(QDialog::Accepted));
		const auto changed = dialog.connection();
		QCOMPARE(changed.protocol, QStringLiteral("gemini"));
		QCOMPARE(changed.mapping.value("path").toString(), QStringLiteral("custom/messages"));
		QCOMPARE(changed.mapping.value("keyLocation").toString(), QStringLiteral("query"));
		QVERIFY(changed.capabilities.value("temperature").toBool());
		lmms::agent::chat::ChatAdvancedDialog canceled(changed);
		canceled.findChild<QLineEdit*>("agentRequestPath")->setText("discarded");
		canceled.reject();
		QCOMPARE(canceled.connection().mapping, changed.mapping);
	}

	void nativeGenerationSettings()
	{
		QTranslator translator;
		QVERIFY(translator.load(qEnvironmentVariable("LMMS_DATA_DIR") + "/locale/zh_CN.qm"));
		QVERIFY(QCoreApplication::installTranslator(&translator));
		const auto removeTranslator = qScopeGuard([&] { QCoreApplication::removeTranslator(&translator); });
		QDialog window;
		window.resize(1600, 1000);
		lmms::agent::chat::ChatGenerationPage page(&window);
		page.setGeometry(232, 66, 1368, 874);
		auto connection = lmms::agent::chat::ChatConnection::create();
		connection.name = "Test provider";
		connection.model = "fixture-model";
		page.setConnection(connection, true);
		window.show();
		QVERIFY(QTest::qWaitForWindowExposed(&window));
		auto* high = page.findChild<QToolButton*>("agentReasoning_high");
		auto* automatic = page.findChild<QToolButton*>("agentReasoning_auto");
		auto* temperature = page.findChild<QDoubleSpinBox*>("agentTemperature");
		auto* slider = page.findChild<QSlider*>("agentTemperatureSlider");
		QVERIFY(high && automatic && temperature && slider);
		QVERIFY(!high->isEnabled());
		QVERIFY(automatic->isEnabled());
		QVERIFY(!temperature->isEnabled());
		QCOMPARE(automatic->text(), QStringLiteral("自动"));
		QCOMPARE(automatic->mapTo(&window, QPoint{}), QPoint(306, 324));
		QCOMPARE(temperature->mapTo(&window, QPoint{}), QPoint(1007, 474));
		connection.capabilities = QJsonObject{{"temperature", true}, {"reasoningLevels", QJsonArray{"low", "high"}}};
		page.setConnection(connection, false);
		QVERIFY(high->isEnabled());
		QVERIFY(temperature->isEnabled());
		QTest::mouseClick(high, Qt::LeftButton);
		slider->setValue(125);
		QCOMPARE(page.connection().reasoning, QStringLiteral("high"));
		QCOMPARE(page.connection().temperature, 1.25);
		QCOMPARE(temperature->value(), 1.25);
		QTest::qWait(300);
		const auto evidence = qEnvironmentVariable("LMMS_CHAT_SCREENSHOT");
		if (!evidence.isEmpty())
		{
			QVERIFY(window.screen()
						->grabWindow(window.winId(), 232, 66, 1368, 874)
						.save(QFileInfo(evidence).absolutePath() + "/agent-chat-generation-native.png"));
		}
		page.findChild<QToolButton*>("agentResetGeneration")->click();
		QCOMPARE(page.connection().reasoning, QStringLiteral("auto"));
		QCOMPARE(page.connection().temperature, 0.7);
		connection.capabilities = {};
		connection.reasoning = "high";
		page.setConnection(connection, true);
		QCOMPARE(page.connection().reasoning, QStringLiteral("auto"));
		QVERIFY(!slider->isEnabled());
		window.close();
	}

	void nativeGenerationDraftPersistence()
	{
		QStandardPaths::setTestModeEnabled(true);
		const auto previousName = QCoreApplication::applicationName();
		const auto unique = lmms::agent::chat::ChatConnection::create().id;
		QCoreApplication::setApplicationName("LMMS-AgentChatSettings-" + unique);
		const auto restoreName = qScopeGuard([&] { QCoreApplication::setApplicationName(previousName); });
		const auto profile = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
		const auto removeProfile = qScopeGuard([&] { QDir(profile).removeRecursively(); });
		QTemporaryDir config;
		lmms::ConfigManager::inst()->loadConfigFile(config.filePath("config.xml"));
		QFile prompt(config.filePath("prompt.txt"));
		QVERIFY(prompt.open(QIODevice::WriteOnly));
		prompt.write("Test system prompt");
		prompt.close();
		lmms::ConfigManager::inst()->setValue("agentchat", "promptPath", prompt.fileName());
		lmms::agent::chat::ChatConnectionStore store(QDir(profile).filePath("agent"));
		auto first = lmms::agent::chat::ChatConnection::create();
		first.name = "First";
		first.baseUrl = "https://example.com/v1";
		first.capabilities = {{"temperature", true}, {"reasoningLevels", QJsonArray{"high"}}};
		auto second = first;
		second.id = lmms::agent::chat::ChatConnection::create().id;
		second.name = "Second";
		second.capabilities.insert("reasoningLevels", QJsonArray{"low"});
		QString error;
		QVERIFY2(store.saveConnection(first, error), qPrintable(error));
		QVERIFY2(store.saveConnection(second, error), qPrintable(error));
		lmms::ConfigManager::inst()->setValue("agentchat", "connection", second.id);
		lmms::agent::chat::ChatSettingsDialog dialog;
		dialog.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dialog));
		auto* api = dialog.findChild<lmms::agent::chat::ChatApiSettingsPage*>();
		auto* list = dialog.findChild<QListWidget*>("agentConnectionList");
		auto* generationTab = dialog.findChild<QToolButton*>("agentSettingsTab1");
		auto* apiTab = dialog.findChild<QToolButton*>("agentSettingsTab0");
		QVERIFY(api && list && generationTab && apiTab);
		QCOMPARE(api->selectedConnectionId(), second.id);
		apiTab->click();
		for (int row = 0; row < list->count(); ++row)
		{
			if (list->item(row)->data(Qt::UserRole).toString() == second.id)
			{
				list->setCurrentRow(row);
			}
		}
		QCOMPARE(api->selectedConnectionId(), second.id);
		generationTab->click();
		dialog.findChild<QToolButton*>("agentReasoning_low")->click();
		dialog.findChild<QDoubleSpinBox*>("agentTemperature")->setValue(1.2);
		apiTab->click();
		QCOMPARE(api->connectionDraft().reasoning, QStringLiteral("low"));
		for (int row = 0; row < list->count(); ++row)
		{
			if (list->item(row)->data(Qt::UserRole).toString() == first.id)
			{
				list->setCurrentRow(row);
			}
		}
		generationTab->click();
		dialog.findChild<QToolButton*>("agentReasoning_high")->click();
		dialog.findChild<QDoubleSpinBox*>("agentTemperature")->setValue(0.4);
		dialog.findChild<QDialogButtonBox*>("agentSettingsButtons")->button(QDialogButtonBox::Save)->click();
		QCOMPARE(dialog.result(), int(QDialog::Accepted));
		const auto saved = store.connections(error);
		QCOMPARE(saved.size(), 2);
		for (const auto& connection : saved)
		{
			QCOMPARE(connection.reasoning, connection.id == first.id ? QStringLiteral("high") : QStringLiteral("low"));
			QCOMPARE(connection.temperature, connection.id == first.id ? 0.4 : 1.2);
		}
		lmms::agent::chat::ChatSettingsDialog canceled;
		canceled.show();
		QVERIFY(QTest::qWaitForWindowExposed(&canceled));
		canceled.findChild<QToolButton*>("agentSettingsTab1")->click();
		canceled.findChild<QDoubleSpinBox*>("agentTemperature")->setValue(1.9);
		canceled.findChild<QDialogButtonBox*>("agentSettingsButtons")->button(QDialogButtonBox::Cancel)->click();
		const auto afterCancel = store.connections(error);
		for (int index = 0; index < saved.size(); ++index)
		{
			QCOMPARE(afterCancel[index].toJson(), saved[index].toJson());
		}
	}

	void nativeAttachmentSelection()
	{
		QTranslator translator;
		QVERIFY(translator.load(qEnvironmentVariable("LMMS_DATA_DIR") + "/locale/zh_CN.qm"));
		QVERIFY(QCoreApplication::installTranslator(&translator));
		const auto removeTranslator = qScopeGuard([&] { QCoreApplication::removeTranslator(&translator); });
		QTemporaryDir directory;
		const auto midi = directory.filePath("chords.mid");
		const auto image = directory.filePath(QStringLiteral("参考和弦.png"));
		const auto audio = directory.filePath("reference.wav");
		for (const auto& path : {midi, image, audio})
		{
			QFile file(path);
			QVERIFY(file.open(QIODevice::WriteOnly));
			file.write("fixture");
		}
		auto connection = lmms::agent::chat::ChatConnection::create();
		connection.capabilities.insert("inputTypes", QJsonArray{"image"});
		lmms::agent::chat::ChatAttachmentsDialog dialog(connection, {});
		dialog.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dialog));
		dialog.addFiles({midi, image, audio});
		QCOMPARE(dialog.size(), QSize(848, 471));
		QCOMPARE(dialog.findChild<QWidget*>("agentAttachmentDropArea")->geometry(), QRect(24, 73, 800, 121));
		QCOMPARE(dialog.findChild<QToolButton*>("agentAttachmentBrowse")->geometry(), QRect(618, 118, 172, 36));
		auto* rows = dialog.findChild<QScrollArea*>("agentAttachmentRows")->widget();
		QCOMPARE(rows->findChild<QLabel*>("agentAttachmentName_0")->mapTo(&dialog, QPoint{}), QPoint(76, 244));
		QCOMPARE(rows->findChild<QLabel*>("agentAttachmentStatus_0")->text(), QStringLiteral("本地引用"));
		QCOMPARE(rows->findChild<QLabel*>("agentAttachmentStatus_2")->text(), QStringLiteral("当前模型不支持"));
		QTest::qWait(250);
		const auto evidence = qEnvironmentVariable("LMMS_CHAT_SCREENSHOT");
		if (!evidence.isEmpty())
		{
			QVERIFY(dialog.screen()
						->grabWindow(dialog.winId())
						.save(QFileInfo(evidence).absolutePath() + "/agent-chat-attachments-native.png"));
		}
		rows->findChild<QToolButton*>("agentAttachmentRemove_1")->click();
		QCOMPARE(dialog.attachments().size(), 2);
		QMimeData mime;
		mime.setUrls({QUrl::fromLocalFile(image)});
		QDragEnterEvent enter({100, 100}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(&dialog, &enter);
		QVERIFY(enter.isAccepted());
		QDropEvent drop(QPointF(100, 100), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(&dialog, &drop);
		QCOMPARE(dialog.attachments().size(), 3);
		dialog.close();
	}

	void nativeApprovalCardLayout()
	{
		QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
		QTranslator translator;
		QVERIFY(translator.load(qEnvironmentVariable("LMMS_DATA_DIR") + "/locale/zh_CN.qm"));
		QVERIFY(QCoreApplication::installTranslator(&translator));
		const auto removeTranslator = qScopeGuard([&] { QCoreApplication::removeTranslator(&translator); });
		QWidget window;
		window.setWindowTitle(QStringLiteral("LMMS 批准卡原生窗口验收"));
		window.resize(420, 200);
		lmms::agent::chat::ChatTranscript transcript(&window);
		transcript.setObjectName("agentMessages");
		transcript.resize(420, 200);
		QJsonObject approval{
			{"token", "token-one"},
			{"sessionId", "session"},
			{"turnId", "turn"},
			{"id", "call-one"},
			{"command", "agent.runScript"},
			{"purpose", "Approved script"},
			{"arguments",
			 QJsonObject{{"script", QJsonObject{{"name", "Full script"}, {"steps", QJsonArray{}}}},
						 {"vars", QJsonObject{{"bars", 8}}},
						 {"seed", 42}}},
			{"targets", QJsonArray{QJsonObject{{"path", "song/track:0"}, {"name", QStringLiteral("柔和 Pad")}}}}};
		transcript.setApproval(approval);
		QSignalSpy decisions(&transcript, &lmms::agent::chat::ChatTranscript::approvalDecided);
		QSignalSpy details(&transcript, &lmms::agent::chat::ChatTranscript::approvalDetailsRequested);
		window.show();
		QVERIFY(QTest::qWaitForWindowExposed(&window));
		QTest::qWait(200);
		auto* card = transcript.findChild<QFrame*>("agentApprovalCard");
		QVERIFY(card && card->isVisible());
		QCOMPARE(card->size(), QSize(420, 157));
		auto* approve = card->findChild<QToolButton*>("agentApprovalApprove");
		auto* view = card->findChild<QToolButton*>("agentApprovalDetails");
		auto* deny = card->findChild<QToolButton*>("agentApprovalDeny");
		QCOMPARE(approve->geometry(), QRect(12, 109, 98, 34));
		QCOMPARE(view->geometry(), QRect(120, 109, 100, 34));
		QCOMPARE(deny->geometry(), QRect(230, 109, 65, 34));
		QCOMPARE(approve->text(), QStringLiteral("批准本次"));
		QCOMPARE(view->text(), QStringLiteral("查看变更"));
		QCOMPARE(deny->text(), QStringLiteral("拒绝"));
		QCOMPARE(card->findChild<QLabel*>("agentApprovalTitle")->pos(), QPoint(39, 13));
		QCOMPARE(card->findChild<QLabel*>("agentApprovalScope")->pos(), QPoint(14, 48));
		const auto evidence = qEnvironmentVariable("LMMS_CHAT_SCREENSHOT");
		if (!evidence.isEmpty())
		{
			QVERIFY(window.screen()
						->grabWindow(window.winId())
						.save(QFileInfo(evidence).absolutePath() + "/agent-chat-approval-native.png"));
		}
		QTest::mouseClick(view, Qt::LeftButton);
		QCOMPARE(details.size(), 1);
		QCOMPARE(details[0][0].toJsonObject(), approval);
		QTest::mouseClick(approve, Qt::LeftButton);
		QCOMPARE(decisions.size(), 1);
		QCOMPARE(decisions[0][0].toJsonObject(), approval);
		QVERIFY(decisions[0][1].toBool());
		transcript.setApproval(approval);
		QVERIFY(!approve->isEnabled());
		QVERIFY(!deny->isEnabled());
		approval.insert("token", "token-two");
		transcript.setApproval(approval);
		QTest::mouseClick(deny, Qt::LeftButton);
		QCOMPARE(decisions.size(), 2);
		QVERIFY(!decisions[1][1].toBool());
		transcript.setApproval({});
		QVERIFY(!card->isVisible());
		window.close();
	}

	void nativeTranscriptLayout()
	{
		QTranslator translator;
		QVERIFY(translator.load(qEnvironmentVariable("LMMS_DATA_DIR") + "/locale/zh_CN.qm"));
		QVERIFY(QCoreApplication::installTranslator(&translator));
		const auto removeTranslator = qScopeGuard([&] { QCoreApplication::removeTranslator(&translator); });
		QWidget window;
		window.setAutoFillBackground(true);
		window.resize(420, 400);
		lmms::agent::chat::ChatTranscript transcript(&window);
		transcript.setObjectName("agentMessages");
		transcript.resize(420, 400);
		QJsonObject assistant{{"id", "assistant-layout"},
							  {"role", "assistant"},
							  {"text", QStringLiteral("我会沿用参考和弦。")},
							  {"summary", QStringLiteral("只处理选中的八小节。")}};
		transcript.setMessages(QJsonArray{
			QJsonObject{{"id", "user-layout"},
						{"role", "user"},
						{"text", QStringLiteral("参考这个 MIDI，添加柔和的 Pad。")},
						{"attachments",
						 QJsonArray{QJsonObject{
							 {"name", "chords.mid"}, {"type", "midi"}, {"mimeType", "audio/midi"}, {"size", 4300}}}}},
			assistant});
		window.show();
		QVERIFY(QTest::qWaitForWindowExposed(&window));
		QTest::qWait(250);
		auto* user = transcript.findChild<QWidget*>("agentMessage_user-layout");
		auto* agent = transcript.findChild<QWidget*>("agentMessage_assistant-layout");
		QVERIFY(user && agent);
		QCOMPARE(user->geometry(), QRect(0, 0, 420, 113));
		QCOMPARE(agent->pos(), QPoint(0, 140));
		auto* userText = transcript.findChild<QLabel*>("agentMessageText_user-layout");
		auto* agentText = transcript.findChild<QLabel*>("agentMessageText_assistant-layout");
		QCOMPARE(userText->pos(), QPoint(14, 13));
		QCOMPARE(userText->width(), 392);
		QCOMPARE(userText->font().pixelSize(), 13);
		QCOMPARE(agentText->pos(), QPoint(0, 37));
		QCOMPARE(transcript.findChild<QWidget*>("agentMessageAttachment_user-layout_0")->pos(), QPoint(14, 74));
		QCOMPARE(transcript.findChild<QLabel*>("agentMessageSummary_assistant-layout")->pos(), QPoint(100, 1));
		const auto evidence = qEnvironmentVariable("LMMS_CHAT_SCREENSHOT");
		if (!evidence.isEmpty())
		{
			QVERIFY(window.screen()
						->grabWindow(window.winId())
						.save(QFileInfo(evidence).absolutePath() + "/agent-chat-transcript-native.png"));
		}
		const QPointer<QWidget> originalUser(user);
		const QPointer<QWidget> originalAgent(agent);
		assistant.insert("text", QStringLiteral("流式更新\n").repeated(60));
		transcript.updateMessage(assistant);
		QVERIFY(originalUser && originalAgent);
		QCOMPARE(transcript.findChild<QWidget*>("agentMessage_assistant-layout"), originalAgent.data());
		QTRY_VERIFY(transcript.verticalScrollBar()->maximum() > 0);
		transcript.verticalScrollBar()->setValue(0);
		assistant.insert("text", assistant.value("text").toString() + QStringLiteral("再补一段。"));
		transcript.updateMessage(assistant);
		QCOMPARE(transcript.verticalScrollBar()->value(), 0);
		QVERIFY(transcript.toPlainText().contains(QStringLiteral("再补一段。")));
		transcript.setMessages(QJsonArray{
			QJsonObject{{"id", "array-result"}, {"role", "tool"}, {"content", QJsonArray{"result-array", 3}}},
			QJsonObject{{"id", "error-message"},
						{"role", "assistant"},
						{"text", "<b>Literal</b>"},
						{"error", QStringLiteral("测试错误")},
						{"status", "canceled"}}});
		QVERIFY(transcript.toPlainText().contains("result-array"));
		QVERIFY(transcript.toPlainText().contains(QStringLiteral("请求失败：测试错误")));
		QVERIFY(transcript.toPlainText().contains(QStringLiteral("已停止")));
		QCOMPARE(transcript.findChild<QLabel*>("agentMessageText_error-message")->textFormat(), Qt::PlainText);
		window.close();
	}

	void nativeTextConversation()
	{
		lmms::Engine::init(true);
		const auto destroyEngine = qScopeGuard([] { lmms::Engine::destroy(); });
		QTranslator translator;
		QVERIFY(translator.load(qEnvironmentVariable("LMMS_DATA_DIR") + "/locale/zh_CN.qm"));
		QVERIFY(QCoreApplication::installTranslator(&translator));
		const auto removeTranslator = qScopeGuard([&] { QCoreApplication::removeTranslator(&translator); });
		QStandardPaths::setTestModeEnabled(true);
		const auto previousName = QCoreApplication::applicationName();
		QCoreApplication::setApplicationName("LMMS-AgentChatSend-" + lmms::agent::chat::ChatConnection::create().id);
		const auto restoreName = qScopeGuard([&] { QCoreApplication::setApplicationName(previousName); });
		const auto profile = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
		const auto removeProfile = qScopeGuard([&] { QDir(profile).removeRecursively(); });
		QTemporaryDir config;
		lmms::ConfigManager::inst()->loadConfigFile(config.filePath("config.xml"));
		QFile prompt(config.filePath("prompt.txt"));
		QVERIFY(prompt.open(QIODevice::WriteOnly));
		prompt.write("Fixture system prompt");
		prompt.close();
		lmms::ConfigManager::inst()->setValue("agentchat", "promptPath", prompt.fileName());
		QTcpServer server;
		lmms::ConfigManager::inst()->setValue("agentchat", "approval", "request");
		QVERIFY(server.listen(QHostAddress::LocalHost));
		QPointer<QTcpSocket> peer;
		int requests = 0;
		const auto textEvent = [](const QString& text) {
			const QJsonObject choice{{"delta", QJsonObject{{"content", text}}}};
			return "data: " + QJsonDocument(QJsonObject{{"choices", QJsonArray{choice}}}).toJson(QJsonDocument::Compact)
				+ "\n\n";
		};
		connect(&server, &QTcpServer::newConnection, &server, [&] {
			auto* socket = server.nextPendingConnection();
			connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
			connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
				auto received = socket->property("received").toByteArray() + socket->readAll();
				socket->setProperty("received", received);
				const auto separator = received.indexOf("\r\n\r\n");
				if (separator < 0 || socket->property("responded").toBool())
				{
					return;
				}
				int length = 0;
				for (const auto& line : received.left(separator).split('\n'))
				{
					if (line.toLower().startsWith("content-length:"))
					{
						length = line.mid(line.indexOf(':') + 1).trimmed().toInt();
					}
				}
				if (received.size() < separator + 4 + length)
				{
					return;
				}
				socket->setProperty("responded", true);
				peer = socket;
				++requests;
				socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n");
				socket->write(textEvent(QStringLiteral("旋律建议：使用五声音阶。\n").repeated(45)));
			});
		});
		auto connection = lmms::agent::chat::ChatConnection::create();
		connection.name = "Local fixture";
		connection.model = "fixture-model";
		connection.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());
		lmms::agent::chat::ChatConnectionStore connections(QDir(profile).filePath("agent"));
		QString error;
		QVERIFY(connections.saveConnection(connection, error));
		QVERIFY(connections.writeKey(connection.id, "test-only-dock-key", error));
		auto alternate = connection;
		alternate.id = lmms::agent::chat::ChatConnection::create().id;
		alternate.name = "Alternate fixture";
		alternate.model = "alternate-model";
		alternate.capabilities
			= {{"reasoningLevels", QJsonArray{"high"}},
			   {"reasoningParameters", QJsonObject{{"high", QJsonObject{{"reasoning_effort", "high"}}}}}};
		alternate.capabilities.insert("inputTypes", QJsonArray{"image"});
		QVERIFY(connections.saveConnection(alternate, error));
		QVERIFY(connections.writeKey(alternate.id, "test-only-dock-key", error));
		const auto removeKey = qScopeGuard([&] {
			connections.removeKey(connection.id, error);
			connections.removeKey(alternate.id, error);
		});
		lmms::ConfigManager::inst()->setValue("agentchat", "connection", connection.id);
		QMainWindow window;
		window.resize(1600, 878);
		window.setCentralWidget(new QWidget(&window));
		auto* dock = new lmms::agent::chat::ChatDock(&window);
		window.show();
		QVERIFY(QTest::qWaitForWindowExposed(&window));
		window.resizeDocks({dock}, {640}, Qt::Horizontal);
		auto* input = dock->findChild<QPlainTextEdit*>("agentInput");
		auto* send = dock->findChild<QToolButton*>("agentSend");
		auto* messages = dock->findChild<lmms::agent::chat::ChatTranscript*>("agentMessages");
		auto* controller = dock->findChild<lmms::agent::chat::ChatController*>();
		QVERIFY(input && send && messages && controller);
		QTimer::singleShot(0, dock, [&, dock] {
			auto* menu = dock->findChild<QMenu*>("agentModelMenu");
			QVERIFY(menu);
			for (auto* action : menu->actions())
			{
				if (action->data().toString() == alternate.id)
				{
					QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(action).center());
					break;
				}
			}
		});
		dock->findChild<QToolButton*>("agentModel")->click();
		QVERIFY(dock->findChild<QToolButton*>("agentModel")->text().contains(alternate.model));
		auto* reasoningButton = dock->findChild<QToolButton*>("agentReasoning");
		QVERIFY(reasoningButton);
		reasoningButton->click();
		auto* popup = dock->findChild<QFrame*>("agentReasoningPopup");
		QVERIFY(popup);
		QVERIFY(QTest::qWaitForWindowExposed(popup));
		QCOMPARE(popup->size(), QSize(316, 384));
		auto* high = popup->findChild<QToolButton*>("agentQuickReasoning_high");
		QVERIFY(high && high->isEnabled());
		QCOMPARE(high->geometry(), QRect(11, 246, 294, 38));
		QVERIFY(!popup->findChild<QToolButton*>("agentQuickReasoning_xhigh")->isEnabled());
		QTest::qWait(250);
		const auto popupEvidence = qEnvironmentVariable("LMMS_CHAT_SCREENSHOT");
		if (!popupEvidence.isEmpty())
		{
			QVERIFY(popup->screen()
						->grabWindow(popup->winId())
						.save(QFileInfo(popupEvidence).absolutePath() + "/agent-chat-reasoning-native.png"));
		}
		QTest::mouseClick(high, Qt::LeftButton);
		const auto attachmentPath = config.filePath("reference.png");
		const auto attachmentBytes = QByteArray::fromBase64(
			"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j9s0AAAAASUVORK5CYII=");
		QFile attachmentFile(attachmentPath);
		QVERIFY(attachmentFile.open(QIODevice::WriteOnly));
		attachmentFile.write(attachmentBytes);
		attachmentFile.close();
		const auto discardedPath = config.filePath("discarded.txt");
		QFile discarded(discardedPath);
		QVERIFY(discarded.open(QIODevice::WriteOnly));
		discarded.write("must-not-be-sent");
		discarded.close();
		QTimer::singleShot(0, dock, [dock, attachmentPath, discardedPath] {
			auto* dialog = dock->findChild<lmms::agent::chat::ChatAttachmentsDialog*>();
			QVERIFY(dialog);
			dialog->addFiles({discardedPath, attachmentPath});
			dialog->reject();
		});
		dock->findChild<QToolButton*>("agentAttach")->click();
		auto* chip = dock->findChild<QToolButton*>("agentAttachmentChip");
		QVERIFY(chip->isVisible());
		QCOMPARE(chip->geometry(), QRect(12, 57, 174, 28));
		QCOMPARE(chip->findChild<QLabel*>("agentAttachmentChipIcon")->geometry(), QRect(9, 6, 15, 15));
		auto* caption = chip->findChild<QLabel*>("agentAttachmentChipCaption");
		QCOMPARE(caption->geometry(), QRect(32, 5, 105, 16));
		auto* remove = chip->findChild<QToolButton*>("agentAttachmentRemove");
		QCOMPARE(remove->geometry(), QRect(141, 8, 12, 12));
		lmms::agent::chat::ChatStore draftStore(QDir(profile).filePath("agent/sessions"));
		const auto draftId
			= dock->findChild<QListWidget*>("agentSessionList")->currentItem()->data(Qt::UserRole).toString();
		QTest::mouseClick(remove, Qt::LeftButton);
		const auto remaining = draftStore.session(draftId).value("draftAttachments").toArray();
		QCOMPARE(remaining.size(), 1);
		QCOMPARE(remaining.first().toObject().value("path").toString(), attachmentPath);
		QCOMPARE(caption->text(), QStringLiteral("reference.png"));
		QVERIFY(QFile::exists(discardedPath));
		QTest::mouseClick(remove, Qt::LeftButton);
		QVERIFY(!chip->isVisible());
		QVERIFY(!send->isEnabled());
		QVERIFY(draftStore.session(draftId).value("draftAttachments").toArray().isEmpty());
		QTimer::singleShot(0, dock, [dock, attachmentPath] {
			auto* dialog = dock->findChild<lmms::agent::chat::ChatAttachmentsDialog*>();
			QVERIFY(dialog);
			dialog->addFiles({attachmentPath});
			dialog->reject();
		});
		dock->findChild<QToolButton*>("agentAttach")->click();
		QVERIFY(chip->isVisible());
		input->setPlainText(QStringLiteral("帮我编写旋律"));
		QVERIFY(send->isEnabled());
		QTest::qWait(250);
		const auto chipEvidence = qEnvironmentVariable("LMMS_CHAT_SCREENSHOT");
		if (!chipEvidence.isEmpty())
		{
			QVERIFY(dock->screen()
						->grabWindow(window.winId(), dock->x(), dock->y(), dock->width(), dock->height())
						.save(QFileInfo(chipEvidence).absolutePath() + "/agent-chat-attachment-chip-native.png"));
		}
		input->setFocus();
		input->moveCursor(QTextCursor::End);
		QTest::keyClick(input, Qt::Key_Return, Qt::ShiftModifier);
		QVERIFY(input->toPlainText().endsWith('\n'));
		QCOMPARE(requests, 0);
		QTest::keyClick(input, Qt::Key_Return);
		QTRY_VERIFY(controller->active());
		const auto original = controller->activeSessionId();
		QTRY_VERIFY(messages->toPlainText().contains(QStringLiteral("五声音阶")));
		const auto received = peer->property("received").toByteArray();
		QVERIFY(!received.contains("must-not-be-sent"));
		const auto sent = QJsonDocument::fromJson(received.mid(received.indexOf("\r\n\r\n") + 4)).object();
		QCOMPARE(sent.value("model").toString(), alternate.model);
		QCOMPARE(sent.value("reasoning_effort").toString(), QStringLiteral("high"));
		const auto parts = sent.value("messages").toArray().last().toObject().value("content").toArray();
		QCOMPARE(parts.size(), 2);
		const auto uri = parts[1].toObject().value("image_url").toObject().value("url").toString();
		QVERIFY(uri.startsWith("data:image/png;base64,"));
		QCOMPARE(QByteArray::fromBase64(uri.mid(uri.indexOf(',') + 1).toLatin1()), attachmentBytes);
		QVERIFY(!chip->isVisible());
		QVERIFY(input->toPlainText().isEmpty());
		QVERIFY(messages->verticalScrollBar()->maximum() > 0);
		messages->verticalScrollBar()->setValue(0);
		peer->write(textEvent(QStringLiteral("补充内容\n").repeated(15)));
		QTRY_VERIFY(messages->toPlainText().contains(QStringLiteral("补充内容")));
		QCOMPARE(messages->verticalScrollBar()->value(), 0);
		dock->findChild<QToolButton*>("agentNewSession")->click();
		input->setPlainText("Other session draft");
		peer->write("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n");
		peer->disconnectFromHost();
		QTRY_VERIFY(!controller->active());
		QVERIFY(!messages->toPlainText().contains(QStringLiteral("五声音阶")));
		QCOMPARE(input->toPlainText(), QStringLiteral("Other session draft"));
		lmms::agent::chat::ChatStore store(QDir(profile).filePath("agent/sessions"));
		QCOMPARE(store.session(original).value("messages").toArray().last().toObject().value("status").toString(),
				 QStringLiteral("complete"));
		auto* list = dock->findChild<QListWidget*>("agentSessionList");
		for (int row = 0; row < list->count(); ++row)
		{
			if (list->item(row)->data(Qt::UserRole).toString() == original)
			{
				list->setCurrentRow(row);
			}
		}
		QVERIFY(messages->toPlainText().contains(QStringLiteral("五声音阶")));
		QVERIFY(dock->findChild<QToolButton*>("agentModel")->text().contains(alternate.model));
		QVERIFY(reasoningButton->text().contains(QStringLiteral("高")));
		QTest::qWait(300);
		const auto evidence = qEnvironmentVariable("LMMS_CHAT_SCREENSHOT");
		if (!evidence.isEmpty())
		{
			QVERIFY(dock->screen()
						->grabWindow(window.winId(), dock->x(), dock->y(), dock->width(), dock->height())
						.save(QFileInfo(evidence).absolutePath() + "/agent-chat-send-native.png"));
		}
		input->setPlainText("Retry this request");
		send->click();
		QTRY_COMPARE(requests, 2);
		peer->write("data: {\"error\":{\"message\":\"Fixture rejection\"}}\n\n");
		peer->disconnectFromHost();
		QTRY_VERIFY(!controller->active());
		QVERIFY(messages->toPlainText().contains("Fixture rejection"));
		QVERIFY(input->toPlainText().isEmpty());
		QVERIFY(send->isEnabled());
		QCOMPARE(send->toolTip(), QStringLiteral("重试上次失败的请求"));
		const auto beforeRetry = store.session(original).value("messages").toArray();
		const auto sendGeometry = send->geometry();
		QTest::mouseClick(send, Qt::LeftButton);
		QTRY_COMPARE(requests, 3);
		QCOMPARE(send->geometry(), sendGeometry);
		const auto retryBytes = peer->property("received").toByteArray();
		const auto retryBody = QJsonDocument::fromJson(retryBytes.mid(retryBytes.indexOf("\r\n\r\n") + 4)).object();
		const auto retryHistory = retryBody.value("messages").toArray();
		QCOMPARE(retryHistory.last().toObject().value("content").toString(), QStringLiteral("Retry this request"));
		QVERIFY(!retryBytes.contains("Fixture rejection"));
		peer->write("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n");
		peer->disconnectFromHost();
		QTRY_VERIFY(!controller->active());
		QCOMPARE(store.session(original).value("messages").toArray().size(), beforeRetry.size() + 1);
		QVERIFY(!send->isEnabled());
		input->setPlainText("Stop this request");
		send->click();
		QTRY_COMPARE(requests, 4);
		QTRY_VERIFY(controller->currentMessage().value("text").toString().contains(QStringLiteral("五声音阶")));
		send->click();
		QVERIFY(!controller->active());
		QCOMPARE(store.session(original).value("messages").toArray().last().toObject().value("status").toString(),
				 QStringLiteral("canceled"));
		input->setPlainText("Close while streaming");
		send->click();
		QTRY_COMPARE(requests, 5);
		QTRY_VERIFY(controller->currentMessage().value("text").toString().contains(QStringLiteral("五声音阶")));
		delete dock;
		QCOMPARE(store.session(original).value("messages").toArray().last().toObject().value("status").toString(),
				 QStringLiteral("canceled"));
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
