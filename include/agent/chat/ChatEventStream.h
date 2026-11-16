#ifndef LMMS_AGENT_CHAT_EVENT_STREAM_H
#define LMMS_AGENT_CHAT_EVENT_STREAM_H

#include <QByteArray>
#include <QVector>

namespace lmms::agent::chat {

struct ChatStreamEvent
{
	QByteArray type;
	QByteArray data;
};

// Incremental SSE framing. Protocol adapters consume complete UTF-8 event data,
// never partial network chunks or partially received tool arguments.
class ChatEventStream
{
public:
	QVector<ChatStreamEvent> feed(const QByteArray& bytes);
	QVector<ChatStreamEvent> finish();

private:
	QVector<ChatStreamEvent> consume(bool endOfStream);
	void consumeLine(QByteArray line, QVector<ChatStreamEvent>& events);
	QByteArray m_pending;
	QByteArray m_type;
	QByteArray m_data;
	bool m_firstLine = true;
};

} // namespace lmms::agent::chat

#endif
