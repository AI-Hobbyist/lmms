#pragma once
#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QSslSocket>
#include <QUrl>
#include <functional>
#include <memory>

#include "svc.h"

namespace svc_rvc {
class Http
{
public:
	Http(QUrl url, QByteArray token, bool upload, int timeoutMs);
	// Bounded upload/read operations; socket waits total at most 100 ms.
	bool pump(const std::function<int64_t(uint8_t*, size_t)>& input,
		const std::function<bool(const uint8_t*, size_t)>& output);
	void cancel();
	int status() const { return m_status; }
	QByteArray header(const QByteArray& key) const { return m_headers.value(key); }
	QString error() const { return m_error; }
	bool finished() const { return m_finished; }
	bool uploaded() const { return m_uploaded; }
	uint64_t sent() const { return m_sent; }

private:
	void fail(const QString& error);
	bool consume(const std::function<bool(const uint8_t*, size_t)>& output);
	std::unique_ptr<QSslSocket> m_socket;
	QUrl m_url;
	QByteArray m_token, m_buffer;
	QHash<QByteArray, QByteArray> m_headers;
	QElapsedTimer m_clock;
	int m_timeout, m_status = 0;
	bool m_upload, m_connected = false, m_requested = false, m_uploaded = false, m_finished = false;
	bool m_chunked = false, m_chunkCrlf = false, m_trailers = false;
	int64_t m_remaining = -1, m_chunkRemaining = -1;
	uint64_t m_sent = 0;
	QString m_error;
};
QUrl endpoint(const QString& address, const QString& route);
} // namespace svc_rvc
