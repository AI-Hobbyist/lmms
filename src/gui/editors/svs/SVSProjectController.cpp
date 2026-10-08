#include "SVSProjectController.h"
#include "SVSProjectImportDialog.h"
#include "SVSModel.h"
#include "ConfigManager.h"
#include "DataFile.h"
#include "Engine.h"
#include "MainWindow.h"
#include "FileDialog.h"
#include "SampleDecoder.h"
#include "Song.h"
#include "Track.h"
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QMessageBox>
#include <QProgressDialog>
#include <QThread>
#include <QUuid>
#include <cmath>
#include <exception>
#include <stdexcept>

namespace lmms::gui {
namespace {
void copyElement(DataFile& file,QDomElement destination,const QDomElement& source) {
 const auto attributes=source.attributes();for(int i=0;i<attributes.size();++i) destination.setAttribute(attributes.item(i).nodeName(),attributes.item(i).nodeValue());
 for(auto child=source.firstChild();!child.isNull();child=child.nextSibling()) destination.appendChild(file.importNode(child,true));
}
}
SVSProjectController::SVSProjectController(MainWindow* window,QString runtimeDirectory):QObject(window),m_window(window),m_bridge(this,std::move(runtimeDirectory)) {
 connect(&m_bridge,&svs::ProjectBridge::finished,this,[this](const QJsonObject& response){converted(response);});
}
SVSProjectController::~SVSProjectController() {
 m_cancelled=true;if(m_worker) {m_worker->disconnect(this);m_worker->wait();delete m_worker;m_worker=nullptr;}
 if(!m_resourceDirectory.isEmpty()) QDir(m_resourceDirectory).removeRecursively();
}
void SVSProjectController::progress(const QString& label) {
 if(!m_progress) {
  m_progress=new QProgressDialog(label,QStringLiteral("取消"),0,0,m_window);m_progress->setObjectName("svsProjectProgress");m_progress->setWindowTitle(QStringLiteral("SVS 工程"));m_progress->setWindowModality(Qt::WindowModal);m_progress->setMinimumDuration(0);m_progress->setAutoClose(false);
  connect(m_progress,&QProgressDialog::canceled,this,[this]{m_cancelled=true;m_bridge.cancel();if(m_progress) m_progress->setLabelText(QStringLiteral("正在取消并清理…"));});
 } else m_progress->setLabelText(label);
 m_progress->show();
}
void SVSProjectController::reset() {
 if(m_progress) {m_progress->hide();m_progress->deleteLater();m_progress=nullptr;}
 m_bridge.releaseTask();if(!m_resourceDirectory.isEmpty()) {QDir(m_resourceDirectory).removeRecursively();m_resourceDirectory.clear();}
 m_task=Task::Idle;m_cancelled=false;
}
void SVSProjectController::importProject() {
 if(m_task!=Task::Idle) return;
 m_cancelled=false;m_task=Task::Catalog;progress(QStringLiteral("正在读取支持格式…"));
 m_bridge.start({{"operation","listFormats"}});
}
void SVSProjectController::converted(const QJsonObject& response) {
 if(m_cancelled||response["status"].toString()=="cancelled") {reset();return;}
 if(response["status"].toString()!="success") {
  const auto error=response["error"].toObject();if(m_progress) m_progress->hide();QMessageBox::critical(m_window,QStringLiteral("SVS 工程转换失败"),error["message"].toString()+"\n"+error["detail"].toString());reset();return;
 }
 if(m_task==Task::Catalog) {if(m_progress) m_progress->hide();m_bridge.releaseTask();chooseSource(response["formats"].toArray());}
 else if(m_task==Task::Import) prepareAudio(response);
}
void SVSProjectController::chooseSource(const QJsonArray& formats) {
 FileDialog file(m_window,QStringLiteral("导入SVS工程"),ConfigManager::inst()->userProjectsDir());file.setFileMode(QFileDialog::ExistingFile);file.setAcceptMode(QFileDialog::AcceptOpen);
 QStringList filters;QMap<QString,QJsonObject> identities;
 for(const auto& entry:formats) {const auto format=entry.toObject();if(!format["canImport"].toBool()) continue;QStringList suffixes;for(const auto& suffix:format["suffixes"].toArray()) suffixes<<"*."+suffix.toString();const auto filter=QStringLiteral("%1 [%2] (%3)").arg(format["name"].toString(),format["id"].toString(),suffixes.join(' '));filters<<filter;identities[filter]=format;}
 file.setNameFilters(filters);if(file.exec()!=QDialog::Accepted||file.selectedFiles().isEmpty()) {reset();return;}
 const auto format=identities.value(file.selectedNameFilter());if(format.isEmpty()) {reset();return;}
 SVSProjectImportDialog dialog(format,m_window);if(dialog.exec()!=QDialog::Accepted) {reset();return;}
 m_voice=dialog.selectedVoice();m_formatId=format["id"].toString();m_task=Task::Import;progress(QStringLiteral("正在解析工程：%1").arg(QFileInfo(file.selectedFiles().first()).fileName()));
 m_bridge.start({{"operation","importProject"},{"formatId",m_formatId},{"path",QFileInfo(file.selectedFiles().first()).absoluteFilePath()},{"options",dialog.options()}});
}
void SVSProjectController::prepareAudio(const QJsonObject& response) {
 const auto project=response["project"].toObject();const auto initial=svs::ProjectMapper::prepareImport(project,m_voice,{});
 if(!initial.valid()) {if(m_progress) m_progress->hide();QMessageBox::critical(m_window,QStringLiteral("SVS 工程预检失败"),initial.error);reset();return;}
 m_task=Task::Preparation;progress(QStringLiteral("正在验证音频和工程数据…"));
 // The UUID directory is the only durable location created/removed by this task.
 m_resourceDirectory=QDir(ConfigManager::inst()->userSamplesDir()).filePath("svs-project/"+QUuid::createUuid().toString(QUuid::WithoutBraces));
 struct Preparation {svs::ProjectImport imported;QStringList warnings;};auto prepared=std::make_shared<Preparation>();
 for(const auto& item:response["warnings"].toArray()) prepared->warnings<<item.toObject()["message"].toString();
 for(const auto& item:response["losses"].toArray()) {const auto loss=item.toObject();prepared->warnings<<QStringLiteral("格式 %1，轨道 %2，%3：%4").arg(loss["formatId"].toString(),loss["track"].toString(),loss["field"].toString(),loss["reason"].toString());}
 const auto directory=m_resourceDirectory;const auto voice=m_voice;
 m_worker=QThread::create([this,project,directory,voice,prepared]{
  try {
   QMap<QString,svs::ProjectAudio> resources;int number=0;
   for(const auto& entry:project["track_list"].toArray()) {
    if(m_cancelled) return;const auto track=entry.toObject();if(track["type_"].toString()!="Instrumental") continue;
    const auto path=track["audio_file_path"].toString();if(resources.contains(path)) continue;
    const QFileInfo source(path);if(!source.isFile()||source.size()>256ll*1024*1024) continue;
    if(!QDir().mkpath(directory)) {prepared->imported.error=QStringLiteral("不能创建导入音频资源目录：%1").arg(directory);return;}
    const auto target=QDir(directory).filePath(QString::number(++number)+"-"+source.fileName());
    if(!QFile::copy(path,target)) {prepared->imported.error=QStringLiteral("不能保存导入音频资源：%1").arg(path);return;}
    const auto decoded=SampleDecoder::decode(target);if(m_cancelled) return;
    if(!decoded||decoded->data.empty()||decoded->sampleRate<=0) {QFile::remove(target);continue;}
    resources[path]={target,0,double(decoded->data.size())/decoded->sampleRate};
   }
   if(!m_cancelled) prepared->imported=svs::ProjectMapper::prepareImport(project,voice,resources);
  } catch(const std::exception& error) {prepared->imported.error=QString::fromUtf8(error.what());} catch(...) {prepared->imported.error=QStringLiteral("工程预检发生未知错误");}
 });
 connect(m_worker,&QThread::finished,this,[this,prepared]{auto* worker=m_worker;m_worker=nullptr;worker->deleteLater();if(m_cancelled) {reset();return;}finishImport(prepared->imported,prepared->warnings);});m_worker->start();
}
void SVSProjectController::finishImport(const svs::ProjectImport& prepared,const QStringList& warnings) {
 if(m_progress) m_progress->hide();
 if(!prepared.valid()) {QMessageBox::critical(m_window,QStringLiteral("SVS 工程预检失败"),prepared.error);reset();return;}
 const auto issues=warnings+prepared.losses;
 if(!issues.isEmpty()&&QMessageBox::warning(m_window,QStringLiteral("确认有损导入"),QStringLiteral("格式 %1 的以下内容需要确认：\n%2\n\n继续作为新工程导入？").arg(m_formatId,issues.join('\n')),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes) {reset();return;}
 if(!m_window->mayChangeProject(false)) {reset();return;}
 // A registry refresh may have removed the voice while conversion was running.
 bool present=false;for(const auto& voice:svs::Registry::instance().voices()) if(voice.pluginId==m_voice.pluginId&&voice.id==m_voice.voiceId) present=true;
 if(!present) {QMessageBox::critical(m_window,QStringLiteral("声库已不可用"),QStringLiteral("所选声库已移除，原工程未替换。"));reset();return;}
 QString error;if(!commitImport(prepared,*Engine::getSong(),error)) {QMessageBox::critical(m_window,QStringLiteral("导入提交失败"),error);reset();return;}
 m_resourceDirectory.clear();reset();
}
bool SVSProjectController::commitImport(const svs::ProjectImport& prepared,Song& song,QString& error) {
 static_assert(int(Track::Type::SVS)==7&&int(Track::Type::Sample)==2);
 if(!prepared.valid()) {error=prepared.error;return false;}
 DataFile native(DataFile::Type::JournalData);const auto root=prepared.document.documentElement();copyElement(native,native.head(),root.firstChildElement("head"));copyElement(native,native.content(),root.firstChildElement("song"));
 DataFile backup(DataFile::Type::JournalData);song.saveProjectState(backup);const bool modified=song.isModified();
 try {
  song.stop();song.restoreProjectState(native);
  const auto expected=root.firstChildElement("song").firstChildElement("trackcontainer").elementsByTagName("track").size();
  if(song.hasErrors()||int(song.tracks().size())!=expected) throw std::runtime_error(song.errorSummary().isEmpty()?"Incomplete project restore":song.errorSummary().toStdString());
  song.setModified(true);return true;
 } catch(const std::exception& failure) {error=QString::fromUtf8(failure.what());} catch(...) {error=QStringLiteral("工程提交发生未知错误");}
 song.restoreProjectState(backup);song.setModified(modified);return false;
}
}
