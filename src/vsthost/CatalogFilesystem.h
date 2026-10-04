#ifndef LMMS_VSTHOST_CATALOG_FILESYSTEM_H
#define LMMS_VSTHOST_CATALOG_FILESYSTEM_H
#include <QJsonObject>
namespace lmms::vsthost
{
// Linked only into the filesystem worker, never into the DAW.
QJsonObject catalogFileOperation(const QJsonObject& request);
}
#endif
