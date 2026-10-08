#ifndef LMMS_SVS_PROJECT_BRIDGE_H
#define LMMS_SVS_PROJECT_BRIDGE_H

#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

namespace lmms::svs {
// One worker at a time. Successful task resources live until releaseTask().
class ProjectBridge : public QObject {
 Q_OBJECT
public:
 explicit ProjectBridge(QObject* parent=nullptr,QString runtimeDirectory={});
 ~ProjectBridge() override;
 bool start(QJsonObject request,int timeoutMs=120000);
 void cancel();
 bool busy() const {return m_active;}
 QString taskDirectory() const {return m_task?m_task->path():QString{};}
 void releaseTask();
signals:
 void finished(const QJsonObject& response);
private:
 void complete(int exitCode,QProcess::ExitStatus);
 void fail(const QString& code,const QString& message);
 QProcess m_process;
 QTimer m_timeout,m_killTimer;
 QString m_runtime,m_requestId,m_formatId,m_failureCode,m_failureMessage;
 QByteArray m_output,m_diagnostics;
 std::unique_ptr<QTemporaryDir> m_task;
 bool m_delivered=false,m_active=false;
};
}
#endif
