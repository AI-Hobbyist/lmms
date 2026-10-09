#include "Http.h"

#include <QRegularExpression>
#include <algorithm>
#include <array>

namespace svc_rvc {
QUrl endpoint(const QString& address, const QString& route)
{
	QUrl url(address);
	if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https") || url.host().isEmpty()
		|| !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
	{
		return {};
	}
	auto path = url.path();
	while (path.endsWith('/'))
	{
		path.chop(1);
	}
	if (path.endsWith("/api/v1")) { path.chop(7); }
	url.setPath(path + route);
	return url;
}
Http::Http(QUrl url, QByteArray token, bool upload, int timeoutMs)
	: m_socket(std::make_unique<QSslSocket>())
	, m_url(std::move(url))
	, m_token(std::move(token))
	, m_timeout(timeoutMs)
	, m_upload(upload)
{
	m_clock.start();
	m_socket->setReadBufferSize(SVC_MAX_FEED * 2);
	if (!m_url.isValid() || m_url.host().isEmpty() || m_token.contains('\r') || m_token.contains('\n'))
	{
		fail("Invalid SVC address or credential format");
		return;
	}
	if (m_url.scheme() == "https") { m_socket->connectToHostEncrypted(m_url.host(), m_url.port(443)); }
	else
	{
		m_socket->connectToHost(m_url.host(), m_url.port(80));
	}
}
void Http::fail(const QString& error)
{
	m_error = error;
	m_finished = true;
	m_socket->abort();
}
void Http::cancel()
{ fail("Request cancelled"); }
bool Http::pump(
	const std::function<int64_t(uint8_t*, size_t)>& input, const std::function<bool(const uint8_t*, size_t)>& output)
{
	if (m_finished) { return false; }
	if (m_clock.elapsed() > m_timeout)
	{
		fail("HTTP timeout (408)");
		return false;
	}
	if (!m_connected)
	{
		const bool connected
			= m_url.scheme() == "https" ? m_socket->waitForEncrypted(50) : m_socket->waitForConnected(50);
		if (!connected)
		{
			if (m_socket->error() != QAbstractSocket::SocketTimeoutError)
			{
				fail("SVC connection or TLS verification failed");
			}
			return !m_finished;
		}
		m_connected = true;
	}
	if (!m_requested)
	{
		auto host = QUrl::toAce(m_url.host());
		if (host.contains(':')) { host = '[' + host + ']'; }
		if (m_url.port() >= 0) { host += ':' + QByteArray::number(m_url.port()); }
		auto target = m_url.path(QUrl::FullyEncoded).toUtf8();
		if (m_url.hasQuery()) { target += '?' + m_url.query(QUrl::FullyEncoded).toUtf8(); }
		QByteArray request = (m_upload ? "POST " : "GET ") + target + " HTTP/1.1\r\nHost: " + host
			+ "\r\nConnection: close\r\nAccept: */*\r\n";
		if (!m_token.isEmpty()) { request += "Authorization: Bearer " + m_token + "\r\n"; }
		if (m_upload) { request += "Content-Type: audio/wav\r\nTransfer-Encoding: chunked\r\n"; }
		request += "\r\n";
		if (m_socket->write(request) != request.size())
		{
			fail("Cannot write HTTP request");
			return false;
		}
		m_requested = true;
		m_uploaded = !m_upload;
	}
	// Read early errors before asking the host for another upload block.
	if (m_socket->bytesAvailable() > 0)
	{
		m_buffer += m_socket->read(SVC_MAX_FEED);
		if (!consume(output)) { return false; }
	}
	if (!m_uploaded && (!m_status || m_status == 200) && m_socket->bytesToWrite() < SVC_MAX_FEED)
	{
		std::array<uint8_t, SVC_MAX_FEED> bytes{};
		const auto count = input(bytes.data(), bytes.size());
		if (count < 0 || count > SVC_MAX_FEED)
		{
			fail("SVC input read failed");
			return false;
		}
		QByteArray block;
		if (count)
		{
			block = QByteArray::number(count, 16) + "\r\n";
			block.append(reinterpret_cast<const char*>(bytes.data()), count);
			block += "\r\n";
			m_sent += count;
		}
		else
		{
			block = "0\r\n\r\n";
			m_uploaded = true;
		}
		if (m_socket->write(block) != block.size())
		{
			fail("SVC upload failed");
			return false;
		}
	}
	if (m_socket->state() == QAbstractSocket::ConnectedState)
	{
		if (m_socket->bytesToWrite()) { m_socket->waitForBytesWritten(50); }
		else if (!m_socket->bytesAvailable()) { m_socket->waitForReadyRead(50); }
	}
	if (m_socket->bytesAvailable())
	{
		m_buffer += m_socket->read(SVC_MAX_FEED);
		if (!consume(output)) { return false; }
	}
	if (m_socket->state() == QAbstractSocket::UnconnectedState && !m_socket->bytesAvailable() && !m_finished)
	{
		if (m_status && !m_chunked && m_remaining < 0 && m_buffer.isEmpty()) { m_finished = true; }
		else
		{
			fail("HTTP response disconnected or truncated");
		}
	}
	return !m_finished;
}
bool Http::consume(const std::function<bool(const uint8_t*, size_t)>& output)
{
	if (!m_status)
	{
		const auto end = m_buffer.indexOf("\r\n\r\n");
		if ((end < 0 && m_buffer.size() > SVC_MAX_HEADER) || end > SVC_MAX_HEADER)
		{
			fail("HTTP headers exceed limit");
			return false;
		}
		if (end < 0) { return true; }
		const auto lines = m_buffer.left(end).split('\n');
		const auto match = QRegularExpression("^HTTP/1\\.[01] ([0-9]{3})(?: .*)?$")
							   .match(QString::fromLatin1(lines.first().trimmed()));
		if (!match.hasMatch())
		{
			fail("Invalid HTTP status line");
			return false;
		}
		m_status = match.captured(1).toInt();
		if (m_status < 200 || (m_status >= 300 && m_status < 400))
		{
			fail("HTTP redirects and interim responses are unsupported");
			return false;
		}
		for (int index = 1; index < lines.size(); ++index)
		{
			const auto line = lines[index].trimmed();
			const auto colon = line.indexOf(':');
			if (colon <= 0)
			{
				fail("Invalid HTTP header");
				return false;
			}
			const auto key = line.left(colon).toLower();
			if (m_headers.contains(key)
				&& (key == "content-length" || key == "transfer-encoding" || key == "content-type"))
			{
				fail("Duplicate critical HTTP header");
				return false;
			}
			m_headers.insert(key, line.mid(colon + 1).trimmed());
		}
		m_buffer.remove(0, end + 4);
		m_chunked = m_headers.value("transfer-encoding").toLower() == "chunked";
		if (m_headers.contains("transfer-encoding") && !m_chunked)
		{
			fail("Unsupported HTTP transfer encoding");
			return false;
		}
		if (m_chunked && m_headers.contains("content-length"))
		{
			fail("Ambiguous HTTP body framing");
			return false;
		}
		if (m_headers.contains("content-length"))
		{
			bool valid;
			m_remaining = m_headers.value("content-length").toLongLong(&valid);
			if (!valid || m_remaining < 0)
			{
				fail("Invalid HTTP Content-Length");
				return false;
			}
		}
	}
	for (;;)
	{
		if (m_chunked)
		{
			if (m_trailers)
			{
				if (m_buffer.startsWith("\r\n"))
				{
					m_buffer.remove(0, 2);
					m_finished = true;
				}
				else
				{
					const auto end = m_buffer.indexOf("\r\n\r\n");
					if (end > SVC_MAX_HEADER)
					{
						fail("HTTP trailers exceed limit");
						return false;
					}
					if (end >= 0)
					{
						m_buffer.remove(0, end + 4);
						m_finished = true;
					}
					else if (m_buffer.size() > SVC_MAX_HEADER)
					{
						fail("HTTP trailers exceed limit");
						return false;
					}
				}
				break;
			}
			if (m_chunkCrlf)
			{
				if (m_buffer.size() < 2) { break; }
				if (!m_buffer.startsWith("\r\n"))
				{
					fail("Invalid HTTP chunk terminator");
					return false;
				}
				m_buffer.remove(0, 2);
				m_chunkCrlf = false;
				m_chunkRemaining = -1;
			}
			if (m_chunkRemaining < 0)
			{
				const auto end = m_buffer.indexOf("\r\n");
				if (end > 1024)
				{
					fail("HTTP chunk header exceeds limit");
					return false;
				}
				if (end < 0)
				{
					if (m_buffer.size() > 1024)
					{
						fail("HTTP chunk header exceeds limit");
						return false;
					}
					break;
				}
				bool valid;
				m_chunkRemaining = m_buffer.left(end).split(';').first().toLongLong(&valid, 16);
				if (!valid || m_chunkRemaining < 0)
				{
					fail("Invalid HTTP chunk size");
					return false;
				}
				m_buffer.remove(0, end + 2);
				if (!m_chunkRemaining)
				{
					m_trailers = true;
					continue;
				}
			}
		}
		auto count = std::min<int64_t>(m_buffer.size(), SVC_MAX_FEED);
		if (m_chunked) { count = std::min(count, m_chunkRemaining); }
		else if (m_remaining >= 0) { count = std::min(count, m_remaining); }
		if (count)
		{
			if (!output(reinterpret_cast<const uint8_t*>(m_buffer.constData()), count))
			{
				fail("Response consumer rejected data");
				return false;
			}
			m_buffer.remove(0, count);
			if (m_chunked)
			{
				m_chunkRemaining -= count;
				if (!m_chunkRemaining) { m_chunkCrlf = true; }
			}
			else if (m_remaining >= 0) { m_remaining -= count; }
		}
		if (!m_chunked && m_remaining == 0)
		{
			m_finished = true;
			break;
		}
		if (!count) { break; }
	}
	if (m_finished && !m_buffer.isEmpty())
	{
		fail("Bytes after HTTP body completion");
		return false;
	}
	return !m_finished;
}
} // namespace svc_rvc
