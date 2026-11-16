#include "agent/chat/ChatConnection.h"

#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTranslator>
#include <QtTest>

using lmms::agent::chat::ChatConnection;
using lmms::agent::chat::ChatConnectionStore;

class ChatConnectionTest : public QObject
{
	Q_OBJECT

private slots:
	void chineseTranslations()
	{
		QTranslator translator;
		const auto catalog = qEnvironmentVariable("LMMS_DATA_DIR") + "/locale/zh_CN.qm";
		QVERIFY2(translator.load(catalog), qPrintable(catalog));
		QVERIFY(QCoreApplication::installTranslator(&translator));
		QCOMPARE(ChatConnection::create().name, QStringLiteral("新连接"));
		auto connection = ChatConnection::create();
		connection.baseUrl = "https://user:secret@example.com";
		QString error;
		QVERIFY(!connection.validate(error));
		QCOMPARE(error, QStringLiteral("API 基础地址须使用 HTTP(S)，且不能包含凭据、查询参数或片段。"));
		QCOMPARE(QCoreApplication::translate("AgentChat", "Windows Credential Manager error %1.").arg(5),
				 QStringLiteral("Windows 凭据管理器错误 5。"));
		QCoreApplication::removeTranslator(&translator);
	}

	void publicSettingsAndNativeCredentials()
	{
		QTemporaryDir profile;
		QTemporaryDir otherProfile;
		ChatConnectionStore store(profile.path());
		ChatConnectionStore other(otherProfile.path());
		auto connection = ChatConnection::create();
		connection.name = QStringLiteral("深度求索 · 创作");
		connection.provider = "deepseek";
		connection.baseUrl = "https://api.deepseek.com";
		connection.model = "deepseek-flash";
		QString error;
		const auto cleanup = qScopeGuard([&] {
			QString ignored;
			store.removeKey(connection.id, ignored);
			other.removeKey(connection.id, ignored);
		});
		QVERIFY2(store.saveConnection(connection, error), qPrintable(error));
		QCOMPARE(store.connections(error).size(), 1);
		QCOMPARE(store.connections(error).first().name, connection.name);
		const QString key = QStringLiteral("test-only-密钥-value");
		QVERIFY2(store.writeKey(connection.id, key, error), qPrintable(error));
		QCOMPARE(store.readKey(connection.id, error), key);
		QVERIFY(error.isEmpty());
		QVERIFY2(other.writeKey(connection.id, "other-profile-key", error), qPrintable(error));
		QCOMPARE(other.readKey(connection.id, error), QStringLiteral("other-profile-key"));
		QCOMPARE(store.readKey(connection.id, error), key);
		QFile file(profile.filePath("connections.json"));
		QVERIFY(file.open(QIODevice::ReadOnly));
		const auto json = file.readAll();
		QVERIFY(!json.contains(key.toUtf8()));
		QVERIFY(!json.contains("apiKey"));
		file.close();
		connection.model = "manually-configured-model";
		QVERIFY(store.saveConnection(connection, error));
		QCOMPARE(store.connections(error).size(), 1);
		QCOMPARE(store.connections(error).first().model, connection.model);
		QCOMPARE(store.readKey(connection.id, error), key);
		QVERIFY(store.removeConnection(connection.id, error));
		QVERIFY(store.connections(error).isEmpty());
		QVERIFY(store.readKey(connection.id, error).isEmpty());
		QVERIFY(error.isEmpty());
		QCOMPARE(other.readKey(connection.id, error), QStringLiteral("other-profile-key"));
	}

	void malformedSettingsAreNotOverwritten()
	{
		QTemporaryDir profile;
		ChatConnectionStore store(profile.path());
		QFile file(profile.filePath("connections.json"));
		QVERIFY(file.open(QIODevice::WriteOnly));
		const QByteArray original = "{broken-json";
		file.write(original);
		file.close();
		auto connection = ChatConnection::create();
		connection.baseUrl = "http://localhost:1234/v1";
		QString error;
		QVERIFY(!store.saveConnection(connection, error));
		QVERIFY(!error.isEmpty());
		QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.readAll(), original);
	}

	void invalidConnection()
	{
		auto connection = ChatConnection::create();
		QString error;
		connection.baseUrl = "https://user:secret@example.com/v1";
		QVERIFY(!connection.validate(error));
		connection.baseUrl = "https://example.com/v1?key=secret";
		QVERIFY(!connection.validate(error));
		connection.baseUrl = "http://localhost:1234/v1";
		QVERIFY(connection.validate(error));
		connection.protocol = "unknown-protocol";
		QVERIFY(!connection.validate(error));
	}
};

QTEST_GUILESS_MAIN(ChatConnectionTest)
#include "ChatConnectionTest.moc"
