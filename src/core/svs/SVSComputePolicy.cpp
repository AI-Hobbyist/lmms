#include "SVSComputePolicy.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>

#include "ConfigManager.h"
#include "svs_compute.hpp"

namespace lmms::svs {
namespace {
constexpr auto RuntimeVersion = "svs-compute-1/ort-1.23.0/dml-1.15.4";
std::mutex probeMutex;
std::map<QString, QJsonObject> probeResults;
std::filesystem::path runtimeDirectory()
{
	const auto configured = std::getenv("SVS_COMPUTE_RUNTIME_DIR");
	if (configured && *configured) { return std::filesystem::u8path(configured); }
	return std::filesystem::u8path(QCoreApplication::applicationDirPath().toUtf8().constData()) / "svs" / "compute";
}
svs_compute::Library library(const std::filesystem::path& directory)
{
#ifdef Q_OS_WIN
	return svs_compute::Library(directory.parent_path().parent_path() / "plugins" / "SVSCompute.dll");
#elif defined(Q_OS_MACOS)
	return svs_compute::Library(directory.parent_path().parent_path() / "plugins" / "libSVSCompute.dylib");
#else
	return svs_compute::Library(directory.parent_path().parent_path() / "plugins" / "libSVSCompute.so");
#endif
}
QJsonObject probeDevice(const QString& id, bool fresh)
{
	std::lock_guard<std::mutex> lock(probeMutex);
	if (!fresh && probeResults.count(id)) { return probeResults.at(id); }
	// A fresh worker resolves the LUID again, detecting hardware removal before any cache lookup.
	QJsonObject result;
	try
	{
		const auto directory = runtimeDirectory();
		const auto client = library(directory);
		const auto context = client.context(directory);
		result = QJsonDocument::fromJson(QByteArray::fromStdString(context.probe("directml", id.toUtf8().constData())))
					 .object();
	}
	catch (const std::exception& error)
	{
		result = {{"available", false}, {"reason", QString::fromUtf8(error.what())}};
	}
	probeResults[id] = result;
	return result;
}
} // namespace
ComputePolicyUpdates& ComputePolicyUpdates::instance()
{
	static ComputePolicyUpdates updates;
	return updates;
}
QJsonObject requestedComputePolicy()
{
	auto* config = ConfigManager::inst();
	const auto backend = config->value("svs", "computeBackend", "cpu");
	return {{"requestedBackend", backend},
		{"requestedDevice", backend == "cpu" ? "cpu" : config->value("svs", "computeDevice", "cpu")},
		{"policyRevision", double(config->value("svs", "computePolicyRevision", "0").toULongLong())}};
}
QJsonObject resolveComputePolicy(const QJsonObject& requested, const QString& engineType, const QJsonObject& compute,
	const std::function<QJsonObject(const QString&)>& probe)
{
	if (engineType != "ai") { return {}; }
	auto policy = requested;
	policy["effectiveBackend"] = "cpu";
	policy["effectiveDevice"] = "cpu";
	policy["runtimeVersion"] = RuntimeVersion;
	policy["stageOverrides"] = compute["stageConstraints"].toArray();
	policy["fallbackReason"] = "";
	const auto backends = compute["supportedBackends"].toArray();
	const bool supported = compute["protocolVersion"].toInt() == 1 && compute["runtime"].toString() == "svs-compute-1"
		&& backends.contains("cpu");
	policy["supported"] = supported;
	if (!supported)
	{
		policy["fallbackReason"] = "AI engine does not support the shared compute interface";
		return policy;
	}
	const auto backend = requested["requestedBackend"].toString("cpu");
	if (backend == "cpu") { return policy; }
	if (backend != "directml" || !backends.contains("directml"))
	{
		policy["fallbackReason"] = "Requested backend is not supported by this AI engine";
		return policy;
	}
	try
	{
		const auto id = requested["requestedDevice"].toString();
		const auto result = probe ? probe(id) : probeDevice(id, false);
		if (!result["available"].toBool() || result["effectiveBackend"].toString() != "directml"
			|| result["effectiveDevice"].toString() != id
			|| result["providerEvidence"].toObject()["dmlNodes"].toInt() <= 0)
		{
			policy["fallbackReason"] = result["reason"].toString("Requested DirectML device probe failed");
			return policy;
		}
		policy["effectiveBackend"] = "directml";
		policy["effectiveDevice"] = id;
	}
	catch (const std::exception& error)
	{
		policy["fallbackReason"] = QString::fromUtf8(error.what());
	}
	return policy;
}
QJsonArray computeDevices(QString& error)
{
	error.clear();
	try
	{
		const auto directory = runtimeDirectory();
		const auto client = library(directory);
		const auto context = client.context(directory);
		const auto devices
			= QJsonDocument::fromJson(QByteArray::fromStdString(context.devices())).object()["devices"].toArray();
		std::lock_guard<std::mutex> lock(probeMutex);
		probeResults.clear();
		for (const auto& entry : devices)
		{
			const auto device = entry.toObject();
			if (device["backend"].toString() == "directml") { probeResults[device["device"].toString()] = device; }
		}
		return devices;
	}
	catch (const std::exception& failure)
	{
		error = QString::fromUtf8(failure.what());
		return {};
	}
}
void refreshComputePolicy(QJsonObject& document)
{
	if (!document.contains("computePolicy")) { return; }
	const auto policy = resolveComputePolicy(document["computePolicy"].toObject(), "ai",
		document["capabilities"].toObject()["compute"].toObject(),
		[](const QString& id) { return probeDevice(id, true); });
	document["computePolicy"] = policy;
	document["computeBackend"] = policy["effectiveBackend"];
	document["computeDevice"] = policy["effectiveDevice"];
}
bool applyComputeSettings(const QString& backend, const QString& device)
{
	auto* config = ConfigManager::inst();
	const auto selected = backend == "cpu" ? QString("cpu") : device;
	if (config->value("svs", "computeBackend", "cpu") == backend
		&& config->value("svs", "computeDevice", "cpu") == selected)
	{
		return false;
	}
	config->setValue("svs", "computeBackend", backend);
	config->setValue("svs", "computeDevice", selected);
	const auto revision = config->value("svs", "computePolicyRevision", "0").toULongLong();
	config->setValue("svs", "computePolicyRevision", QString::number(revision + 1));
	emit ComputePolicyUpdates::instance().changed();
	return true;
}
} // namespace lmms::svs
