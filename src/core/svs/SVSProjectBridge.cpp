#include "SVSProjectBridge.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QUuid>
#include <algorithm>

namespace lmms::svs {
namespace {constexpr qsizetype MaximumJson=64*1024*1024;}
ProjectBridge::ProjectBridge(QObject* parent,QString directory):QObject(parent),
 m_runtime(directory.isEmpty()?QCoreApplication::applicationDirPath()+"/svs-project":std::move(directory)) {
 m_timeout.setSingleShot(true);m_killTimer.setSingleShot(true);
 connect(&m_timeout,&QTimer::timeout,this,[this]{m_failureCode="timeout";m_failureMessage="SVS conversion timed out";m_process.terminate();m_killTimer.start(1000);});
 connect(&m_killTimer,&QTimer::timeout,&m_process,&QProcess::kill);
 connect(&m_process,&QProcess::started,this,[this]{m_process.write(m_output);m_output.clear();m_process.closeWriteChannel();});
 connect(&m_process,&QProcess::readyReadStandardOutput,this,[this]{
  m_output+=m_process.readAllStandardOutput();
  if(m_output.size()>MaximumJson) {m_failureCode="resultTooLarge";m_failureMessage="SVS conversion result exceeds size limit";m_process.kill();}
 });
 connect(&m_process,&QProcess::readyReadStandardError,this,[this]{
  const auto data=m_process.readAllStandardError();if(m_diagnostics.size()<1024*1024) m_diagnostics+=data.left(1024*1024-m_diagnostics.size());
 });
 connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,&ProjectBridge::complete);
 connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart) fail("runtimeUnavailable",m_process.errorString());});
}
ProjectBridge::~ProjectBridge() {m_timeout.stop();m_killTimer.stop();m_process.disconnect(this);if(busy()) {m_process.kill();m_process.waitForFinished(3000);}}
bool ProjectBridge::start(QJsonObject request,int timeoutMs) {
 if(busy()||m_task) return false;m_active=true;
 m_requestId=QUuid::createUuid().toString(QUuid::WithoutBraces);m_formatId=request["formatId"].toString();m_failureCode.clear();m_failureMessage.clear();m_delivered=false;m_diagnostics.clear();
 request["protocol"]=1;request["requestId"]=m_requestId;
 m_output=QJsonDocument(request).toJson(QJsonDocument::Compact);
 if(m_output.size()>MaximumJson) {QTimer::singleShot(0,this,[this]{fail("requestTooLarge","SVS conversion request exceeds size limit");});return true;}
 m_task=std::make_unique<QTemporaryDir>(QDir::tempPath()+"/lmms-svs-project-XXXXXX");
 if(!m_task->isValid()) {QTimer::singleShot(0,this,[this]{fail("temporaryDirectory","Cannot create SVS conversion task directory");});return true;}
 // Output paths are supplied as names; only the bridge chooses the staging root.
 if(request["operation"]=="exportProject") {
  auto name=request["path"].toString();name=QFileInfo(name).fileName();
  if(name.isEmpty()||name=="."||name=="..") {QTimer::singleShot(0,this,[this]{fail("invalidPath","Missing SVS output filename");});return true;}
  request["path"]=m_task->path()+"/output/"+name;m_output=QJsonDocument(request).toJson(QJsonDocument::Compact);
 }
 auto environment=QProcessEnvironment::systemEnvironment();environment.remove("PYTHONPATH");environment.remove("PYTHONHOME");environment.insert("PYTHONNOUSERSITE","1");
 m_process.setProcessEnvironment(environment);m_process.setWorkingDirectory(m_task->path());m_process.setProcessChannelMode(QProcess::SeparateChannels);
 m_process.start(m_runtime+"/python/python.exe",{"-I",m_runtime+"/bridge.py","--workspace",m_task->path()});
 m_timeout.start(std::max(1,timeoutMs));return true;
}
void ProjectBridge::cancel() {
 if(!busy()) return;m_failureCode="cancelled";m_failureMessage="SVS conversion cancelled";m_timeout.stop();m_process.terminate();m_killTimer.start(1000);
}
void ProjectBridge::releaseTask() {if(!busy()) m_task.reset();}
void ProjectBridge::fail(const QString& code,const QString& message) {
 if(m_delivered) return;m_delivered=true;m_active=false;m_timeout.stop();m_killTimer.stop();m_task.reset();m_output.clear();
 emit finished({{"protocol",1},{"dataVersion",1},{"requestId",m_requestId},{"formatId",m_formatId},{"status",code=="cancelled"?"cancelled":"error"},{"warnings",QJsonArray{}},{"losses",QJsonArray{}},{"error",QJsonObject{{"code",code},{"message",message},{"detail",QString::fromUtf8(m_diagnostics)}}}});
}
void ProjectBridge::complete(int code,QProcess::ExitStatus status) {
 m_timeout.stop();m_killTimer.stop();m_output+=m_process.readAllStandardOutput();
 if(!m_failureCode.isEmpty()) {fail(m_failureCode,m_failureMessage);return;}
 if(code!=0||status!=QProcess::NormalExit) {fail("workerCrashed","SVS conversion process terminated unexpectedly");return;}
 QJsonParseError parse;const auto document=QJsonDocument::fromJson(m_output,&parse);auto response=document.object();
 const auto responseStatus=response["status"].toString();
 if(m_output.size()>MaximumJson||parse.error!=QJsonParseError::NoError||!document.isObject()||response["protocol"].toInt()!=1||response["dataVersion"].toInt()!=1||response["requestId"]!=m_requestId||response["formatId"].toString()!=m_formatId||!QStringList{"success","error","cancelled"}.contains(responseStatus)) {fail("invalidResponse","Invalid SVS conversion response");return;}
 if(response.contains("files")) for(const auto& file:response["files"].toArray()) {
  const auto canonical=QFileInfo(file.toString()).canonicalFilePath();const auto root=QFileInfo(m_task->path()).canonicalFilePath()+"/";
  if(canonical.isEmpty()||!canonical.startsWith(root,Qt::CaseInsensitive)||!QFileInfo(canonical).isFile()) {fail("invalidOutput","SVS output escaped its task directory");return;}
 }
 m_delivered=true;m_active=false;m_output.clear();if(responseStatus!="success") m_task.reset();emit finished(response);
}
}
