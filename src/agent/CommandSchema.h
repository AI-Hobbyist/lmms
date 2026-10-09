#ifndef LMMS_AGENT_COMMAND_SCHEMA_H
#define LMMS_AGENT_COMMAND_SCHEMA_H

#include <QJsonObject>
#include <QString>

namespace lmms::agent {

QJsonObject describeArguments(QJsonObject schema, bool mutating);
QString validateArguments(const QJsonObject& schema, const QJsonObject& arguments);

} // namespace lmms::agent

#endif
