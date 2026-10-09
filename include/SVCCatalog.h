#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "SVCChunking.h"
#include "svc.h"
#include "svc_plugin.h"

class QTimer;

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

struct ReconnectPolicy
{
	int intervalSeconds = 5;
	int maximumRetries = 3;
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
	void refresh(const QString& id);
	QString status(const QString& id) const { return m_status.value(id); }
	ReconnectPolicy reconnectPolicy() const;
	QString setReconnectPolicy(const ReconnectPolicy& policy);
	void reconnectDisconnected();
	void shutdown();
	~Catalog() override;

signals:
	void changed();
	void connectionChanged(const QString& id);

private:
	Catalog();
	std::vector<EngineProfile> m_engines;
	QHash<QString, QString> m_sessionTokens;
	struct Module
	{
		const svc_plugin* api;
		std::shared_ptr<void> library;
	};
	QHash<QString, Module> m_modules;
	QHash<QString, QString> m_status;
	QHash<QString, uint64_t> m_versions;
	QHash<QString, QTimer*> m_retryTimers;
	QHash<QString, int> m_retryCounts;
	QHash<QString, QString> m_connectionErrors;
	QSet<QString> m_reconnecting;
	QSet<QString> m_connecting;
	struct Discovery
	{
		QString id;
		Connection connection;
		uint64_t version;
		Module module;
	};
	std::vector<Discovery> m_discoveries;
	std::thread m_worker;
	std::mutex m_mutex;
	std::condition_variable m_wake;
	bool m_stopping = false;
	void discover();
	void beginDiscovery(const QString& id);
	void stopRetries(const QString& id);
	void discoveryFinished(const QString& id, const QString& error);
};

bool conditionsMatch(const QJsonObject& conditions, const QJsonObject& values);
QJsonObject selectionContext(const QJsonObject& selection, const QJsonObject& model);
QJsonArray parameterDefinitions(const EngineProfile& profile, const QJsonObject& model);
ChunkConfig chunkDefaults();
QString setChunkDefaults(const ChunkConfig& config);
} // namespace lmms::svc
