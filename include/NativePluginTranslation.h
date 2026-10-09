#ifndef LMMS_NATIVE_PLUGIN_TRANSLATION_H
#define LMMS_NATIVE_PLUGIN_TRANSLATION_H

#include <QCoreApplication>

#include "SVSCapabilities.h"

namespace lmms::gui::nativeTranslation {

// Only display copies are localized. Protocol declarations and saved IDs stay intact.
inline QString svsText(const QString& plugin, const QString& source)
{
	const bool example = plugin == "org.lmms.svs.example";
	const bool diffSinger = plugin == "org.lmms.svs.diffsinger";
	if (!example && !diffSinger) { return source; }
	static const char* const exampleTexts[] = {
		QT_TRANSLATE_NOOP("NativeSVS", "Mode"),
		QT_TRANSLATE_NOOP("NativeSVS", "Voice"),
		QT_TRANSLATE_NOOP("NativeSVS", "Basic"),
		QT_TRANSLATE_NOOP("NativeSVS", "Advanced"),
		QT_TRANSLATE_NOOP("NativeSVS", "Gain"),
		QT_TRANSLATE_NOOP("NativeSVS", "Breath"),
		QT_TRANSLATE_NOOP("NativeSVS", "Expression"),
		QT_TRANSLATE_NOOP("NativeSVS", "Tension"),
		QT_TRANSLATE_NOOP("NativeSVS", "Gender"),
		QT_TRANSLATE_NOOP("NativeSVS", "Power"),
		QT_TRANSLATE_NOOP("NativeSVS", "Note"),
		QT_TRANSLATE_NOOP("NativeSVS", "Soft"),
		QT_TRANSLATE_NOOP("NativeSVS", "Label"),
		QT_TRANSLATE_NOOP("NativeSVS", "Phoneme gain"),
		QT_TRANSLATE_NOOP("NativeSVS", "Phoneme"),
		QT_TRANSLATE_NOOP("NativeSVS", "Rendered energy"),
		QT_TRANSLATE_NOOP("NativeSVS", "Rendered level"),
		QT_TRANSLATE_NOOP("NativeSVS", "Rendered peak"),
		QT_TRANSLATE_NOOP("NativeSVS", "Singing Voice Synthesis"),
	};
	static const char* const diffSingerTexts[] = {
		QT_TRANSLATE_NOOP("NativeSVS", "Speaker"),
		QT_TRANSLATE_NOOP("NativeSVS", "Voice"),
		QT_TRANSLATE_NOOP("NativeSVS", "Rendering steps"),
		QT_TRANSLATE_NOOP("NativeSVS", "Voicebank directories"),
		QT_TRANSLATE_NOOP("NativeSVS", "Global shared vocoder directories"),
		QT_TRANSLATE_NOOP("NativeSVS", "Show phoneme language prefixes"),
		QT_TRANSLATE_NOOP("NativeSVS", "energy (absolute)"),
		QT_TRANSLATE_NOOP("NativeSVS", "energy offset"),
		QT_TRANSLATE_NOOP("NativeSVS", "energy"),
		QT_TRANSLATE_NOOP("NativeSVS", "breathiness (absolute)"),
		QT_TRANSLATE_NOOP("NativeSVS", "breathiness offset"),
		QT_TRANSLATE_NOOP("NativeSVS", "breathiness"),
		QT_TRANSLATE_NOOP("NativeSVS", "voicing (absolute)"),
		QT_TRANSLATE_NOOP("NativeSVS", "voicing offset"),
		QT_TRANSLATE_NOOP("NativeSVS", "voicing"),
		QT_TRANSLATE_NOOP("NativeSVS", "tension (absolute)"),
		QT_TRANSLATE_NOOP("NativeSVS", "tension offset"),
		QT_TRANSLATE_NOOP("NativeSVS", "tension"),
		QT_TRANSLATE_NOOP("NativeSVS", "Gender"),
		QT_TRANSLATE_NOOP("NativeSVS", "Velocity"),
		QT_TRANSLATE_NOOP("NativeSVS", "Expressiveness"),
	};
	auto translateKnown = [&](const auto& texts) {
		for (const auto* text : texts)
		{
			if (source == QLatin1String(text)) { return QCoreApplication::translate("NativeSVS", text); }
		}
		return source;
	};
	return example ? translateKnown(exampleTexts) : translateKnown(diffSingerTexts);
}

inline QVector<svs::Parameter> svsParameters(const QString& plugin, QVector<svs::Parameter> parameters)
{
	for (auto& parameter : parameters)
	{
		parameter.name = svsText(plugin, parameter.name);
		// DiffSinger's speaker choices are voicebank data, even if a name matches a UI word.
		if (plugin == "org.lmms.svs.example" && parameter.id == "example.mode")
		{
			for (int index = 0; index < parameter.choices.size(); ++index)
			{
				auto item = parameter.choices[index].toObject();
				item["name"] = svsText(plugin, item["name"].toString());
				parameter.choices[index] = item;
			}
		}
	}
	return parameters;
}

inline QString rvcText(const QString& engine, const QString& source)
{
	if (engine != "RVC") { return source; }
	struct Entry
	{
		const char* value;
		const char* text;
	};
	static const Entry entries[] = {
		{"pitch_shift", QT_TRANSLATE_NOOP("NativeRVC", "Pitch shift")},
		{"f0_method", QT_TRANSLATE_NOOP("NativeRVC", "Pitch detection method")},
		{"index_rate", QT_TRANSLATE_NOOP("NativeRVC", "Index rate")},
		{"index_mode", QT_TRANSLATE_NOOP("NativeRVC", "Index mode")},
		{"index_id", QT_TRANSLATE_NOOP("NativeRVC", "Index")},
		{"protect", QT_TRANSLATE_NOOP("NativeRVC", "Protect unvoiced consonants")},
		{"filter_radius", QT_TRANSLATE_NOOP("NativeRVC", "Filter radius")},
		{"rms_mix_rate", QT_TRANSLATE_NOOP("NativeRVC", "Volume envelope mix")},
		{"resample_sr", QT_TRANSLATE_NOOP("NativeRVC", "Output sample rate")},
		{"chunk_seconds", QT_TRANSLATE_NOOP("NativeRVC", "Chunk duration")},
		{"auto", QT_TRANSLATE_NOOP("NativeRVC", "Automatic")},
		{"off", QT_TRANSLATE_NOOP("NativeRVC", "Off")},
		{"required", QT_TRANSLATE_NOOP("NativeRVC", "Required")},
		{"Speaker 0", QT_TRANSLATE_NOOP("NativeRVC", "Speaker 0")},
		{"Automatic / no index", QT_TRANSLATE_NOOP("NativeRVC", "Automatic / no index")},
		{"Index", QT_TRANSLATE_NOOP("NativeRVC", "Index")},
		{"This weight has no usable index", QT_TRANSLATE_NOOP("NativeRVC", "This weight has no usable index")},
		{"Backend did not report availability", QT_TRANSLATE_NOOP("NativeRVC", "Backend did not report availability")},
		{"Weight is unusable", QT_TRANSLATE_NOOP("NativeRVC", "Weight is unusable")},
		{"Index is unusable", QT_TRANSLATE_NOOP("NativeRVC", "Index is unusable")},
		{"Multiple compatible indexes require an explicit selection",
			QT_TRANSLATE_NOOP("NativeRVC", "Multiple compatible indexes require an explicit selection")},
		{"Hz (0 = model rate)", QT_TRANSLATE_NOOP("NativeRVC", "Hz (0 = model rate)")},
		{"semitones", QT_TRANSLATE_NOOP("NativeRVC", "semitones")},
	};
	for (const auto& entry : entries)
	{
		if (source == QLatin1String(entry.value)) { return QCoreApplication::translate("NativeRVC", entry.text); }
	}
	return source;
}

} // namespace lmms::gui::nativeTranslation

#endif
