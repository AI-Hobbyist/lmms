#include "agent/chat/ChatEventStream.h"

namespace lmms::agent::chat {

QVector<ChatStreamEvent> ChatEventStream::feed(const QByteArray& bytes)
{
	m_pending.append(bytes);
	return consume(false);
}

QVector<ChatStreamEvent> ChatEventStream::finish()
{
	auto events = consume(true);
	// SSE does not dispatch an event without its terminating blank line.
	// The protocol adapter separately checks for its own completion marker.
	m_pending.clear();
	m_data.clear();
	m_type.clear();
	m_firstLine = true;
	return events;
}

QVector<ChatStreamEvent> ChatEventStream::consume(bool endOfStream)
{
	QVector<ChatStreamEvent> events;
	qsizetype consumed = 0;
	for (qsizetype index = 0; index < m_pending.size(); ++index)
	{
		const auto character = m_pending[index];
		if (character != '\r' && character != '\n')
		{
			continue;
		}
		if (character == '\r' && index + 1 == m_pending.size() && !endOfStream)
		{
			break; // A CRLF pair may be split across network reads.
		}
		consumeLine(m_pending.mid(consumed, index - consumed), events);
		if (character == '\r' && index + 1 < m_pending.size() && m_pending[index + 1] == '\n')
		{
			++index;
		}
		consumed = index + 1;
	}
	m_pending.remove(0, consumed);
	return events;
}

void ChatEventStream::consumeLine(QByteArray line, QVector<ChatStreamEvent>& events)
{
	if (m_firstLine)
	{
		if (line.startsWith(QByteArray::fromHex("efbbbf")))
		{
			line.remove(0, 3);
		}
		m_firstLine = false;
	}
	if (line.isEmpty())
	{
		if (!m_data.isEmpty())
		{
			m_data.chop(1);
			events.append({m_type.isEmpty() ? QByteArray("message") : m_type, m_data});
		}
		m_type.clear();
		m_data.clear();
		return;
	}
	if (line.startsWith(':'))
	{
		return;
	}
	const auto separator = line.indexOf(':');
	const auto field = separator < 0 ? line : line.left(separator);
	auto value = separator < 0 ? QByteArray{} : line.mid(separator + 1);
	if (value.startsWith(' '))
	{
		value.remove(0, 1);
	}
	if (field == "event")
	{
		m_type = value;
	}
	else if (field == "data")
	{
		m_data.append(value);
		m_data.append('\n');
	}
	// id/retry control EventSource reconnection. Chat requests do not reconnect
	// automatically because replaying a generation can duplicate paid work.
}

} // namespace lmms::agent::chat
