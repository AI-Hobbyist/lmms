#ifndef LMMS_SVS_COMPUTE_POLICY_H
#define LMMS_SVS_COMPUTE_POLICY_H
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

namespace lmms::svs {
class ComputePolicyUpdates : public QObject
{
	Q_OBJECT
public:
	static ComputePolicyUpdates& instance();
signals:
	void changed();
};
// Captured on the owner thread. Render workers consume this value, never ConfigManager.
QJsonObject requestedComputePolicy();
QJsonObject resolveComputePolicy(const QJsonObject& requested, const QString& engineType, const QJsonObject& compute,
	const std::function<QJsonObject(const QString&)>& probe = {});
QJsonArray computeDevices(QString& error);
void refreshComputePolicy(QJsonObject& document);
bool applyComputeSettings(const QString& backend, const QString& device);
} // namespace lmms::svs
#endif
