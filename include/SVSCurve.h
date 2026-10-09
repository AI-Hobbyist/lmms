#ifndef LMMS_SVS_CURVE_H
#define LMMS_SVS_CURVE_H
#include "SVSCapabilities.h"
#include "svs_curve.hpp"
#include <optional>

namespace lmms::svs {
class Curve
{
public:
	QString id, unit, scope = "clip", type = "float", mode, interpolation = "linear";
	svs_sdk::Curve evaluator;
	QJsonObject extra;
	std::optional<QJsonValue> valueAt(double tick) const;
	double derivativeAt(double tick) const;
	void insert(double tick, const QJsonValue& value);
	void erase(double start, double end);
	void connect(double start, double end);
	void pinTangents();
	void replaceRange(double start, double end, const Curve& localSource);
	Curve slice(double start, double end, bool rebase = true) const;
	QJsonObject toJson() const;
	static bool fromJson(const QJsonObject&, Curve&, QString& error, const Parameter* descriptor = nullptr);
	bool operator==(const Curve& other) const { return toJson() == other.toJson(); }
};
Curve withParameterBase(const Curve&, const Parameter&, const QJsonValue& base);
using Curves = QMap<QString, Curve>;
QJsonObject curvesToJson(const Curves&);
Curves curvesFromJson(const QJsonObject&, QStringList* diagnostics = nullptr);
bool absolutePitchToOffset(const Curve& absolute, const Curve& reference, Curve& output, QString& error);
}
#endif
