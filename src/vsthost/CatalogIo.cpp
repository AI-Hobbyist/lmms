#include "vsthost/CatalogIo.h"
#include "vsthost/HostSession.h"
#include <QJsonDocument>

namespace lmms::vsthost {
CatalogIo::Reply CatalogIo::request(QJsonObject operation) const
{
	if (m_stop.stop_requested())
	{
		return {Error::InvalidState};
	}
	if (m_helper.isEmpty())
	{
		return {Error::MissingHelper};
	}
	if (!m_timeout || m_timeout > 300000)
	{
		return {Error::InvalidMessage};
	}
	operation["schema"] = 1;
	const auto bytes = QJsonDocument(operation).toJson(QJsonDocument::Compact);
	if (bytes.size() > MaxControlBytes)
	{
		return {Error::InvalidMessage};
	}
	HostSession session;
	std::stop_callback cancel(m_stop, [&] { session.cancel(); });
	auto opened = session.open({m_helper.toStdWString(), {}, m_timeout}).get();
	if (opened.error != Error::None)
	{
		return {opened.error, session.fault().nativeCode};
	}
	const auto reply
		= session.request(MessageType::Scan, std::vector<std::uint8_t>(bytes.begin(), bytes.end()), m_timeout).get();
	if (reply.error != Error::None)
	{
		return {reply.error, session.fault().nativeCode};
	}
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(
		QByteArray(reinterpret_cast<const char*>(reply.payload.data()), static_cast<qsizetype>(reply.payload.size())),
		&parse);
	if (parse.error != QJsonParseError::NoError || !document.isObject()
		|| document.object().value("schema").toDouble() != 1)
	{
		return {Error::InvalidMessage};
	}
	const auto closed = session.close().get();
	if (closed.error != Error::None)
	{
		return {closed.error, session.fault().nativeCode};
	}
	return {Error::None, 0, document.object()};
}
} // namespace lmms::vsthost
