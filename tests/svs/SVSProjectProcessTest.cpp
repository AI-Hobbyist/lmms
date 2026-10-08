#include "SVSProjectBridge.h"
#include <QtTest>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QSignalSpy>

using lmms::svs::ProjectBridge;
class SVSProjectProcessTest : public QObject {
 Q_OBJECT
private:
 QString runtime() const {return QStringLiteral(LMMS_SVS_PROJECT_RUNTIME);}
 QJsonObject response(QSignalSpy& spy) {return spy.first().first().toJsonObject();}
private slots:
 void catalogAndTaskLifecycle() {
  ProjectBridge bridge(nullptr,runtime());QSignalSpy spy(&bridge,&ProjectBridge::finished);
  QVERIFY(bridge.start({{"operation","listFormats"}}));const auto task=bridge.taskDirectory();QVERIFY(QDir(task).exists());
  QVERIFY(!bridge.start({{"operation","listFormats"}}));
  QVERIFY(spy.wait(60000));const auto result=response(spy);QCOMPARE(result["status"].toString(),QString("success"));QCOMPARE(result["formats"].toArray().size(),40);QVERIFY(!bridge.busy());
  QVERIFY(QDir(task).exists());bridge.releaseTask();QVERIFY(!QDir(task).exists());
 }
 void cancelAndTimeout() {
  for(const bool cancel:{false,true}) {
   ProjectBridge bridge(nullptr,runtime());QSignalSpy spy(&bridge,&ProjectBridge::finished);
   QVERIFY(bridge.start({{"operation","listFormats"}},cancel?120000:1));const auto task=bridge.taskDirectory();
   if(cancel) bridge.cancel();
   QVERIFY(spy.wait(10000));const auto result=response(spy);QCOMPARE(result["error"].toObject()["code"].toString(),QString(cancel?"cancelled":"timeout"));
   QVERIFY(!bridge.busy());QVERIFY(!QDir(task).exists());QCOMPARE(spy.size(),1);
  }
 }
 void missingRuntimeAndCrash() {
  {
   ProjectBridge bridge(nullptr,runtime()+"/missing");QSignalSpy spy(&bridge,&ProjectBridge::finished);
   QVERIFY(bridge.start({{"operation","listFormats"}}));QTRY_COMPARE_WITH_TIMEOUT(spy.size(),1,10000);QCOMPARE(response(spy)["error"].toObject()["code"].toString(),QString("runtimeUnavailable"));QVERIFY(bridge.taskDirectory().isEmpty());
  }
  QTemporaryDir fixture;QVERIFY(fixture.isValid());QVERIFY(QDir().mkpath(fixture.path()+"/python"));
  // A real interpreter with the same isolated stdlib; only this fixture exits abnormally.
  for(const auto& name:{"python.exe","python313.dll","python3.dll","python313.zip","python313._pth","vcruntime140.dll","vcruntime140_1.dll"}) {
   const auto source=runtime()+"/python/"+name;if(QFile::exists(source)) QVERIFY(QFile::copy(source,fixture.path()+"/python/"+name));
  }
  QFile script(fixture.path()+"/bridge.py");QVERIFY(script.open(QIODevice::WriteOnly));script.write("import os\nos._exit(17)\n");script.close();
  ProjectBridge bridge(nullptr,fixture.path());QSignalSpy spy(&bridge,&ProjectBridge::finished);
  QVERIFY(bridge.start({{"operation","listFormats"}}));const auto task=bridge.taskDirectory();QVERIFY(spy.wait(10000));QCOMPARE(response(spy)["error"].toObject()["code"].toString(),QString("workerCrashed"));QVERIFY(!QDir(task).exists());
 }
};
QTEST_GUILESS_MAIN(SVSProjectProcessTest)
#include "SVSProjectProcessTest.moc"
