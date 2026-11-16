#include "agent/chat/ChatStore.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using lmms::agent::chat::ChatStore;

class ChatStoreTest : public QObject
{
	Q_OBJECT
private slots:
	void roundTripAndExport()
	{
		QTemporaryDir directory;
		ChatStore store(directory.filePath("sessions"));
		auto session = ChatStore::newSession();
		session.insert("title", QStringLiteral("五声音阶"));
		session.insert("apiKey", "must-not-export");
		session.insert("draft", "private draft");
		QString error;
		QVERIFY2(store.saveSession(session, error), qPrintable(error));
		const auto id = session.value("id").toString();
		QCOMPARE(store.session(id).value("title"), session.value("title"));
		QCOMPARE(store.sessions().size(), 1);
		QVERIFY(store.exportSession(id, directory.filePath("export.json"), error));
		QFile file(directory.filePath("export.json"));
		QVERIFY(file.open(QIODevice::ReadOnly));
		const auto bytes = file.readAll();
		QVERIFY(!bytes.contains("must-not-export"));
		QVERIFY(!bytes.contains("private draft"));
		const auto importedId = store.importSession(file.fileName(), error);
		QVERIFY2(!importedId.isEmpty(), qPrintable(error));
		QVERIFY(importedId != id);
		QCOMPARE(store.session(importedId).value("title"), session.value("title"));
		QVERIFY(!store.session(importedId).contains("apiKey"));
		QVERIFY(store.removeSession(id, error));
		QCOMPARE(store.sessions().size(), 1);
		QVERIFY(store.removeSession(importedId, error));
		QVERIFY(store.sessions().isEmpty());
	}

	void invalidIdentifierAndPrompt()
	{
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		session.insert("id", "../outside");
		QString error;
		QVERIFY(!store.saveSession(session, error));
		QVERIFY(!error.isEmpty());
		error.clear();
		QCOMPARE(ChatStore::readPrompt(directory.filePath("missing.txt"), error), QString{});
		QVERIFY(!error.isEmpty());
		QFile file(directory.filePath("prompt.txt"));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(QStringLiteral("你是音乐助手").toUtf8());
		file.close();
		error.clear();
		QCOMPARE(ChatStore::readPrompt(file.fileName(), error), QStringLiteral("你是音乐助手"));
		QVERIFY(error.isEmpty());
	}
};

QTEST_GUILESS_MAIN(ChatStoreTest)
#include "ChatStoreTest.moc"
