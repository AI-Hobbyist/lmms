#ifndef LMMS_NATIVE_VST_PRESET_DROP_H
#define LMMS_NATIVE_VST_PRESET_DROP_H

#include "DataFile.h"
#include "Effect.h"
#include "Plugin.h"
#include "vsthost/Vst3Preset.h"
#include "vsthost/VstNodePorts.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <cmath>
#include <memory>

namespace lmms::vsthost
{
struct NativeVstPresetDrop
{
	QDomElement item;
	QDomElement state;
	Plugin::Descriptor::SubPluginFeatures::Key key;
	bool instrument = false;

	bool hasAvailableIdentity() const
	{
		const auto& attributes = key.attributes;
		const auto format = attributes.value("format");
		const auto path = QFileInfo(attributes.value("file"));
		const auto architecture = attributes.value("architecture");
		if (state.isNull() || !path.exists() || (architecture != "32" && architecture != "64"))
		{
			return false;
		}
		if (format == "vst2")
		{
			bool valid = true;
			const auto shellId = attributes.value("shellid", "0").toUInt(&valid);
			if (!path.isFile() || !valid || (attributes.contains("shellid") && shellId == 0))
			{
				return false;
			}
			if (state.hasAttribute("chunk"))
			{
				return !QByteArray::fromBase64(
					state.attribute("chunk").toLatin1(), QByteArray::AbortOnBase64DecodingErrors).isEmpty();
			}
			const auto count = state.attribute("numparams").toUInt(&valid);
			if (!valid || count > 65536)
			{
				return false;
			}
			for (unsigned index = 0; index < count; ++index)
			{
				const auto key = QString::number(index);
				const auto fields = state.attribute("param" + key).split(':');
				if (fields.size() < 3 || fields.front() != key)
				{
					return false;
				}
				const auto value = fields.back().toDouble(&valid);
				if (!valid || !std::isfinite(value) || value < 0 || value > 1)
				{
					return false;
				}
			}
			return true;
		}
		static const QRegularExpression cid(QStringLiteral("^[0-9A-Fa-f]{32}$"));
		const auto classId = attributes.value("classid");
		bool validLayout = true;
		const auto layout = attributes.value("mainlayout", "0").toUInt(&validLayout);
		if (format != "vst3" || !cid.match(classId).hasMatch() || state.attribute("format") != "vst3"
			|| state.attribute("classid").compare(classId, Qt::CaseInsensitive) != 0 || !validLayout
			|| layout > 3 || state.attribute("mainlayout", "0") != QString::number(layout))
		{
			return false;
		}
		const auto bytes = QByteArray::fromBase64(
			state.attribute("vst3state").toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
		return validVst3State(std::span(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
			static_cast<std::size_t>(bytes.size())));
	}
};

inline bool isNativeVstInstrumentPreset(DataFile& preset)
{
	return preset.type() == DataFile::Type::InstrumentTrackSettings
		&& preset.content().firstChildElement("instrumenttrack").firstChildElement("instrument")
			.attribute("name") == "native_vst_instrument";
}

inline NativeVstPresetDrop readNativeVstPresetDrop(DataFile& preset)
{
	const bool instrument = preset.type() == DataFile::Type::InstrumentTrackSettings;
	if (!instrument && preset.type() != DataFile::Type::EffectSettings)
	{
		return {};
	}
	const auto descriptor = instrument ? "native_vst_instrument" : "native_vst_effect";
	const auto item = instrument
		? preset.content().firstChildElement("instrumenttrack").firstChildElement("instrument")
		: preset.content().firstChildElement("effect");
	if (item.attribute("name") != descriptor)
	{
		return {};
	}
	const auto state = item.firstChildElement(instrument ? descriptor : "native_vst_effect_controls");
	const auto keyElement = (instrument ? state : item).firstChildElement("key");
	return {item, state, Plugin::Descriptor::SubPluginFeatures::Key(keyElement), instrument};
}

inline bool nativeVstPresetRestored(Plugin* plugin)
{
	auto* owner = dynamic_cast<VstNodeOwner*>(plugin);
	auto* processor = owner ? owner->vstNodeProcessor() : nullptr;
	return processor && processor->presetStateRestored();
}

inline std::unique_ptr<Effect> createNativeVstPresetEffect(const NativeVstPresetDrop& preset, Model* parent)
{
	if (preset.instrument || !preset.hasAvailableIdentity())
	{
		return {};
	}
	auto key = preset.key;
	std::unique_ptr<Effect> effect(Effect::instantiate("native_vst_effect", parent, &key));
	if (!effect || !effect->isOkay())
	{
		return {};
	}
	effect->restoreState(preset.item);
	if (!effect->isOkay() || !nativeVstPresetRestored(effect.get()))
	{
		return {};
	}
	return effect;
}
}

#endif // LMMS_NATIVE_VST_PRESET_DROP_H
