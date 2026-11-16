#include "agent/chat/ChatEventStream.h"

#include <QtTest>

using lmms::agent::chat::ChatEventStream;
using lmms::agent::chat::ChatStreamEvent;

class ChatEventStreamTest : public QObject
{
	Q_OBJECT

private slots:
	void arbitraryChunkBoundaries()
	{
		const auto wire = QByteArray::fromHex("efbbbf")
			+ QStringLiteral(": keepalive\r\nevent: delta\r\ndata: 五声音阶\r\ndata: second line\r\n\r\n"
							 "data:\n\ndata: [DONE]\n\n")
				  .toUtf8();
		for (qsizetype chunkSize = 1; chunkSize <= wire.size(); ++chunkSize)
		{
			ChatEventStream stream;
			QVector<ChatStreamEvent> events;
			for (qsizetype offset = 0; offset < wire.size(); offset += chunkSize)
			{
				events.append(stream.feed(wire.mid(offset, chunkSize)));
			}
			events.append(stream.finish());
			QCOMPARE(events.size(), 3);
			QCOMPARE(events[0].type, QByteArray("delta"));
			QCOMPARE(QString::fromUtf8(events[0].data), QStringLiteral("五声音阶\nsecond line"));
			QCOMPARE(events[1].type, QByteArray("message"));
			QVERIFY(events[1].data.isEmpty());
			QCOMPARE(events[2].data, QByteArray("[DONE]"));
		}
	}

	void carriageReturnsAndUnterminatedEvents()
	{
		ChatEventStream stream;
		QVERIFY(stream.feed("event: delta\rdata: value\r\r").isEmpty());
		const auto completed = stream.finish();
		QCOMPARE(completed.size(), 1);
		QCOMPARE(completed[0].type, QByteArray("delta"));
		QCOMPARE(completed[0].data, QByteArray("value"));
		QVERIFY(stream.feed("data: incomplete\n").isEmpty());
		QVERIFY(stream.finish().isEmpty());
		QVERIFY(stream.feed("data: missing-terminator").isEmpty());
		QVERIFY(stream.finish().isEmpty());
	}

	void fieldsAndComments()
	{
		ChatEventStream stream;
		const auto events = stream.feed("event: ignored\n\n:comment\nretry: 10\nid: 1\nevent:\ndata:  spaced\n\n");
		QCOMPARE(events.size(), 1);
		QCOMPARE(events[0].type, QByteArray("message"));
		QCOMPARE(events[0].data, QByteArray(" spaced"));
	}
};

QTEST_GUILESS_MAIN(ChatEventStreamTest)
#include "ChatEventStreamTest.moc"
