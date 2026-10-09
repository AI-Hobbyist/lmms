#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <memory>
#include <vector>

#include "SVCChunking.h"
#include "svc.h"

namespace lmms::svc {
struct EngineProfile
{
	QString id;
	QString name;
	QString defaultAddress;
	QJsonObject capabilities;
	const svc_engine* api = nullptr;
	std::shared_ptr<void> context;
	uint32_t inputRate = 0;
	bool inputIsPcm = false;
};

struct Connection
{
	QString address;
	QString token;
	bool remembered = false;
};

class Catalog : public QObject
{
	Q_OBJECT
public:
	static Catalog& instance();
	const std::vector<EngineProfile>& engines() const { return m_engines; }
	EngineProfile engine(const QString& id) const;
	QString install(EngineProfile profile);
	Connection connection(const QString& id) const;
	QString setConnection(const QString& id, const Connection& connection);
	QJsonObject requestSelection(const QJsonObject& saved, QString& error) const;

signals:
	void changed();
	void connectionChanged(const QString& id);

private:
	Catalog();
	std::vector<EngineProfile> m_engines;
	QHash<QString, QString> m_sessionTokens;
};

bool conditionsMatch(const QJsonObject& conditions, const QJsonObject& values);
QJsonObject selectionContext(const QJsonObject& selection, const QJsonObject& model);
ChunkConfig chunkDefaults();
QString setChunkDefaults(const ChunkConfig& config);
} // namespace lmms::svc
