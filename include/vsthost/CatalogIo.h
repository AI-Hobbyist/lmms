#ifndef LMMS_VSTHOST_CATALOG_IO_H
#define LMMS_VSTHOST_CATALOG_IO_H
#include "lmms_export.h"
#include "vsthost/Protocol.h"
#include <QJsonObject>
#include <QString>
#include <stop_token>

namespace lmms::vsthost {
// Every call owns a disposable supervised worker. A blocked filesystem call is
// abandoned by killing its Job, without leaving a blocked thread in the DAW.
class LMMS_EXPORT CatalogIo
{
public:
	struct Reply
	{
		Error error = Error::None;
		std::uint32_t nativeCode = 0;
		QJsonObject object;
	};
	CatalogIo(QString helper, std::uint32_t timeoutMs, std::stop_token stop)
		: m_helper(std::move(helper))
		, m_timeout(timeoutMs)
		, m_stop(stop)
	{
	}
	Reply request(QJsonObject operation) const;

private:
	QString m_helper;
	std::uint32_t m_timeout;
	std::stop_token m_stop;
};
} // namespace lmms::vsthost
#endif
