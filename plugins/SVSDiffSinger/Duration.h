#ifndef DIFFSINGER_DURATION_H
#define DIFFSINGER_DURATION_H
#include "CpuModel.h"
#include "Pronunciation.h"
#include "svs_time.hpp"
namespace diffsinger {
struct NoteInput
{
	std::string id, lyric, reading, language;
	double tick = 0, durationTick = 0, start = 0, duration = 0, pitch = 60;
	Json phonemes = Json::object(), pronunciation = Json::object();
};
struct Phone
{
	std::string symbol, language, noteId;
	double start = 0, end = 0, pitch = 60;
	bool manual = false;
};
struct DurationPlan
{
	std::vector<Phone> phones;
	std::vector<float> predictions;
	Json feedback = Json::array();
	Json computeStages = Json::array();
};
class Duration
{
public:
	Duration(Ort::Env&, std::shared_ptr<const VoicePackage>, const Json& policy = Json::object());
	DurationPlan predict(const std::vector<NoteInput>&, const svs_sdk::TempoMap&, double tempoOrigin,
						 const Json& parameters, const std::atomic<bool>&);

private:
	std::shared_ptr<const VoicePackage> m_voice;
	Pronunciation m_pronunciation;
	std::unique_ptr<CpuModel> m_linguistic, m_duration;
};
} // namespace diffsinger
#endif
