#ifndef LMMS_AGENT_PROJECT_SNAPSHOT_H
#define LMMS_AGENT_PROJECT_SNAPSHOT_H

#include <QJsonObject>
#include <QMap>
#include <QString>

namespace lmms::agent
{
using ProjectFields = QMap<QString, QString>;
ProjectFields projectFields();
QJsonObject projectDiff(const QString& command, const ProjectFields& before, const ProjectFields& after);
}

#endif
