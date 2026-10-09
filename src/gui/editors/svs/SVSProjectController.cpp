#include "SVSProjectController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <cmath>
#include <exception>
#include <stdexcept>

#include "ConfigManager.h"
#include "DataFile.h"
#include "Engine.h"
#include "FileDialog.h"
#include "MainWindow.h"
#include "SVSModel.h"
#include "SVSProjectExport.h"
#include "SVSProjectImportDialog.h"
#include "SVSProjectOutput.h"
#include "SampleDecoder.h"
#include "Song.h"
#include "Track.h"

namespace lmms::gui {
struct SVSProjectController::ExportState
{
	svs::ProjectExportData data;
	std::unique_ptr<QTemporaryDir> directory;
	QString destination;
	QJsonObject request;
};
namespace {
void copyElement(DataFile& file, QDomElement destination, const QDomElement& source)
{
	const auto attributes = source.attributes();
	for (int i = 0; i < attributes.size(); ++i)
		destination.setAttribute(attributes.item(i).nodeName(), attributes.item(i).nodeValue());
	for (auto child = source.firstChild(); !child.isNull(); child = child.nextSibling())
		destination.appendChild(file.importNode(child, true));
}
}
SVSProjectController::SVSProjectController(MainWindow* window, QString runtimeDirectory)
	: QObject(window)
	, m_window(window)
	, m_bridge(this, std::move(runtimeDirectory))
{
	connect(
		&m_bridge, &svs::ProjectBridge::finished, this, [this](const QJsonObject& response) { converted(response); });
}
SVSProjectController::~SVSProjectController()
{
	m_cancelled = true;
	if (m_worker)
	{
		m_worker->disconnect(this);
		m_worker->wait();
		delete m_worker;
		m_worker = nullptr;
	}
	if (!m_resourceDirectory.isEmpty())
		QDir(m_resourceDirectory).removeRecursively();
}
void SVSProjectController::progress(const QString& label)
{
	if (!m_progress)
	{
		m_progress = new QProgressDialog(label, QCoreApplication::translate("SVSProjectUI", "Cancel"), 0, 0, m_window);
		m_progress->setObjectName("svsProjectProgress");
		m_progress->setWindowTitle(QCoreApplication::translate("SVSProjectUI", "SVS project"));
		m_progress->setWindowModality(Qt::WindowModal);
		m_progress->setMinimumDuration(0);
		m_progress->setAutoClose(false);
		connect(m_progress, &QProgressDialog::canceled, this, [this] {
			m_cancelled = true;
			m_bridge.cancel();
			if (m_progress)
				m_progress->setLabelText(QCoreApplication::translate("SVSProjectUI", "Cancelling and cleaning up…"));
		});
	}
	else
		m_progress->setLabelText(label);
	m_progress->show();
}
void SVSProjectController::reset()
{
	if (m_progress)
	{
		m_progress->hide();
		m_progress->deleteLater();
		m_progress = nullptr;
	}
	m_bridge.releaseTask();
	if (!m_resourceDirectory.isEmpty())
	{
		QDir(m_resourceDirectory).removeRecursively();
		m_resourceDirectory.clear();
	}
	m_task = Task::Idle;
	m_cancelled = false;
	m_export.reset();
}
void SVSProjectController::importProject()
{
	if (m_task != Task::Idle)
		return;
	m_cancelled = false;
	m_task = Task::Catalog;
	progress(QCoreApplication::translate("SVSProjectUI", "Reading supported formats…"));
	m_bridge.start({{"operation", "listFormats"}});
}
void SVSProjectController::converted(const QJsonObject& response)
{
	if (m_cancelled || response["status"].toString() == "cancelled")
	{
		reset();
		return;
	}
	if (response["status"].toString() != "success")
	{
		const auto error = response["error"].toObject();
		if (m_progress)
			m_progress->hide();
		QMessageBox::critical(m_window, QCoreApplication::translate("SVSProjectUI", "SVS project conversion failed"),
			error["message"].toString() + "\n" + error["detail"].toString());
		reset();
		return;
	}
	if (m_task == Task::Catalog)
	{
		if (m_progress)
			m_progress->hide();
		m_bridge.releaseTask();
		chooseSource(response["formats"].toArray());
	}
	else if (m_task == Task::Import)
		prepareAudio(response);
	else if (m_task == Task::ExportCatalog)
	{
		if (m_progress)
			m_progress->hide();
		m_bridge.releaseTask();
		chooseExport(response["formats"].toArray());
	}
	else if (m_task == Task::ExportInspection)
		inspectExport(response);
	else if (m_task == Task::ExportConversion)
		finishExport(response);
}
void SVSProjectController::chooseSource(const QJsonArray& formats)
{
	FileDialog file(m_window, QCoreApplication::translate("SVSProjectUI", "Import SVS project"),
		ConfigManager::inst()->userProjectsDir());
	file.setFileMode(QFileDialog::ExistingFile);
	file.setAcceptMode(QFileDialog::AcceptOpen);
	QStringList filters;
	QMap<QString, QJsonObject> identities;
	QStringList supportedSuffixes;
	QMap<QString, QJsonObject> suffixIdentities;
	for (const auto& entry : formats)
	{
		const auto format = entry.toObject();
		if (!format["canImport"].toBool())
			continue;
		QStringList suffixes;
		for (const auto& suffix : format["suffixes"].toArray())
		{
			suffixes << "*." + suffix.toString();
			suffixIdentities[suffix.toString().toLower()] = format;
		}
		supportedSuffixes << suffixes;
		const auto filter = QStringLiteral("%1 [%2] (%3)")
								.arg(format["name"].toString(), format["id"].toString(), suffixes.join(' '));
		filters << filter;
		identities[filter] = format;
	}
	supportedSuffixes.removeDuplicates();
	const auto supportedFilter
		= QCoreApplication::translate("SVSProjectUI", "All supported formats (%1)").arg(supportedSuffixes.join(' '));
	filters.prepend(supportedFilter);
	file.setNameFilters(filters);
	file.selectNameFilter(supportedFilter);
	if (file.exec() != QDialog::Accepted || file.selectedFiles().isEmpty())
	{
		reset();
		return;
	}
	const auto format = file.selectedNameFilter() == supportedFilter
		? suffixIdentities.value(QFileInfo(file.selectedFiles().first()).suffix().toLower())
		: identities.value(file.selectedNameFilter());
	if (format.isEmpty())
	{
		reset();
		return;
	}
	SVSProjectImportDialog dialog(format, m_window);
	if (dialog.exec() != QDialog::Accepted)
	{
		reset();
		return;
	}
	m_voice = {};
	m_formatId = format["id"].toString();
	m_task = Task::Import;
	progress(QCoreApplication::translate("SVSProjectUI", "Parsing project: %1")
			.arg(QFileInfo(file.selectedFiles().first()).fileName()));
	m_bridge.start({{"operation", "importProject"}, {"formatId", m_formatId},
		{"path", QFileInfo(file.selectedFiles().first()).absoluteFilePath()}, {"options", dialog.options()}});
}
void SVSProjectController::prepareAudio(const QJsonObject& response)
{
	const auto project = response["project"].toObject();
	const auto initial = svs::ProjectMapper::prepareImport(project, m_voice, {});
	if (!initial.valid())
	{
		if (m_progress)
			m_progress->hide();
		QMessageBox::critical(
			m_window, QCoreApplication::translate("SVSProjectUI", "SVS project preflight failed"), initial.error);
		reset();
		return;
	}
	m_task = Task::Preparation;
	progress(QCoreApplication::translate("SVSProjectUI", "Validating audio and project data…"));
	// The UUID directory is the only durable location created/removed by this task.
	m_resourceDirectory = QDir(ConfigManager::inst()->userSamplesDir())
							  .filePath("svs-project/" + QUuid::createUuid().toString(QUuid::WithoutBraces));
	struct Preparation
	{
		svs::ProjectImport imported;
		QStringList warnings;
	};
	auto prepared = std::make_shared<Preparation>();
	for (const auto& item : response["warnings"].toArray())
		prepared->warnings << item.toObject()["message"].toString();
	for (const auto& item : response["losses"].toArray())
	{
		const auto loss = item.toObject();
		prepared->warnings << QCoreApplication::translate("SVSProjectUI", "Format %1, track %2, %3: %4")
								  .arg(loss["formatId"].toString(), loss["track"].toString(), loss["field"].toString(),
									  loss["reason"].toString());
	}
	const auto directory = m_resourceDirectory;
	const auto voice = m_voice;
	m_worker = QThread::create([this, project, directory, voice, prepared] {
		try
		{
			QMap<QString, svs::ProjectAudio> resources;
			int number = 0;
			for (const auto& entry : project["track_list"].toArray())
			{
				if (m_cancelled)
					return;
				const auto track = entry.toObject();
				if (track["type_"].toString() != "Instrumental")
					continue;
				const auto path = track["audio_file_path"].toString();
				if (resources.contains(path))
					continue;
				const QFileInfo source(path);
				if (!source.isFile() || source.size() > 256ll * 1024 * 1024)
					continue;
				if (!QDir().mkpath(directory))
				{
					prepared->imported.error = QCoreApplication::translate(
						"SVSProjectUI", "Could not create the imported audio resource directory: %1")
												   .arg(directory);
					return;
				}
				const auto target = QDir(directory).filePath(QString::number(++number) + "-" + source.fileName());
				if (!QFile::copy(path, target))
				{
					prepared->imported.error
						= QCoreApplication::translate("SVSProjectUI", "Could not save the imported audio resource: %1")
							  .arg(path);
					return;
				}
				const auto decoded = SampleDecoder::decode(target);
				if (m_cancelled)
					return;
				if (!decoded || decoded->data.empty() || decoded->sampleRate <= 0)
				{
					QFile::remove(target);
					continue;
				}
				resources[path] = {target, 0, double(decoded->data.size()) / decoded->sampleRate};
			}
			if (!m_cancelled)
				prepared->imported = svs::ProjectMapper::prepareImport(project, voice, resources);
		}
		catch (const std::exception& error)
		{
			prepared->imported.error = QString::fromUtf8(error.what());
		}
		catch (...)
		{
			prepared->imported.error
				= QCoreApplication::translate("SVSProjectUI", "An unknown error occurred during project preflight");
		}
	});
	connect(m_worker, &QThread::finished, this, [this, prepared] {
		auto* worker = m_worker;
		m_worker = nullptr;
		worker->deleteLater();
		if (m_cancelled)
		{
			reset();
			return;
		}
		finishImport(prepared->imported, prepared->warnings);
	});
	m_worker->start();
}
void SVSProjectController::finishImport(const svs::ProjectImport& prepared, const QStringList& warnings)
{
	if (m_progress)
		m_progress->hide();
	if (!prepared.valid())
	{
		QMessageBox::critical(
			m_window, QCoreApplication::translate("SVSProjectUI", "SVS project preflight failed"), prepared.error);
		reset();
		return;
	}
	const auto issues = warnings + prepared.losses;
	if (!issues.isEmpty()
		&& QMessageBox::warning(m_window, QCoreApplication::translate("SVSProjectUI", "Confirm lossy import"),
			   QCoreApplication::translate("SVSProjectUI",
				   "Confirm the following content for format %1:\n%2\n\nContinue importing as a new project?")
				   .arg(m_formatId, issues.join('\n')),
			   QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
			!= QMessageBox::Yes)
	{
		reset();
		return;
	}
	if (!m_window->mayChangeProject(false))
	{
		reset();
		return;
	}
	// A registry refresh may have removed the voice while conversion was running.
	bool present = m_voice.pluginId.isEmpty() && m_voice.voiceId.isEmpty();
	for (const auto& voice : svs::Registry::instance().voices())
		if (voice.pluginId == m_voice.pluginId && voice.id == m_voice.voiceId)
			present = true;
	if (!present)
	{
		QMessageBox::critical(m_window, QCoreApplication::translate("SVSProjectUI", "Voicebank unavailable"),
			QCoreApplication::translate(
				"SVSProjectUI", "The selected voicebank was removed. The original project has been preserved."));
		reset();
		return;
	}
	QString error;
	if (!commitImport(prepared, *Engine::getSong(), error))
	{
		QMessageBox::critical(
			m_window, QCoreApplication::translate("SVSProjectUI", "Could not commit the import"), error);
		reset();
		return;
	}
	m_resourceDirectory.clear();
	reset();
}
void SVSProjectController::exportProject()
{
	if (busy())
		return;
	const auto snapshot = svs::ProjectExport::capture(*Engine::getSong());
	if (!snapshot.valid())
	{
		QMessageBox::critical(
			m_window, QCoreApplication::translate("SVSProjectUI", "SVS project export failed"), snapshot.error);
		return;
	}
	m_cancelled = false;
	m_task = Task::ExportPreparation;
	m_export = std::make_shared<ExportState>();
	const auto state = m_export;
	state->directory = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/lmms-svs-export-audio-XXXXXX");
	if (!state->directory->isValid())
	{
		QMessageBox::critical(m_window, QCoreApplication::translate("SVSProjectUI", "SVS project export failed"),
			QCoreApplication::translate("SVSProjectUI", "Could not create the temporary export audio directory."));
		reset();
		return;
	}
	progress(QCoreApplication::translate("SVSProjectUI", "Preparing the project snapshot and accompaniment…"));
	m_worker = QThread::create([this, snapshot, state] {
		try
		{
			state->data = svs::ProjectExport::build(snapshot, &m_cancelled);
			if (state->data.valid() && !m_cancelled)
				svs::ProjectExport::writeAudioFiles(
					state->data, state->directory->path(), state->data.error, &m_cancelled);
		}
		catch (const std::exception& error)
		{
			state->data.error = QString::fromUtf8(error.what());
		}
		catch (...)
		{
			state->data.error = QCoreApplication::translate(
				"SVSProjectUI", "An unknown error occurred while preparing the export snapshot");
		}
	});
	connect(m_worker, &QThread::finished, this, [this, state] {
		auto* worker = m_worker;
		m_worker = nullptr;
		worker->deleteLater();
		if (m_cancelled)
		{
			reset();
			return;
		}
		if (!state->data.valid())
		{
			if (m_progress)
				m_progress->hide();
			QMessageBox::critical(
				m_window, QCoreApplication::translate("SVSProjectUI", "SVS project export failed"), state->data.error);
			reset();
			return;
		}
		m_task = Task::ExportCatalog;
		progress(QCoreApplication::translate("SVSProjectUI", "Reading output formats…"));
		m_bridge.start({{"operation", "listFormats"}});
	});
	m_worker->start();
}
void SVSProjectController::chooseExport(const QJsonArray& formats)
{
	FileDialog file(m_window, QCoreApplication::translate("SVSProjectUI", "Export SVS project"),
		ConfigManager::inst()->userProjectsDir());
	file.setFileMode(QFileDialog::AnyFile);
	file.setAcceptMode(QFileDialog::AcceptSave);
	file.setOption(QFileDialog::DontConfirmOverwrite, true);
	QStringList filters;
	QMap<QString, QJsonObject> identities;
	QStringList supportedSuffixes;
	QMap<QString, QJsonObject> suffixIdentities;
	QString defaultFilter;
	for (const auto& entry : formats)
	{
		const auto format = entry.toObject();
		if (!format["canExport"].toBool())
			continue;
		QStringList suffixes;
		for (const auto& suffix : format["suffixes"].toArray())
		{
			suffixes << "*." + suffix.toString();
			suffixIdentities[suffix.toString().toLower()] = format;
		}
		supportedSuffixes << suffixes;
		const auto filter = QStringLiteral("%1 [%2] (%3)")
								.arg(format["name"].toString(), format["id"].toString(), suffixes.join(' '));
		filters << filter;
		identities[filter] = format;
		if (format["id"].toString() == "json")
			defaultFilter = filter;
	}
	supportedSuffixes.removeDuplicates();
	const auto supportedFilter
		= QCoreApplication::translate("SVSProjectUI", "All supported formats (%1)").arg(supportedSuffixes.join(' '));
	filters.prepend(supportedFilter);
	file.setNameFilters(filters);
	file.selectNameFilter(supportedFilter);
	const auto defaultSuffix = identities[defaultFilter]["suffixes"].toArray().first().toString();
	file.setDefaultSuffix(defaultSuffix);
	file.selectFile(QCoreApplication::translate("SVSProjectUI", "SVS project"));
	connect(&file, &QFileDialog::filterSelected, &file, [&file, &identities, &supportedFilter, &defaultSuffix](const QString& filter) {
		file.setDefaultSuffix(filter == supportedFilter
			? defaultSuffix : identities[filter]["suffixes"].toArray().first().toString());
	});
	if (file.exec() != QDialog::Accepted || file.selectedFiles().isEmpty())
	{
		reset();
		return;
	}
	const auto format = file.selectedNameFilter() == supportedFilter
		? suffixIdentities.value(QFileInfo(file.selectedFiles().first()).suffix().toLower())
		: identities.value(file.selectedNameFilter());
	if (format.isEmpty())
	{
		reset();
		return;
	}
	const auto destination = QFileInfo(file.selectedFiles().first()).absoluteFilePath();
	QStringList suffixes;
	for (const auto& suffix : format["suffixes"].toArray())
		suffixes << suffix.toString();
	if (!suffixes.contains(QFileInfo(destination).suffix(), Qt::CaseInsensitive))
	{
		QMessageBox::critical(m_window, QCoreApplication::translate("SVSProjectUI", "Output extension mismatch"),
			QCoreApplication::translate("SVSProjectUI", "Format %1 supports: %2")
				.arg(format["id"].toString(), suffixes.join(", ")));
		reset();
		return;
	}
	SVSProjectExportDialog dialog(format, m_export->data.project, m_window);
	if (dialog.exec() != QDialog::Accepted)
	{
		reset();
		return;
	}
	QJsonArray assets;
	for (const auto& audio : m_export->data.audioFiles)
		assets.append(QJsonObject{
			{"name", audio.fileName}, {"source", QDir(m_export->directory->path()).filePath(audio.fileName)}});
	m_export->destination = destination;
	m_formatId = format["id"].toString();
	m_export->request = {{"operation", "inspectExport"}, {"formatId", m_formatId}, {"path", destination},
		{"project", m_export->data.project}, {"options", dialog.options()}, {"selection", dialog.selection()},
		{"assets", assets}};
	m_task = Task::ExportInspection;
	progress(QCoreApplication::translate("SVSProjectUI", "Checking format restrictions and omitted content…"));
	m_bridge.start(m_export->request);
}
void SVSProjectController::inspectExport(const QJsonObject& response)
{
	if (m_progress)
		m_progress->hide();
	QStringList issues = m_export->data.losses;
	for (const auto& entry : response["losses"].toArray())
	{
		const auto loss = entry.toObject();
		issues << QCoreApplication::translate("SVSProjectUI", "Format %1, track %2, %3: %4")
					  .arg(m_formatId, loss["track"].toString(), loss["field"].toString(), loss["reason"].toString());
	}
	if (!issues.isEmpty()
		&& QMessageBox::warning(m_window, QCoreApplication::translate("SVSProjectUI", "Confirm lossy export"),
			   issues.join('\n') + QCoreApplication::translate("SVSProjectUI", "\n\nContinue exporting?"),
			   QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
			!= QMessageBox::Yes)
	{
		reset();
		return;
	}
	m_bridge.releaseTask();
	auto request = m_export->request;
	request["operation"] = "exportProject";
	request["acceptLosses"] = true;
	// Do not publish audio companions for tracks omitted by explicit projection.
	QSet<QString> used;
	for (const auto& entry : response["project"].toObject()["track_list"].toArray())
	{
		const auto track = entry.toObject();
		if (track["type_"].toString() == "Instrumental")
			used.insert(track["audio_file_path"].toString());
	}
	QJsonArray assets;
	for (const auto& entry : request["assets"].toArray())
		if (used.contains(entry.toObject()["name"].toString()))
			assets.append(entry);
	request["assets"] = assets;
	m_task = Task::ExportConversion;
	progress(QCoreApplication::translate("SVSProjectUI", "Converting project…"));
	m_bridge.start(request);
}
void SVSProjectController::finishExport(const QJsonObject& response)
{
	if (m_progress)
		m_progress->hide();
	QStringList warnings;
	for (const auto& entry : response["warnings"].toArray())
		warnings << entry.toObject()["message"].toString();
	if (!warnings.isEmpty()
		&& QMessageBox::warning(m_window,
			   QCoreApplication::translate("SVSProjectUI", "Confirm format conversion warnings"),
			   warnings.join('\n') + QCoreApplication::translate("SVSProjectUI", "\n\nContinue saving the file group?"),
			   QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
			!= QMessageBox::Yes)
	{
		reset();
		return;
	}
	QStringList files;
	for (const auto& entry : response["files"].toArray())
		files << entry.toString();
	const auto directory = QFileInfo(m_export->destination).absolutePath();
	const auto staging = QDir(m_bridge.taskDirectory()).filePath("output");
	const auto plan = svs::ProjectOutput::prepare(files, staging, directory);
	if (!plan.valid())
	{
		QMessageBox::critical(
			m_window, QCoreApplication::translate("SVSProjectUI", "Output file group preflight failed"), plan.error);
		reset();
		return;
	}
	if (!plan.overwrites.isEmpty()
		&& QMessageBox::warning(m_window,
			   QCoreApplication::translate("SVSProjectUI", "Confirm overwriting the export file group"),
			   QCoreApplication::translate(
				   "SVSProjectUI", "All of the following files will be overwritten:\n%1\n\nContinue?")
				   .arg(plan.overwrites.join('\n')),
			   QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
			!= QMessageBox::Yes)
	{
		reset();
		return;
	}
	m_task = Task::ExportCommit;
	progress(QCoreApplication::translate("SVSProjectUI", "Saving the complete file group…"));
	struct Result
	{
		bool success = false;
		QString error;
	};
	const auto result = std::make_shared<Result>();
	m_worker = QThread::create([this, plan, result] {
		try
		{
			result->success = svs::ProjectOutput::commit(plan, result->error, &m_cancelled);
		}
		catch (const std::exception& error)
		{
			result->error = QString::fromUtf8(error.what());
		}
		catch (...)
		{
			result->error = QCoreApplication::translate(
				"SVSProjectUI", "An unknown error occurred while saving the export file group");
		}
	});
	connect(m_worker, &QThread::finished, this, [this, plan, result] {
		auto* worker = m_worker;
		m_worker = nullptr;
		worker->deleteLater();
		if (m_progress)
			m_progress->hide();
		if (result->success)
		{
			QStringList paths;
			for (const auto& file : plan.files)
				paths << file.target;
			QMessageBox::information(
				m_window, QCoreApplication::translate("SVSProjectUI", "SVS project export complete"), paths.join('\n'));
		}
		else if (!m_cancelled || !result->error.isEmpty())
			QMessageBox::critical(m_window,
				QCoreApplication::translate("SVSProjectUI", "Could not save the output file group"), result->error);
		reset();
	});
	m_worker->start();
}
bool SVSProjectController::commitImport(const svs::ProjectImport& prepared, Song& song, QString& error)
{
	static_assert(int(Track::Type::SVS) == 7 && int(Track::Type::Sample) == 2);
	if (!prepared.valid())
	{
		error = prepared.error;
		return false;
	}
	DataFile native(DataFile::Type::JournalData);
	const auto root = prepared.document.documentElement();
	copyElement(native, native.head(), root.firstChildElement("head"));
	copyElement(native, native.content(), root.firstChildElement("song"));
	DataFile backup(DataFile::Type::JournalData);
	song.saveProjectState(backup);
	const bool modified = song.isModified();
	try
	{
		song.stop();
		song.restoreProjectState(native);
		const auto expected
			= root.firstChildElement("song").firstChildElement("trackcontainer").elementsByTagName("track").size();
		if (song.hasErrors() || int(song.tracks().size()) != expected)
			throw std::runtime_error(song.errorSummary().isEmpty()
					? QCoreApplication::translate("SVSProjectUI", "Incomplete project restore").toStdString()
					: song.errorSummary().toStdString());
		song.setModified(true);
		return true;
	}
	catch (const std::exception& failure)
	{
		error = QString::fromUtf8(failure.what());
	}
	catch (...)
	{
		error = QCoreApplication::translate("SVSProjectUI", "An unknown error occurred while committing the project");
	}
	song.restoreProjectState(backup);
	song.setModified(modified);
	return false;
}
}
