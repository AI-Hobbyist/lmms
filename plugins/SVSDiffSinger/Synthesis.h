#ifndef DIFFSINGER_SYNTHESIS_H
#define DIFFSINGER_SYNTHESIS_H
#include "Duration.h"
#include "TensorCache.h"
namespace diffsinger {
struct SynthesisResult
{
	std::vector<float> stereo;
	double start = 0;
	Json feedback = Json::object();
};
class Synthesis
{
public:
	Synthesis(Ort::Env&, std::shared_ptr<const VoicePackage>);
	SynthesisResult render(const DurationPlan&, const std::vector<NoteInput>&, const Json&, const svs_sdk::TempoMap&,
						   double origin, uint32_t rate, const std::atomic<bool>&);
	static void declareParameters(const VoicePackage&, Json& schema);

private:
	Ort::Env& m_environment;
	std::shared_ptr<const VoicePackage> m_voice;
	Pronunciation m_pronunciation;
	std::map<std::string, std::unique_ptr<CpuModel>> m_models;
	Json m_computePolicy = Json::object();
	uint32_t m_defaultSeed = 0;
	uint32_t m_seed = 0;
	uint32_t m_pitchSeed = 0;
	CpuModel& model(const std::string& stage, const std::string& role);
};
} // namespace diffsinger
#endif
