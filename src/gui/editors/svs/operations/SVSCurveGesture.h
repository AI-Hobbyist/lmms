#ifndef LMMS_SVS_CURVE_GESTURE_H
#define LMMS_SVS_CURVE_GESTURE_H
#include "SVSCurve.h"
#include "SVSMonotonicCurve.h"
#include <algorithm>

namespace lmms::gui {
// A stroke edits one content-time interval. The original curve is immutable.
class SVSCurveGesture {
public:
 enum class Kind { Freehand,Line,Smooth,Erase,Reset };
 svs::Curve original,preview;
 void begin(const svs::Curve& curve,double tick,const QJsonValue& value,Kind kind) {
  original=preview=curve; m_tick=m_last=m_first=m_end=tick; m_value=value; m_kind=kind;
  m_stroke=curve; m_stroke.evaluator.points.clear(); m_stroke.evaluator.gaps.clear(); m_stroke.insert(tick,value);
 }
 void update(double tick,const QJsonValue& value) {
  preview=original;
  if(m_kind==Kind::Reset) {
   m_first=std::min(m_first,tick); m_end=std::max(m_end,tick);
   auto source=m_stroke; source.evaluator.points.clear(); source.insert(0,m_value); source.insert(m_end-m_first,m_value);
   preview.replaceRange(m_first,m_end,source); return;
  }
  const auto start=std::min(m_tick,tick),end=std::max(m_tick,tick);
  if(m_kind==Kind::Erase) { preview.erase(start,end); return; }
  if(m_kind==Kind::Freehand) {
   auto& points=m_stroke.evaluator.points;
   points.erase(std::remove_if(points.begin(),points.end(),[&](const auto& p){return p.tick>std::min(m_last,tick)&&p.tick<std::max(m_last,tick);}),points.end());
   m_stroke.insert(tick,value); m_last=tick;
   const auto first=points.front().tick,last=points.back().tick;
   auto stroke=m_stroke;
   if(stroke.type=="float"&&stroke.evaluator.interpolation!=svs_sdk::Interpolation::Step) {
    // TuneLab's freehand reduction: keep extrema, collapse flat interiors,
    // and suppress near-collinear samples less than five ticks apart.
    auto& reduced=stroke.evaluator.points;reduced.clear();reduced.push_back(points.front());
    for(size_t i=1;i+1<points.size();++i) {
     const auto& previous=reduced.back();const auto& point=points[i];const auto& next=points[i+1];
     const auto left=(point.value-previous.value)/(point.tick-previous.tick),right=(next.value-point.value)/(next.tick-point.tick);
     if(left==0&&right==0) continue;
     if(point.tick-previous.tick<5&&left*right>0&&std::abs(std::log2(left/right))<2) continue;
     reduced.push_back(point);
    }
    if(points.size()>1) reduced.push_back(points.back());
    stroke=monotonicCurve(std::move(stroke));
   }
   auto source=stroke.slice(first,last);
   preview.replaceRange(first,last,source);
  } else {
   auto source=original; source.evaluator.points.clear(); source.evaluator.gaps.clear();
   if(source.type=="float") { source.interpolation=m_kind==Kind::Smooth?"hermite":"linear"; source.evaluator.interpolation=m_kind==Kind::Smooth?svs_sdk::Interpolation::Hermite:svs_sdk::Interpolation::Linear; }
   source.insert(0,tick<m_tick?value:m_value); source.insert(end-start,tick<m_tick?m_value:value);
   for(auto& anchor:source.evaluator.points) { anchor.segmentInterpolation=static_cast<int>(source.evaluator.interpolation); if(m_kind==Kind::Smooth&&source.type=="float") { anchor.automatic=false; anchor.tangentIn=anchor.tangentOut=0; } }
   preview.replaceRange(start,end,source);
  }
 }
private:
 Kind m_kind=Kind::Freehand;
 double m_tick=0,m_last=0,m_first=0,m_end=0;
 QJsonValue m_value;
 svs::Curve m_stroke;
};
}
#endif
