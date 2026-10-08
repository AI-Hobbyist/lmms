#ifndef LMMS_SVS_PROJECT_CONTROLLER_H
#define LMMS_SVS_PROJECT_CONTROLLER_H
#include "SVSProjectBridge.h"
#include "SVSProjectMapper.h"
#include <QPointer>
#include <atomic>
class QProgressDialog;
class QThread;
namespace lmms {class Song;}
namespace lmms::gui {
class MainWindow;
class SVSProjectController : public QObject {
public:
 explicit SVSProjectController(MainWindow* window,QString runtimeDirectory={});
 ~SVSProjectController() override;
 void importProject();
 bool busy() const {return m_task!=Task::Idle;}
 static bool commitImport(const svs::ProjectImport& prepared,Song& song,QString& error);
private:
 void converted(const QJsonObject& response);
 void chooseSource(const QJsonArray& formats);
 void prepareAudio(const QJsonObject& response);
 void finishImport(const svs::ProjectImport& prepared,const QStringList& warnings);
 void progress(const QString& label);
 void reset();
 MainWindow* m_window;
 svs::ProjectBridge m_bridge;
 QPointer<QProgressDialog> m_progress;
 QThread* m_worker=nullptr;
 svs::ProjectVoice m_voice;
 QString m_formatId;
 QString m_resourceDirectory;
 std::atomic<bool> m_cancelled{false};
 enum class Task {Idle,Catalog,Import,Preparation};
 Task m_task=Task::Idle;
};
}
#endif
