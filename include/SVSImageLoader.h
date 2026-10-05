#ifndef LMMS_SVS_IMAGE_LOADER_H
#define LMMS_SVS_IMAGE_LOADER_H
#include <QObject>
#include <QImage>
#include <QSize>
#include <functional>
#include <atomic>
#include <memory>
namespace lmms::gui {
// GUI-owned request gate; decoding and the bounded image cache live off the GUI thread.
class SVSImageLoader : public QObject {
public:
 explicit SVSImageLoader(QObject* parent=nullptr):QObject(parent) {}
 ~SVSImageLoader() override {++*m_gate;}
 void request(const QString& package,const QString& path,QSize pixels);
 QImage image() const { return m_image; }
 QString diagnostic() const { return m_diagnostic; }
 std::function<void()> changed;
 static qsizetype cacheBytes();
private:
 QString m_package,m_path,m_diagnostic;
 QSize m_pixels;
 QImage m_image;
 quint64 m_request=0;
 std::shared_ptr<std::atomic<quint64>> m_gate=std::make_shared<std::atomic<quint64>>(0);
};
}
#endif
