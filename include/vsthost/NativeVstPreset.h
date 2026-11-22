#ifndef LMMS_NATIVE_VST_PRESET_H
#define LMMS_NATIVE_VST_PRESET_H

#include "vsthost/Protocol.h"
#include "DataFile.h"
#include "vsthost/VstCatalogSelection.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryFile>

namespace lmms::vsthost
{
enum class PresetWriteResult
{
	Written,
	AlreadyExists,
	Failed
};

inline QString nativeVstPresetPathComponent(QString label, const QString& fallback)
{
	static const QRegularExpression invalid(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])"));
	static const QRegularExpression trailing(QStringLiteral(R"([ .]+$)"));
	static const QRegularExpression reserved(QStringLiteral(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$))"),
		QRegularExpression::CaseInsensitiveOption);
	label = label.trimmed().left(96);
	label.replace(invalid, QStringLiteral("_"));
	label.remove(trailing);
	if (label.isEmpty())
	{
		return fallback;
	}
	if (reserved.match(label).hasMatch())
	{
		label.prepend('_');
	}
	return label;
}

inline QString nativeVstPresetDirectory(const QString& userPresets, Format format, bool instrument,
	const QString& pluginName, const QString& modulePath)
{
	const auto formatDir = format == Format::Vst3 ? QStringLiteral("VST3") : QStringLiteral("VST2");
	const auto typeDir = instrument ? QStringLiteral("乐器") : QStringLiteral("效果");
	const auto moduleName = nativeVstPresetPathComponent(QFileInfo(modulePath).completeBaseName(), QStringLiteral("VST"));
	const auto leaf = nativeVstPresetPathComponent(pluginName, moduleName);
	return QDir(userPresets).filePath(QStringLiteral("VST/") + formatDir + '/' + typeDir + '/' + leaf);
}

inline QDomElement nativeVstPresetKey(QDomDocument& document,
	const Plugin::Descriptor::SubPluginFeatures::Key& key)
{
	auto element = document.createElement("key");
	for (auto attribute = key.attributes.cbegin(); attribute != key.attributes.cend(); ++attribute)
	{
		auto field = document.createElement("attribute");
		field.setAttribute("name", attribute.key());
		field.setAttribute("value", attribute.value());
		element.appendChild(field);
	}
	return element;
}

inline PresetWriteResult writeScannedVstPreset(
	const QString& destination, const CatalogEntry& entry, const QDomElement& pluginState)
{
	const bool instrument = isVstInstrument(entry);
	const auto descriptor = instrument ? "native_vst_instrument" : "native_vst_effect";
	DataFile preset(instrument ? DataFile::Type::InstrumentTrackSettings : DataFile::Type::EffectSettings);
	auto item = preset.createElement(instrument ? "instrument" : "effect");
	item.setAttribute("name", descriptor);
	auto state = preset.importNode(pluginState, true).toElement();
	state.setTagName(instrument ? descriptor : "native_vst_effect_controls");
	auto key = vstCatalogKey(nullptr, entry);
	key.attributes["pluginname"] = entry.name;
	auto keyElement = nativeVstPresetKey(preset, key);
	if (instrument)
	{
		state.appendChild(keyElement);
		preset.content().setAttribute("name", entry.name);
		preset.content().setAttribute("muted", 0);
		preset.content().setAttribute("solo", 0);
	}
	else
	{
		item.setAttribute("on", 1);
		item.setAttribute("wet", 1);
		item.appendChild(keyElement);
	}
	item.appendChild(state);
	if (instrument)
	{
		auto track = preset.createElement("instrumenttrack");
		track.appendChild(item);
		preset.content().appendChild(track);
	}
	else
	{
		preset.content().appendChild(item);
	}

	const auto directory = QFileInfo(destination).absolutePath();
	if (!QDir().mkpath(directory))
	{
		return PresetWriteResult::Failed;
	}
	if (QFileInfo::exists(destination))
	{
		return PresetWriteResult::AlreadyExists;
	}
	QTemporaryFile file(QDir(directory).filePath(".lmms-vst-preset-XXXXXX"));
	if (!file.open())
	{
		return PresetWriteResult::Failed;
	}
	const auto xml = preset.toByteArray();
	if (file.write(xml) != xml.size() || !file.flush())
	{
		return PresetWriteResult::Failed;
	}
	file.close();
	if (!file.rename(destination))
	{
		return QFileInfo::exists(destination) ? PresetWriteResult::AlreadyExists : PresetWriteResult::Failed;
	}
	file.setAutoRemove(false);
	return PresetWriteResult::Written;
}
}

#endif // LMMS_NATIVE_VST_PRESET_H
