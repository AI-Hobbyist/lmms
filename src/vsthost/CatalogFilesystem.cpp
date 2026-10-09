#include "CatalogFilesystem.h"
#include "vsthost/PluginCatalog.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QtEndian>
#include <windows.h>
#include <optional>
#include <algorithm>
#include <stdexcept>
namespace lmms::vsthost {
namespace {
constexpr qsizetype MaxModules = 65536;
struct Module
{
	QString path, binary;
	Format format = Format::Vst3;
	Architecture architecture = Architecture::X64;
};
bool isBackup(const QString& name)
{
	for (const auto* suffix : {".bak", ".backup", ".old", ".disabled"})
	{
		if (name.endsWith(suffix, Qt::CaseInsensitive))
		{
			return true;
		}
	}
	return false;
}

QString canonicalPath(const QString& path)
{
	const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
		nullptr);
	if (handle == INVALID_HANDLE_VALUE)
	{
		return {};
	}
	const auto length = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	std::wstring buffer(length, L'\0');
	const auto written
		= length ? GetFinalPathNameByHandleW(handle, buffer.data(), length, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS) : 0;
	CloseHandle(handle);
	if (!written || written >= length)
	{
		return {};
	}
	auto result = QString::fromWCharArray(buffer.data(), written);
	if (result.startsWith(QStringLiteral("\\\\?\\UNC\\")))
	{
		result = "\\\\" + result.mid(8);
	}
	else if (result.startsWith(QStringLiteral("\\\\?\\")))
	{
		result = result.mid(4);
	}
	return QDir::fromNativeSeparators(result);
}

QString directoryKey(const QString& path)
{
	// Resolve directory aliases by file ID as well as path: junctions can form
	// cycles without a finite canonical path. No plugin code is loaded here.
	const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
		nullptr);
	BY_HANDLE_FILE_INFORMATION info{};
	const bool valid = handle != INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle, &info);
	if (handle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(handle);
	}
	if (valid)
	{
		return QString::number(info.dwVolumeSerialNumber) + ":" + QString::number(info.nFileIndexHigh) + ":"
			+ QString::number(info.nFileIndexLow);
	}
	return scanPathKey(canonicalPath(path));
}

bool peArchitecture(const QString& path, Architecture& architecture, bool dll)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() < 64)
	{
		return false;
	}
	const auto dos = file.read(64);
	if (dos.size() != 64 || dos.first(2) != "MZ")
	{
		return false;
	}
	const auto offset = qFromLittleEndian<quint32>(dos.constData() + 60);
	if (offset < 64 || offset > static_cast<quint64>(file.size() - 24) || !file.seek(offset))
	{
		return false;
	}
	const auto header = file.read(24);
	if (header.size() != 24 || header.first(4) != QByteArray("PE\0\0", 4)
		|| (dll && !(qFromLittleEndian<quint16>(header.constData() + 22) & 0x2000)))
	{
		return false;
	}
	const auto machine = qFromLittleEndian<quint16>(header.constData() + 4);
	const auto size = qFromLittleEndian<quint16>(header.constData() + 20);
	if (size < 2 || offset + 24ull + size > static_cast<quint64>(file.size()))
	{
		return false;
	}
	const auto optional = file.read(2);
	if (optional.size() != 2)
	{
		return false;
	}
	const auto magic = qFromLittleEndian<quint16>(optional.constData());
	if (machine == static_cast<unsigned>(Architecture::X86) && magic == 0x10b && size >= 96)
	{
		architecture = Architecture::X86;
		return true;
	}
	if (machine == static_cast<unsigned>(Architecture::X64) && magic == 0x20b && size >= 112)
	{
		architecture = Architecture::X64;
		return true;
	}
	return false;
}

QString enclosingBundle(const QString& path)
{
	auto directory = QFileInfo(path).dir();
	while (true)
	{
		if (QFileInfo(directory.absolutePath()).suffix().compare("vst3", Qt::CaseInsensitive) == 0)
		{
			return canonicalPath(directory.absolutePath());
		}
		if (!directory.cdUp())
		{
			return {};
		}
	}
}

void discover(const ScanRoot& root, const QString& path, QSet<QString>& directories, QSet<QString>& modules,
	std::vector<Module>& discovered, std::vector<CatalogFailure>& failures, std::stop_token stop, unsigned depth = 0)
{
	if (stop.stop_requested())
	{
		return;
	}
	if (discovered.size() >= MaxModules || directories.size() >= MaxModules || depth > 128)
	{
		failures.push_back({path, "discovery capacity", Error::InvalidMessage});
		return;
	}
	const QFileInfo info(path);
	if (!info.exists())
	{
		failures.push_back({path, "directory", Error::LoadFailed});
		return;
	}
	if (isBackup(info.fileName()))
	{
		return;
	}
	const bool vst3 = info.suffix().compare("vst3", Qt::CaseInsensitive) == 0;
	const bool vst2 = info.isFile() && info.suffix().compare("dll", Qt::CaseInsensitive) == 0;
	if (!vst3 || info.isFile())
	{
		const auto bundle = enclosingBundle(canonicalPath(path));
		if (!bundle.isEmpty())
		{
			discover(root, bundle, directories, modules, discovered, failures, stop, depth);
			return;
		}
	}
	if (vst3 || vst2)
	{
		if (!root.formats.contains(vst3 ? "vst3" : "vst2"))
		{
			return;
		}
		const auto canonical = canonicalPath(path);
		const auto moduleKey = scanPathKey(canonical);
		if (canonical.isEmpty() || modules.contains(moduleKey))
		{
			return;
		}
		modules.insert(moduleKey);
		auto add = [&](const QString& binary, std::optional<Architecture> expected) {
			Module module;
			module.path = canonical;
			module.binary = canonicalPath(binary);
			module.format = vst3 ? Format::Vst3 : Format::Vst2;
			if (module.binary.isEmpty() || !peArchitecture(module.binary, module.architecture, true)
				|| (expected && *expected != module.architecture))
			{
				failures.push_back({binary, "PE", Error::UnsupportedArchitecture});
				return;
			}
			const auto binaryKey
				= "binary:" + QString::number(static_cast<unsigned>(module.format)) + ":" + scanPathKey(module.binary);
			if (modules.contains(binaryKey))
			{
				return;
			}
			modules.insert(binaryKey);
			discovered.push_back(std::move(module));
		};
		if (info.isFile())
		{
			add(canonical, {});
		}
		else
		{
			bool found = false;
			for (const auto& [folder, architecture] :
				{std::pair{"x86-win", Architecture::X86}, std::pair{"x86_64-win", Architecture::X64}})
			{
				const auto binary = canonical + "/Contents/" + folder + "/" + QFileInfo(canonical).fileName();
				if (QFileInfo(binary).isFile())
				{
					found = true;
					add(binary, architecture);
				}
			}
			if (!found)
			{
				failures.push_back({canonical, "bundle", Error::LoadFailed});
			}
		}
		return; // A VST3 bundle is atomic, including when its format is disabled.
	}
	if (!info.isDir())
	{
		return;
	}
	const auto key = directoryKey(path);
	if (key.isEmpty() || directories.contains(key))
	{
		return;
	}
	directories.insert(key);
	const QDir directory(path);
	if (!info.isReadable())
	{
		failures.push_back({path, "directory", Error::LoadFailed});
		return;
	}
	const auto children
		= directory.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
	for (const auto& child : children)
	{
		if (stop.stop_requested())
		{
			return;
		}
		if (child.isDir() && !root.recursive && child.suffix().compare("vst3", Qt::CaseInsensitive))
		{
			continue;
		}
		discover(root, child.absoluteFilePath(), directories, modules, discovered, failures, stop, depth + 1);
	}
}

QByteArray fingerprint(const Module& module, std::stop_token stop)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	QStringList files{module.binary};
	// Include bundle resources and metadata, but never follow reparse points
	// into external trees. The selected binary is included even if it is an alias.
	if (QFileInfo(module.path).isDir())
	{
		std::vector<QString> pending{module.path};
		while (!pending.empty())
		{
			if (stop.stop_requested())
			{
				return {};
			}
			const auto path = std::move(pending.back());
			pending.pop_back();
			for (const auto& info :
				QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name))
			{
				const auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(info.absoluteFilePath().utf16()));
				if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
					|| isBackup(info.fileName()))
				{
					continue;
				}
				if (info.isDir())
				{
					pending.push_back(info.absoluteFilePath());
				}
				else if (info.isFile() && info.canonicalFilePath() != module.binary)
				{
					files.append(info.absoluteFilePath());
				}
				if (files.size() + pending.size() > 65536)
				{
					return {};
				}
			}
		}
		std::sort(files.begin(), files.end());
	}
	for (const auto& path : files)
	{
		if (stop.stop_requested())
		{
			return {};
		}
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
		{
			return {};
		}
		const auto relative = QDir(module.path).relativeFilePath(path).toUtf8();
		hash.addData(QByteArray::number(relative.size()));
		hash.addData(":");
		hash.addData(relative);
		hash.addData(QByteArray::number(file.size()));
		hash.addData(":");
		while (!file.atEnd())
		{
			if (stop.stop_requested())
			{
				return {};
			}
			const auto bytes = file.read(1024 * 1024);
			if (bytes.isEmpty() && file.error() != QFile::NoError)
			{
				return {};
			}
			hash.addData(bytes);
		}
	}
	return hash.result();
}

}

QJsonObject catalogFileOperation(const QJsonObject& request)
{
	if (request.value("schema").toDouble() != 1 || !request.value("op").isString())
	{
		throw std::invalid_argument("Invalid filesystem request");
	}
	auto path = [&](const char* key) {
		const auto value = request.value(key);
		const auto result = value.toString();
		if (!value.isString() || result.size() > 32767 || result.contains(QChar(0)) || !QDir::isAbsolutePath(result))
		{
			throw std::invalid_argument("Invalid filesystem path");
		}
		return result;
	};
	QJsonObject reply{{"schema", 1}};
	const auto op = request.value("op").toString();
	if (op == "discover")
	{
		std::vector<ScanRoot> roots;
		QString error;
		if (!decodeScanRoots(
				QJsonDocument(request.value("roots").toObject()).toJson(QJsonDocument::Compact), roots, error)
			|| roots.size() != 1)
		{
			throw std::invalid_argument("Invalid discovery root");
		}
		QSet<QString> directories, visited;
		std::vector<Module> modules;
		std::vector<CatalogFailure> failures;
		discover(roots.front(), roots.front().path, directories, visited, modules, failures, {});
		QJsonArray items, errors;
		for (const auto& module : modules)
		{
			items.append(QJsonObject{{"path", module.path}, {"binary", module.binary},
				{"format", static_cast<int>(module.format)}, {"architecture", static_cast<int>(module.architecture)}});
		}
		for (const auto& failure : failures)
		{
			errors.append(QJsonObject{
				{"path", failure.path}, {"operation", failure.operation}, {"error", static_cast<int>(failure.error)}});
		}
		reply["modules"] = items;
		reply["failures"] = errors;
	}
	else if (op == "fingerprint")
	{
		Module module;
		module.path = path("path");
		module.binary = path("binary");
		reply["fingerprint"] = QString::fromLatin1(fingerprint(module, {}).toHex());
	}
	else if (op == "pe")
	{
		const auto file = path("path");
		Architecture architecture{};
		reply["exists"] = QFileInfo(file).isFile();
		reply["valid"] = peArchitecture(file, architecture, request.value("dll").toBool());
		reply["architecture"] = static_cast<int>(architecture);
	}
	else if (op == "read-document")
	{
		QFile file(path("path"));
		reply["exists"] = file.exists();
		if (file.exists())
		{
			if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024)
			{
				throw std::runtime_error("Cannot read bounded catalog cache");
			}
			QJsonParseError error;
			const auto document = QJsonDocument::fromJson(file.readAll(), &error);
			reply["invalid"] = error.error != QJsonParseError::NoError || !document.isObject();
			reply["document"] = document.object();
		}
	}
	else if (op == "publish")
	{
		const auto file = path("path");
		if (!request.value("success").isObject() || !request.value("failures").isObject())
		{
			throw std::invalid_argument("Invalid cache publication");
		}
		QString error;
		if (!QDir().mkpath(QFileInfo(file).absolutePath()))
		{
			error = "Cannot create catalog cache directory";
		}
		QLockFile lock(file + ".lock");
		if (error.isEmpty() && !lock.tryLock(0))
		{
			error = "Catalog cache writer is busy";
		}
		auto save = [&](const QString& target, const QJsonObject& document) {
			if (!error.isEmpty())
			{
				return;
			}
			const auto bytes = QJsonDocument(document).toJson(QJsonDocument::Compact);
			if (bytes.size() > 64 * 1024 * 1024)
			{
				error = "Catalog cache exceeds size limit";
				return;
			}
			QSaveFile output(target);
			output.setDirectWriteFallback(false);
			if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
			{
				error = "Cannot atomically save catalog cache: " + output.errorString();
			}
		};
		save(file, request.value("success").toObject());
		save(file + ".failures.json", request.value("failures").toObject());
		reply["error"] = error;
	}
	else
	{
		throw std::invalid_argument("Unknown filesystem operation");
	}
	return reply;
}
} // namespace lmms::vsthost
