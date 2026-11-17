#include "agent/chat/ChatDownloadTask.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>

#include "Engine.h"
#include "Song.h"
#include "agent/CommandBus.h"

namespace lmms::agent::chat {
namespace {

QString message(const char* source)
{ return QCoreApplication::translate("AgentChat", source); }

QJsonObject failure(const QString& code, const QString& text)
{ return CommandResult::failure(code, text).toJson(); }

QString sha256(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return {}; }
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(&file);
	return QString::fromLatin1(hash.result().toHex());
}

// A plain file name: no directory parts, no reserved Windows characters.
bool validFileName(const QString& name)
{
	static const QRegularExpression reserved(R"([<>:"/\\|?*\x00-\x1f])");
	return !name.isEmpty() && name != "." && name != ".." && !name.contains(reserved) && !name.endsWith('.')
		&& !name.endsWith(' ');
}

} // namespace

ChatDownloadTask::ChatDownloadTask(QObject* parent)
	: QObject(parent)
{
}

ChatDownloadTask::~ChatDownloadTask()
{ abort(); }

QString ChatDownloadTask::command()
{ return QStringLiteral("chat.downloadAsset"); }

QJsonObject ChatDownloadTask::definition()
{
	const QJsonObject properties{
		{"url", QJsonObject{{"type", "string"}, {"description", "http or https address of the asset."}}},
		{"directory",
			QJsonObject{{"type", "string"},
				{"description",
					"Target folder: absolute, or relative to the saved project's folder (for example assets/drums)."}}},
		{"fileName", QJsonObject{{"type", "string"}, {"description", "File name; defaults to the URL's name."}}},
		{"overwrite",
			QJsonObject{{"type", "boolean"}, {"description", "Replace an existing file with the same name."}}}};
	return {{"name", command()},
		{"description",
			"Download one audio, MIDI or other asset file the user asked for into a folder. "
			"Downloaded files are never executed."},
		{"inputSchema",
			QJsonObject{{"type", "object"}, {"properties", properties}, {"required", QJsonArray{"url", "directory"}},
				{"additionalProperties", false}}},
		{"annotations", QJsonObject{{"readOnlyHint", false}, {"openWorldHint", true}}}};
}

bool ChatDownloadTask::plan(const QJsonObject& arguments, QJsonObject& plan, QString& error)
{
	plan = {};
	const QUrl url(arguments.value("url").toString());
	if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "http" && url.scheme() != "https"))
	{
		error = message("Only http and https assets can be downloaded.");
		return false;
	}
	auto directory = arguments.value("directory").toString().trimmed();
	if (directory.isEmpty())
	{
		error = message("A target folder is required.");
		return false;
	}
	if (QDir::isRelativePath(directory))
	{
		const auto* song = Engine::getSong();
		const auto project = song ? song->projectFileName() : QString{};
		if (project.isEmpty())
		{
			error = message("Save the project first, or give an absolute target folder.");
			return false;
		}
		directory = QFileInfo(project).absoluteDir().absoluteFilePath(directory);
	}
	directory = QDir::cleanPath(QDir(directory).absolutePath());
	auto fileName = arguments.value("fileName").toString().trimmed();
	if (fileName.isEmpty()) { fileName = QFileInfo(url.path()).fileName(); }
	if (!validFileName(fileName))
	{
		error = message("The file name is missing or not a plain file name.");
		return false;
	}
	plan = {{"url", url.toString()}, {"directory", directory}, {"fileName", fileName},
		{"target", QDir(directory).absoluteFilePath(fileName)}, {"overwrite", arguments.value("overwrite").toBool()}};
	return true;
}

void ChatDownloadTask::start(const QJsonObject& plan, const QString& aria2Path)
{
	abort();
	m_plan = plan;
	m_output.clear();
	m_lastProgress = {};
	const auto target = plan.value("target").toString();
	if (QFileInfo::exists(target) && !plan.value("overwrite").toBool())
	{
		// Checked before downloading so nothing is fetched for a refused name.
		emit finished(failure("file_exists",
			message("%1 already exists; ask again with overwrite to replace it.")
				.arg(QDir::toNativeSeparators(target))));
		return;
	}
	const QDir directory(plan.value("directory").toString());
	m_temporary = directory.absoluteFilePath(".lmms-download-" + QUuid::createUuid().toString(QUuid::Id128));
	if (!QDir().mkpath(m_temporary))
	{
		emit finished(failure("target_unwritable",
			message("Cannot create the download folder in %1.").arg(QDir::toNativeSeparators(directory.path()))));
		m_temporary.clear();
		return;
	}

	auto* process = new QProcess(this);
	m_process = process;
	process->setProcessChannelMode(QProcess::MergedChannels);
	// Arguments are passed as a list; nothing is interpreted by a shell.
	const QStringList arguments{"--dir=" + QDir::toNativeSeparators(m_temporary),
		"--out=" + plan.value("fileName").toString(), "--allow-overwrite=false", "--auto-file-renaming=false",
		"--file-allocation=none", "--follow-torrent=false", "--follow-metalink=false", "--enable-rpc=false",
		"--max-tries=3", "--connect-timeout=15", "--summary-interval=1", "--console-log-level=warn",
		plan.value("url").toString()};
	connect(process, &QProcess::readyRead, this, &ChatDownloadTask::readOutput);
	connect(process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
		complete(status == QProcess::NormalExit ? exitCode : -1);
	});
	connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart && m_process)
		{
			// No finished() follows a failed start: release the process here.
			m_process->deleteLater();
			m_process = nullptr;
			cleanup();
			emit finished(failure("tool_missing", message("aria2 could not be started.")));
		}
	});
	process->start(aria2Path, arguments);
}

void ChatDownloadTask::readOutput()
{
	if (!m_process) { return; }
	const auto chunk = m_process->readAll();
	m_output += chunk;
	// Console readout: [#id 1.2MiB/12MiB(10%) CN:1 DL:3.4MiB ETA:3s]
	static const QRegularExpression readout(
		R"(\[#\w+\s+([\d.]+\w*)/([\d.]+\w*)(?:\((\d+)%\))?.*?(?:DL:([\d.]+\w*))?(?:\s+ETA:(\w+))?\])");
	auto matches = readout.globalMatch(QString::fromUtf8(chunk));
	while (matches.hasNext())
	{
		const auto match = matches.next();
		// An unknown total is reported without a percentage, never as 0% or 100%.
		m_lastProgress = {{"received", match.captured(1)}, {"total", match.captured(2)},
			{"percent", match.captured(3).isEmpty() ? QJsonValue() : QJsonValue(match.captured(3).toInt())},
			{"speed", match.captured(4)}, {"eta", match.captured(5)}};
		emit progress(m_lastProgress);
	}
}

void ChatDownloadTask::complete(int exitCode)
{
	if (!m_process) { return; }
	readOutput();
	m_process->deleteLater();
	m_process = nullptr;
	const auto downloaded = QDir(m_temporary).absoluteFilePath(m_plan.value("fileName").toString());
	if (exitCode != 0 || !QFileInfo(downloaded).isFile())
	{
		const auto lines = QString::fromUtf8(m_output).split('\n', Qt::SkipEmptyParts);
		QStringList tail;
		for (const auto& line : lines)
		{
			if (line.contains("ERROR") || line.contains("errorCode") || line.contains("status="))
			{
				tail.append(line.trimmed());
			}
		}
		cleanup();
		emit finished(failure(
			"download_failed", message("aria2 exited with code %1. %2").arg(exitCode).arg(tail.join(' ').left(400))));
		return;
	}

	// Check the finished file, then move it into the approved target.
	const auto target = m_plan.value("target").toString();
	const bool overwrite = m_plan.value("overwrite").toBool();
	const bool existed = QFileInfo::exists(target);
	if (existed && (!overwrite || !QFile::remove(target)))
	{
		cleanup();
		emit finished(failure("file_exists",
			message("%1 already exists; ask again with overwrite to replace it.")
				.arg(QDir::toNativeSeparators(target))));
		return;
	}
	const auto size = QFileInfo(downloaded).size();
	const auto hash = sha256(downloaded);
	if (!QFile::rename(downloaded, target))
	{
		cleanup();
		emit finished(failure(
			"target_unwritable", message("Cannot move the download to %1.").arg(QDir::toNativeSeparators(target))));
		return;
	}
	cleanup();
	emit finished(
		CommandResult::success(QJsonObject{{"url", m_plan.value("url")}, {"path", QDir::toNativeSeparators(target)},
								   {"size", size}, {"sha256", hash}, {"overwritten", existed}})
			.toJson());
}

void ChatDownloadTask::abort()
{
	if (m_process)
	{
		auto* process = m_process.data();
		m_process = nullptr;
		process->disconnect(this);
		process->kill();
		process->waitForFinished(3000);
		process->deleteLater();
	}
	cleanup();
}

bool ChatDownloadTask::active() const
{ return !m_process.isNull(); }

void ChatDownloadTask::cleanup()
{
	if (!m_temporary.isEmpty())
	{
		QDir(m_temporary).removeRecursively();
		m_temporary.clear();
	}
}

} // namespace lmms::agent::chat
