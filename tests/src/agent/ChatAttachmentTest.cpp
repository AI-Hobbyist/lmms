#include "agent/chat/ChatAttachment.h"

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

#include "agent/chat/ChatRequest.h"

using namespace lmms::agent::chat;

class ChatAttachmentTest : public QObject
{
	Q_OBJECT
private:
	static bool write(const QString& path, const QByteArray& bytes)
	{
		QFile file(path);
		return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
	}

	static ChatConnection connection(const QString& protocol = "openai-chat")
	{
		auto result = ChatConnection::create();
		result.protocol = protocol;
		result.baseUrl = "https://example.com/v1";
		result.model = "fixture-model";
		result.capabilities.insert("inputTypes", QJsonArray{"image", "audio", "file"});
		if (protocol == "custom-json")
		{
			result.mapping.insert("path", "generate");
		}
		return result;
	}

private slots:
	void actualTextAndMidiReference()
	{
		QTemporaryDir directory;
		const auto path = directory.filePath(QStringLiteral("和弦.txt"));
		QVERIFY(write(path, QStringLiteral("C大调：C E G").toUtf8()));
		ChatAttachment attachment;
		QString error;
		QVERIFY(inspectChatAttachment(path, attachment, error));
		QCOMPARE(attachment.type, QStringLiteral("text"));
		QVERIFY(!attachment.toJson().contains("data"));
		QJsonObject part;
		QVERIFY2(prepareChatAttachment(connection(), attachment, part, error), qPrintable(error));
		QVERIFY(part.value("text").toString().contains(QStringLiteral("C大调：C E G")));
		QVERIFY(write(path, QByteArray::fromHex("fffeff")));
		QVERIFY(inspectChatAttachment(path, attachment, error));
		QVERIFY(!prepareChatAttachment(connection(), attachment, part, error));
		QVERIFY(part.isEmpty());
		const auto midi = directory.filePath("chords.mid");
		QVERIFY(write(midi, QByteArray::fromHex("4d546864000000060000000101e04d54726b0000000400ff2f00")));
		QVERIFY(inspectChatAttachment(midi, attachment, error));
		QCOMPARE(attachment.type, QStringLiteral("midi"));
		QVERIFY(prepareChatAttachment(connection(), attachment, part, error));
		QVERIFY(part.value("text").toString().contains("not uploaded or parsed"));
		QVERIFY(part.value("text").toString().contains(midi));
	}

	void imageRequestBytes()
	{
		QTemporaryDir directory;
		const auto path = directory.filePath("reference.png");
		const auto bytes = QByteArray::fromBase64(
			"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j9s0AAAAASUVORK5CYII=");
		QVERIFY(write(path, bytes));
		ChatAttachment attachment;
		QString error;
		QVERIFY(inspectChatAttachment(path, attachment, error));
		QCOMPARE(attachment.mimeType, QStringLiteral("image/png"));
		for (const auto& protocol : ChatConnection::protocols())
		{
			const auto settings = connection(protocol);
			QJsonObject part;
			if (protocol == "custom-json")
			{
				QVERIFY(!prepareChatAttachment(settings, attachment, part, error));
				continue;
			}
			QVERIFY2(prepareChatAttachment(settings, attachment, part, error), qPrintable(error));
			QCOMPARE(QByteArray::fromBase64(part.value("data").toString().toLatin1()), bytes);
			const QJsonArray content{QJsonObject{{"type", "text"}, {"text", "Analyze"}}, part};
			ChatRequest request;
			QVERIFY2(buildChatRequest(settings, "fake-key", "",
									  QJsonArray{QJsonObject{{"role", "user"}, {"content", content}}}, request, error),
					 qPrintable(error));
			if (protocol == "gemini")
			{
				const auto native = request.body.value("contents")
										.toArray()
										.first()
										.toObject()
										.value("parts")
										.toArray()[1]
										.toObject();
				QCOMPARE(native.value("inlineData").toObject().value("data"), part.value("data"));
			}
			else
			{
				const auto native = request.body.value(protocol.endsWith("responses") ? "input" : "messages")
										.toArray()
										.first()
										.toObject()
										.value("content")
										.toArray()[1]
										.toObject();
				if (protocol == "anthropic-messages")
				{
					QCOMPARE(native.value("source").toObject().value("data"), part.value("data"));
				}
				else
				{
					const auto uri = protocol.endsWith("responses")
						? native.value("image_url").toString()
						: native.value("image_url").toObject().value("url").toString();
					QVERIFY(uri.startsWith("data:image/png;base64,"));
					QCOMPARE(QByteArray::fromBase64(uri.mid(uri.indexOf(',') + 1).toLatin1()), bytes);
				}
			}
		}
	}

	void pdfRequestBytes()
	{
		QTemporaryDir directory;
		const auto path = directory.filePath("reference.pdf");
		const QByteArray bytes = "%PDF-1.4\n1 0 obj\n<< /Type /Catalog >>\nendobj\n%%EOF\n";
		QVERIFY(write(path, bytes));
		ChatAttachment attachment;
		QString error;
		QVERIFY(inspectChatAttachment(path, attachment, error));
		QCOMPARE(attachment.mimeType, QStringLiteral("application/pdf"));
		for (const auto& protocol : ChatConnection::protocols())
		{
			QJsonObject part;
			const auto settings = connection(protocol);
			if (protocol == "custom-json")
			{
				QVERIFY(!prepareChatAttachment(settings, attachment, part, error));
				continue;
			}
			QVERIFY2(prepareChatAttachment(settings, attachment, part, error), qPrintable(error));
			ChatRequest request;
			QVERIFY2(buildChatRequest(settings, "fake-key", "",
									  QJsonArray{QJsonObject{{"role", "user"}, {"content", QJsonArray{part}}}}, request,
									  error),
					 qPrintable(error));
			QJsonObject native;
			if (protocol == "gemini")
			{
				native = request.body.value("contents")
							 .toArray()
							 .first()
							 .toObject()
							 .value("parts")
							 .toArray()
							 .first()
							 .toObject();
				const auto data = native.value("inlineData").toObject();
				QCOMPARE(data.value("mimeType").toString(), attachment.mimeType);
				QCOMPARE(QByteArray::fromBase64(data.value("data").toString().toLatin1()), bytes);
			}
			else
			{
				native = request.body.value(protocol.endsWith("responses") ? "input" : "messages")
							 .toArray()
							 .first()
							 .toObject()
							 .value("content")
							 .toArray()
							 .first()
							 .toObject();
				if (protocol == "anthropic-messages")
				{
					QCOMPARE(native.value("type").toString(), QStringLiteral("document"));
					const auto source = native.value("source").toObject();
					QCOMPARE(source.value("media_type").toString(), attachment.mimeType);
					QCOMPARE(QByteArray::fromBase64(source.value("data").toString().toLatin1()), bytes);
				}
				else
				{
					const auto file = protocol.endsWith("responses") ? native : native.value("file").toObject();
					QCOMPARE(file.value("filename").toString(), attachment.name);
					const auto uri = file.value("file_data").toString();
					QVERIFY(uri.startsWith("data:application/pdf;base64,"));
					QCOMPARE(QByteArray::fromBase64(uri.mid(uri.indexOf(',') + 1).toLatin1()), bytes);
				}
			}
		}
	}

	void geminiAudioRequestBytes()
	{
		QTemporaryDir directory;
		const auto path = directory.filePath("reference.wav");
		const auto bytes = QByteArray::fromHex(
			"524946462600000057415645666d7420100000000100010044ac0000885801000200100064617461020000000000");
		QVERIFY(write(path, bytes));
		ChatAttachment attachment;
		QString error;
		QVERIFY(inspectChatAttachment(path, attachment, error));
		QJsonObject part;
		const auto settings = connection("gemini");
		QVERIFY(prepareChatAttachment(settings, attachment, part, error));
		ChatRequest request;
		QVERIFY(buildChatRequest(settings, "fake-key", "",
								 QJsonArray{QJsonObject{{"role", "user"}, {"content", QJsonArray{part}}}}, request,
								 error));
		const auto data = request.body.value("contents")
							  .toArray()
							  .first()
							  .toObject()
							  .value("parts")
							  .toArray()
							  .first()
							  .toObject()
							  .value("inlineData")
							  .toObject();
		QCOMPARE(data.value("mimeType").toString(), QStringLiteral("audio/wav"));
		QCOMPARE(QByteArray::fromBase64(data.value("data").toString().toLatin1()), bytes);
	}

	void capabilitySizeAndFileErrors()
	{
		QTemporaryDir directory;
		const auto path = directory.filePath("reference.wav");
		const QByteArray bytes = QByteArray::fromHex(
			"524946462600000057415645666d7420100000000100010044ac0000885801000200100064617461020000000000");
		QVERIFY(write(path, bytes));
		ChatAttachment attachment;
		QString error;
		QVERIFY(inspectChatAttachment(path, attachment, error));
		QJsonObject part;
		QVERIFY2(prepareChatAttachment(connection(), attachment, part, error), qPrintable(error));
		QCOMPARE(QByteArray::fromBase64(part.value("data").toString().toLatin1()), bytes);
		QCOMPARE(part.value("mimeType").toString(), QStringLiteral("audio/wav"));
		ChatRequest request;
		const QJsonArray content{part};
		QVERIFY(buildChatRequest(connection(), "fake-key", "",
								 QJsonArray{QJsonObject{{"role", "user"}, {"content", content}}}, request, error));
		const auto audio = request.body.value("messages")
							   .toArray()
							   .first()
							   .toObject()
							   .value("content")
							   .toArray()
							   .first()
							   .toObject()
							   .value("input_audio")
							   .toObject();
		QCOMPARE(audio.value("format").toString(), QStringLiteral("wav"));
		QCOMPARE(QByteArray::fromBase64(audio.value("data").toString().toLatin1()), bytes);
		QVERIFY(!supportsChatAttachment(connection("anthropic-messages"), attachment, error));
		auto settings = connection();
		settings.capabilities = {};
		QVERIFY(!supportsChatAttachment(settings, attachment, error));
		settings = connection();
		settings.capabilities.insert("maxAttachmentBytes", 8);
		QVERIFY(!supportsChatAttachment(settings, attachment, error));
		QVERIFY(write(path, "changed"));
		QVERIFY(!prepareChatAttachment(connection(), attachment, part, error));
		QVERIFY(QFile::remove(path));
		QVERIFY(!prepareChatAttachment(connection(), attachment, part, error));
	}
};

QTEST_GUILESS_MAIN(ChatAttachmentTest)
#include "ChatAttachmentTest.moc"
