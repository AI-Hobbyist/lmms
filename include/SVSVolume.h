#ifndef LMMS_SVS_VOLUME_H
#define LMMS_SVS_VOLUME_H

#include <QCoreApplication>
#include <algorithm>
#include <cmath>

#include "SVSCurve.h"

namespace lmms::svs {
inline const QString VolumeId = QStringLiteral("svs.volume");

inline Parameter volumeParameter()
{
	Parameter parameter;
	parameter.id = VolumeId;
	parameter.name = QCoreApplication::translate("NativeSVS", "Volume");
	parameter.type = "float";
	parameter.scope = "clip";
	parameter.unit = "dB";
	parameter.defaultValue = 0.;
	parameter.minimum = -12.;
	parameter.maximum = 12.;
	parameter.step = .1;
	parameter.interpolation = "hermite";
	parameter.curve = true;
	parameter.color = "#737CE5";
	return parameter;
}

inline void addHostVolume(Capabilities& capabilities)
{
	// The host owns this identifier independently of engine declarations.
	capabilities.parameters.removeIf([](const Parameter& parameter) { return parameter.id == VolumeId; });
	capabilities.parameters.prepend(volumeParameter());
}

struct VolumeAutomation
{
	Curve curve;
	double base = 0.;

	double gainAt(double tick) const
	{
		const auto value = curve.valueAt(tick);
		const double volume = std::clamp(base + (value ? value->toDouble() : 0.), -12., 12.);
		// TuneLab's lower tail reaches silence while joining the dB curve smoothly.
		static const double slope = std::log(10.) / 20.;
		static const double crossover = 1. / slope - 12.;
		static const double tail = std::pow(10., crossover / 20.) * slope;
		return volume > crossover ? std::pow(10., volume / 20.) : (volume + 12.) * tail;
	}
};
} // namespace lmms::svs
#endif
