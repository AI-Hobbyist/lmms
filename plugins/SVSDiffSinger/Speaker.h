#ifndef DIFFSINGER_SPEAKER_H
#define DIFFSINGER_SPEAKER_H
#include "VoiceCatalog.h"
namespace diffsinger {
Json speakerChoices(const VoicePackage&);
std::vector<float> speakerEmbedding(const VoicePackage&, const StageConfig&, const Json& parameters);
}
#endif
