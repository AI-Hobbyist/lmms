#include "SVSImageLoader.h"
#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>
#include <QRunnable>
#include <QFileInfo>
#include <QFile>
#include <QBuffer>
#include <QImageReader>
#include <QCryptographicHash>
#include <QCache>
#include <QMutex>
#include <QMutexLocker>
namespace lmms::gui {
namespace {
struct Images {
 QMutex mutex;
 QCache<QString,QImage> cache{32*1024*1024};
 QThreadPool pool;
 Images() {pool.setMaxThreadCount(2);}
 ~Images() {pool.waitForDone();}
};
Images& images() {static Images value; return value;}
struct Decoded {QImage image; QString diagnostic;};
Decoded decode(const QString& package,const QString& path,QSize pixels) {
 if(path.isEmpty()) return {{},QStringLiteral("Voice image is not available")};
 const auto root=QFileInfo(package).canonicalFilePath(),resolved=QFileInfo(path).canonicalFilePath();
 if(root.isEmpty()||resolved.isEmpty()||!resolved.startsWith(root+'/',Qt::CaseInsensitive)) return {{},QStringLiteral("Voice image is missing or outside its package")};
 QFile file(resolved); if(!file.open(QIODevice::ReadOnly)||file.size()>16*1024*1024) return {{},QStringLiteral("Voice image is unreadable or exceeds 16 MiB")};
 auto bytes=file.read(16*1024*1024+1); if(bytes.size()>16*1024*1024) return {{},QStringLiteral("Voice image exceeds 16 MiB")};
 const auto hash=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(); const auto key=QString::fromLatin1(hash)+QString("/%1/%2").arg(pixels.width()).arg(pixels.height());
 {QMutexLocker lock(&images().mutex); if(auto* cached=images().cache.object(key)) return {*cached,{}};}
 QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer); reader.setAutoTransform(true);
 const auto native=reader.size(); if(!native.isValid()||native.width()>32768||native.height()>32768||qint64(native.width())*native.height()>16*1024*1024) return {{},QStringLiteral("Voice image has invalid dimensions or exceeds the decoding limit")};
 const int bits=QImage::toPixelFormat(reader.imageFormat()).bitsPerPixel(); const auto rowBytes=((qint64(native.width())*qMax(32,bits?bits:128)+31)/32)*4;
 if(rowBytes*native.height()>64*1024*1024) return {{},QStringLiteral("Voice image exceeds the 64 MiB decoded image limit")};
 const auto scaled=native.scaled(pixels,Qt::KeepAspectRatio); reader.setScaledSize(scaled); auto image=reader.read();
 if(image.isNull()) return {{},QStringLiteral("Voice image decoding failed: ")+reader.errorString()};
 if(image.width()>pixels.width()||image.height()>pixels.height()) image=image.scaled(pixels,Qt::KeepAspectRatio,Qt::SmoothTransformation);
 image=image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
 {QMutexLocker lock(&images().mutex); images().cache.insert(key,new QImage(image),int(image.sizeInBytes()));}
 return {image,{}};
}
}
qsizetype SVSImageLoader::cacheBytes() {QMutexLocker lock(&images().mutex); return images().cache.totalCost();}
void SVSImageLoader::request(const QString& package,const QString& path,QSize pixels) {
 pixels.setWidth(qBound(1,pixels.width(),2048)); pixels.setHeight(qBound(1,pixels.height(),2048));
 if(package==m_package&&path==m_path&&pixels==m_pixels) return;
 m_package=package; m_path=path; m_pixels=pixels; const auto request=++m_request; m_gate->store(request); m_image={}; m_diagnostic.clear(); if(changed) changed();
 QPointer<SVSImageLoader> guard(this);
 images().pool.start(QRunnable::create([guard,gate=m_gate,request,package,path,pixels]{
  // Superseded resize requests may finish, but only the current image is published.
  if(gate->load()!=request) return; auto decoded=decode(package,path,pixels); if(gate->load()!=request) return;
  QMetaObject::invokeMethod(QCoreApplication::instance(),[guard,request,decoded=std::move(decoded)]{
   if(!guard||guard->m_request!=request) return; guard->m_image=decoded.image; guard->m_diagnostic=decoded.diagnostic; if(guard->changed) guard->changed();
  },Qt::QueuedConnection);
 }));
}
}
