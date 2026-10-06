#ifndef LMMS_SVS_MONOTONIC_CURVE_H
#define LMMS_SVS_MONOTONIC_CURVE_H
#include "SVSCurve.h"

namespace lmms::gui {
// The reference editor eases endpoints to zero and uses the harmonic mean
// of neighboring secants internally. Pin these tangents before splicing so
// outside anchors cannot change the newly drawn interval's shape.
inline svs::Curve monotonicCurve(svs::Curve curve) {
 curve.interpolation="hermite";curve.evaluator.interpolation=svs_sdk::Interpolation::Hermite;
 for(auto& point:curve.evaluator.points) {point.automatic=true;point.segmentInterpolation=static_cast<int>(svs_sdk::Interpolation::Hermite);}
 curve.pinTangents();
 if(!curve.evaluator.points.empty()) {
  for(auto* endpoint:{&curve.evaluator.points.front(),&curve.evaluator.points.back()}) endpoint->tangentIn=endpoint->tangentOut=0;
 }
 return curve;
}
}
#endif
