#ifndef LMMS_AGENT_SCRIPT_EXPRESSION_H
#define LMMS_AGENT_SCRIPT_EXPRESSION_H

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <cstdint>

namespace lmms::agent {
// Throws std::runtime_error with a user-facing diagnostic on invalid input.
QJsonValue evaluateScriptValue(const QJsonValue& value, const QJsonObject& variables, std::uint32_t& randomState,
	int ticksPerBar, const QString& argumentName = {});
QJsonValue evaluateScriptExpression(
	const QString& expression, const QJsonObject& variables, std::uint32_t& randomState);
double scriptNumber(const QJsonValue& value);
bool scriptBoolean(const QJsonValue& value);
double scriptRandom(std::uint32_t& state, double minimum, double maximum);
}

#endif
