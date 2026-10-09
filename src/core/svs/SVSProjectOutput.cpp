#include "SVSProjectOutput.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <algorithm>

namespace lmms::svs {
namespace {
bool contained(const QString& path, const QString& root)
{
	return path.startsWith(root.endsWith('/') ? root : root + "/", Qt::CaseInsensitive);
}
bool parentSafe(const QString& path, const QString& root)
{
	QString parent = QFileInfo(path).absolutePath();
	while (parent != root)
	{
		const QFileInfo info(parent);
		if (info.isSymLink() || (info.exists() && (!info.isDir() || !contained(info.canonicalFilePath(), root))))
			return false;
		const auto next = info.absolutePath();
		if (next == parent)
			return false;
		parent = next;
	}
	return true;
}
}
ProjectOutputPlan ProjectOutput::prepare(
	const QStringList& paths, const QString& stagingRoot, const QString& destinationDirectory)
{
	ProjectOutputPlan result;
	const auto root = QFileInfo(stagingRoot).canonicalFilePath();
	result.directory = QFileInfo(destinationDirectory).canonicalFilePath();
	auto fail = [&](const QString& error) {
		result.error = error;
		result.files.clear();
		return result;
	};
	if (root.isEmpty() || result.directory.isEmpty() || !QFileInfo(root).isDir()
		|| !QFileInfo(result.directory).isDir())
		return fail(QCoreApplication::translate("NativeSVS", "Export staging or destination directory does not exist"));
	if (paths.isEmpty())
		return fail(QCoreApplication::translate("NativeSVS", "The converter produced no output files"));
	QSet<QString> targets;
	for (const auto& path : paths)
	{
		const QFileInfo source(path);
		const auto canonical = source.canonicalFilePath();
		if (canonical.isEmpty() || !source.isFile() || source.isSymLink() || !contained(canonical, root)
			|| source.size() == 0)
			return fail(QCoreApplication::translate(
				"NativeSVS", "Converter output is invalid or outside the staging directory: %1")
					.arg(path));
		const auto relative = QDir(root).relativeFilePath(canonical);
		const auto target = QDir::cleanPath(QDir(result.directory).filePath(relative));
		if (!contained(target, result.directory) || !parentSafe(target, result.directory)
			|| targets.contains(target.toCaseFolded()))
			return fail(QCoreApplication::translate(
				"NativeSVS", "Export destination paths conflict or escape the destination directory: %1")
					.arg(target));
		const QFileInfo existing(target);
		if (existing.isSymLink() || (existing.exists() && !existing.isFile()))
			return fail(
				QCoreApplication::translate("NativeSVS", "Export destination is not a replaceable regular file: %1")
					.arg(target));
		targets.insert(target.toCaseFolded());
		result.files.append({canonical, target, relative});
		if (existing.exists())
			result.overwrites << target;
	}
	std::sort(
		result.files.begin(), result.files.end(), [](const auto& a, const auto& b) { return a.relative < b.relative; });
	return result;
}
bool ProjectOutput::commit(const ProjectOutputPlan& plan, QString& error, const std::atomic<bool>* cancelled)
{
	if (!plan.valid())
	{
		error = plan.error;
		return false;
	}
	for (const auto& file : plan.files)
	{
		const auto target = QDir::cleanPath(QDir(plan.directory).filePath(file.relative));
		if (QDir::isAbsolutePath(file.relative) || file.relative == ".." || file.relative.startsWith("../")
			|| target != file.target || !contained(target, plan.directory))
		{
			error = QCoreApplication::translate("NativeSVS", "Export file group plan contains invalid paths");
			return false;
		}
	}
	auto stopped = [&] { return cancelled && cancelled->load(); };
	if (stopped())
	{
		error = QCoreApplication::translate("NativeSVS", "Cancelled");
		return false;
	}
	// Same-volume ready files allow individual replacement by rename. The old
	// complete group remains recoverable until every replacement has succeeded.
	QTemporaryDir transaction(QDir(plan.directory).filePath(".lmms-svs-export-XXXXXX"));
	if (!transaction.isValid())
	{
		error = QCoreApplication::translate(
			"NativeSVS", "Cannot create the export transaction directory on the destination volume");
		return false;
	}
	QVector<QString> ready, backups, createdDirectories;
	QVector<int> installed, backedUp;
	auto makeParents = [&](const QString& target) {
		auto parent = QFileInfo(target).absolutePath();
		QStringList missing;
		while (!QFileInfo::exists(parent))
		{
			missing.prepend(parent);
			const auto next = QFileInfo(parent).absolutePath();
			if (next == parent)
				return false;
			parent = next;
		}
		for (const auto& path : missing)
		{
			if (!QDir().mkdir(path))
				return false;
			createdDirectories.append(path);
		}
		return true;
	};
	auto rollback = [&] {
		QStringList failures;
		for (auto it = installed.crbegin(); it != installed.crend(); ++it)
			if (!QFile::remove(plan.files[*it].target))
				failures << plan.files[*it].target;
		for (auto it = backedUp.crbegin(); it != backedUp.crend(); ++it)
			if (!QFile::rename(backups[*it], plan.files[*it].target))
				failures << plan.files[*it].target;
		for (auto it = createdDirectories.crbegin(); it != createdDirectories.crend(); ++it)
			QDir().rmdir(*it);
		if (!failures.isEmpty())
		{
			transaction.setAutoRemove(false);
			error += QCoreApplication::translate(
				"NativeSVS", "\nRollback could not restore: %1\nOriginal file backups remain at: %2")
						 .arg(failures.join('\n'), transaction.path());
		}
		return false;
	};
	for (int index = 0; index < plan.files.size(); ++index)
	{
		const auto& file = plan.files[index];
		const auto copy = QDir(transaction.path()).filePath("ready/" + file.relative),
				   backup = QDir(transaction.path()).filePath("backup/" + file.relative);
		ready.append(copy);
		backups.append(backup);
		if (stopped())
		{
			error = QCoreApplication::translate("NativeSVS", "Cancelled");
			return rollback();
		}
		if (!QDir().mkpath(QFileInfo(copy).absolutePath()) || !QDir().mkpath(QFileInfo(backup).absolutePath())
			|| !QFile::copy(file.source, copy) || QFileInfo(copy).size() <= 0
			|| QFileInfo(copy).size() != QFileInfo(file.source).size())
		{
			error = QCoreApplication::translate("NativeSVS", "Cannot stage the complete export file group: %1")
						.arg(file.relative);
			return rollback();
		}
	}
	// Revalidate every target before moving any originals. Reject files which
	// appeared after the overwrite confirmation rather than silently replacing.
	for (const auto& file : plan.files)
	{
		const QFileInfo target(file.target);
		if (!parentSafe(file.target, plan.directory) || target.isSymLink()
			|| (target.exists() && (!target.isFile() || !plan.overwrites.contains(file.target))))
		{
			error = QCoreApplication::translate("NativeSVS", "Export destination changed after confirmation: %1")
						.arg(file.target);
			return rollback();
		}
	}
	for (int index = 0; index < plan.files.size(); ++index)
	{
		const auto& file = plan.files[index];
		if (stopped())
		{
			error = QCoreApplication::translate("NativeSVS", "Cancelled");
			return rollback();
		}
		if (QFileInfo::exists(file.target))
		{
			QFile original(file.target);
			if (!original.rename(backups[index]))
			{
				error = QCoreApplication::translate("NativeSVS", "Cannot back up the original file: %1\n%2")
							.arg(file.target, original.errorString());
				return rollback();
			}
			backedUp.append(index);
		}
	}
	for (int index = 0; index < plan.files.size(); ++index)
	{
		const auto& file = plan.files[index];
		if (stopped())
		{
			error = QCoreApplication::translate("NativeSVS", "Cancelled");
			return rollback();
		}
		if (!parentSafe(file.target, plan.directory) || !makeParents(file.target) || QFileInfo::exists(file.target)
			|| !QFile::rename(ready[index], file.target))
		{
			error = QCoreApplication::translate("NativeSVS", "Cannot commit the exported file: %1").arg(file.target);
			return rollback();
		}
		installed.append(index);
	}
	return true;
}
}
