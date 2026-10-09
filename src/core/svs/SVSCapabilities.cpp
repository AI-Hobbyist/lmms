#include "SVSCapabilities.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

#include "SVSCurve.h"

namespace lmms::svs {

bool Parameter::accepts(const QJsonValue& value) const
{
	if (type == "directory-list")
	{
		if (!value.isArray() || value.toArray().size() > maxItems)
			return false;
		for (const auto& path : value.toArray())
			if (!path.isString() || path.toString().size() > 32768)
				return false;
		return true;
	}
	if (type == "bool")
		return value.isBool();
	if (type == "string")
		return value.isString() && (resourceIds.isEmpty() || resourceIds.contains(value.toString()));
	if (type == "enum")
	{
		if (!value.isString())
			return false;
		for (const auto& choice : choices)
			if (choice.toObject()["id"] == value)
				return true;
		return false;
	}
	if (!value.isDouble())
		return false;
	const auto number = value.toDouble();
	return std::isfinite(number) && number >= minimum && number <= maximum
		&& (type != "int" || std::floor(number) == number);
}

bool Parameter::isVisible(const QJsonObject& context) const
{
	if (!visible)
		return false;
	for (auto i = visibleWhen.begin(); i != visibleWhen.end(); ++i)
		if (context.value(i.key()) != i.value())
			return false;
	return true;
}

const Parameter* Capabilities::parameter(const QString& id, const QString& scope) const
{
	for (const auto& parameter : parameters)
		if (parameter.id == id && parameter.scope == scope)
			return &parameter;
	return nullptr;
}

bool Capabilities::parse(const QJsonObject& object, Capabilities& output, QString& error)
{
	Capabilities parsed;
	parsed.original = object;
	if (object["schemaVersion"].toInt() != 1)
	{
		error = QCoreApplication::translate("NativeSVS", "Unsupported capability schema version");
		return false;
	}
	for (const auto& required : object["requiredCapabilities"].toArray())
	{
		if (!QStringList{"parameters", "pronunciation", "pitch", "phonemes", "resources", "synthesis"}.contains(
				required.toString()))
		{
			error
				= QCoreApplication::translate("NativeSVS", "Unknown required capability: %1").arg(required.toString());
			return false;
		}
	}
	QMap<QString, QString> curveColors;
	const QStringList defaultCurveColors{
		"#73B8E5", "#73E5C2", "#E573A5", "#C2E573", "#A573E5", "#E5AD73", "#E5DD73", "#E57373"};
	auto readParameters = [&](const QString& key, QVector<Parameter>& result, bool writable) {
		QSet<QString> ids;
		if (!object[key].isArray())
		{
			error = QCoreApplication::translate("NativeSVS", "%1 must be an array").arg(key);
			return false;
		}
		for (const auto& item : object[key].toArray())
		{
			const auto value = item.toObject();
			Parameter p;
			p.id = value["id"].toString();
			p.name = value["name"].toString();
			p.translationKey = value["translationKey"].toString();
			p.group = value["group"].toString();
			p.order = value["order"].toInt();
			p.type = value["type"].toString();
			p.maxItems = value["maxItems"].toInt(128);
			p.scope = value["scope"].toString();
			p.unit = value["unit"].toString();
			p.scale = value["scale"].toString("linear");
			p.interpolation = value["interpolation"].toString("step");
			p.defaultValue = value["default"];
			p.color = value["color"].toString();
			if (value.contains("color")
				&& (!value["color"].isString() || !QRegularExpression("^#[0-9a-fA-F]{6}$").match(p.color).hasMatch()))
			{
				error = QCoreApplication::translate("NativeSVS", "Invalid parameter color: %1").arg(p.id);
				return false;
			}
			p.minimum = value["min"].toDouble();
			p.maximum = value["max"].toDouble(1);
			p.step = value["step"].toDouble(p.type == "int" ? 1 : 0.01);
			p.writable = writable && value["writable"].toBool(true);
			p.curve = value["curve"].toBool();
			p.visible = value["visible"].toBool(true);
			p.enabled = value["enabled"].toBool(true);
			p.disabledReason = value["disabledReason"].toString();
			p.visibleWhen = value["visibleWhen"].toObject();
			p.choices = value["choices"].toArray();
			QSet<QString> resources;
			if (value.contains("resourceIds"))
			{
				if (p.type != "string" || !value["resourceIds"].isArray() || value["resourceIds"].toArray().isEmpty())
				{
					error = QCoreApplication::translate("NativeSVS", "Invalid resource selector: %1").arg(p.id);
					return false;
				}
				for (const auto& resource : value["resourceIds"].toArray())
				{
					const auto id = resource.toString();
					if (id.isEmpty() || resources.contains(id))
					{
						error = QCoreApplication::translate("NativeSVS", "Invalid or duplicate resource ID: %1")
									.arg(p.id);
						return false;
					}
					resources.insert(id);
					p.resourceIds << id;
				}
			}
			const QString identity = p.scope + "/" + p.id;
			if (p.id.isEmpty() || ids.contains(p.id)
				|| !QStringList{"float", "int", "bool", "enum", "string", "directory-list"}.contains(p.type)
				|| !QStringList{"track", "clip", "note", "phoneme"}.contains(p.scope))
			{
				error = QCoreApplication::translate("NativeSVS", "Invalid or duplicate parameter: %1").arg(identity);
				return false;
			}
			ids.insert(p.id);
			if (p.type == "directory-list" && (p.curve || p.maxItems < 1 || p.maxItems > 128))
			{
				error = QCoreApplication::translate("NativeSVS", "Invalid directory list: %1").arg(identity);
				return false;
			}
			if (p.type == "float" || p.type == "int")
			{
				if (!value["min"].isDouble() || !value["max"].isDouble()
					|| (value.contains("step") && !value["step"].isDouble()))
				{
					error = QCoreApplication::translate("NativeSVS", "Non-numeric range: %1").arg(identity);
					return false;
				}
				if (!std::isfinite(p.minimum) || !std::isfinite(p.maximum) || !std::isfinite(p.step)
					|| p.maximum < p.minimum || p.step <= 0 || !QStringList{"linear", "log"}.contains(p.scale)
					|| (p.scale == "log" && p.minimum <= 0)
					|| (p.type == "int"
						&& (std::floor(p.minimum) != p.minimum || std::floor(p.maximum) != p.maximum
							|| std::floor(p.step) != p.step)))
				{
					error = QCoreApplication::translate("NativeSVS", "Invalid numeric range: %1").arg(identity);
					return false;
				}
			}
			if (p.type == "enum")
			{
				QSet<QString> options;
				for (const auto& choice : p.choices)
				{
					auto id = choice.toObject()["id"].toString();
					if (id.isEmpty() || options.contains(id))
					{
						error = QCoreApplication::translate("NativeSVS", "Invalid enum ID: %1").arg(identity);
						return false;
					}
					options.insert(id);
				}
			}
			if (!p.accepts(p.defaultValue) || !QStringList{"linear", "hermite", "step"}.contains(p.interpolation)
				|| (p.curve && (p.type == "bool" || p.type == "enum" || p.type == "int") && p.interpolation != "step")
				|| (p.curve && p.type == "string"))
			{
				error = QCoreApplication::translate("NativeSVS", "Invalid default or interpolation: %1").arg(identity);
				return false;
			}
			if (p.curve)
			{
				if (p.color.isEmpty())
					p.color
						= curveColors.value(p.id, defaultCurveColors[curveColors.size() % defaultCurveColors.size()]);
				curveColors[p.id] = p.color;
			}
			result.push_back(p);
		}
		return true;
	};
	if (!readParameters("parameters", parsed.parameters, true)
		|| !readParameters("feedbackParameters", parsed.feedbackParameters, false))
		return false;
	for (const auto& language : object["languages"].toArray())
		parsed.languages << language.toString();
	parsed.defaultLanguage = object["defaultLanguage"].toString();
	if (parsed.languages.isEmpty() || parsed.languages.contains(QString{})
		|| !parsed.languages.contains(parsed.defaultLanguage))
	{
		error = QCoreApplication::translate("NativeSVS", "Invalid capability languages");
		return false;
	}
	parsed.noteLanguage = object["noteLanguage"].toBool();
	const auto pitch = object["pitch"].toObject();
	parsed.pitchInput = pitch["input"].toString("none");
	parsed.pitchUnit = pitch["unit"].toString();
	if (!QStringList{"none", "absolute", "offset"}.contains(parsed.pitchInput)
		|| (parsed.pitchInput != "none" && parsed.pitchUnit != "semitone"))
	{
		error = QCoreApplication::translate("NativeSVS", "Unsupported pitch mode/unit");
		return false;
	}
	if (parsed.pitchInput == "offset")
	{
		Curve reference;
		if (!Curve::fromJson(pitch["referencePitch"].toObject(), reference, error) || reference.mode != "absolute"
			|| reference.type != "float" || reference.unit != "semitone" || reference.evaluator.points.empty())
		{
			error = QCoreApplication::translate(
				"NativeSVS", "Offset pitch requires a valid declared absolute referencePitch curve: %1")
						.arg(error);
			return false;
		}
	}
	const auto pronunciation = object["pronunciation"].toObject();
	parsed.parser = pronunciation["parser"].toString();
	parsed.continuation = pronunciation["continuation"].toString();
	parsed.phonemeSetId = pronunciation["phonemeSet"].toString();
	for (const auto& phoneme : pronunciation["phonemes"].toArray())
		parsed.phonemeSet << phoneme.toString();
	QSet<QString> symbols;
	for (const auto& symbol : parsed.phonemeSet)
	{
		if (symbol.isEmpty() || symbols.contains(symbol))
		{
			error = QCoreApplication::translate("NativeSVS", "Invalid or duplicate phoneme symbol");
			return false;
		}
		symbols.insert(symbol);
	}
	for (const auto& resource : pronunciation["dictionaries"].toArray())
		parsed.dictionaryResources << resource.toString();
	if (!parsed.dictionaryResources.isEmpty() && parsed.phonemeSetId.isEmpty())
	{
		error = QCoreApplication::translate("NativeSVS", "Dictionary requires a phoneme set ID");
		return false;
	}
	parsed.phonemeTiming = object["phonemes"].toObject()["timingEditable"].toBool();
	parsed.cancel = object["synthesis"].toObject()["cancel"].toBool();
	parsed.concurrent = object["synthesis"].toObject()["concurrent"].toBool();
	const auto synthesis = object["synthesis"].toObject();
	if (synthesis.contains("segmented") && synthesis["segmented"] != QJsonValue(false))
	{
		const auto segmented = synthesis["segmented"].toObject();
		const double padding = segmented["paddingSeconds"].toDouble(NAN);
		if (segmented["split"].toString() != "rests" || segmented["version"].toDouble() != 1 || !std::isfinite(padding)
			|| padding < 0 || padding > 2)
		{
			error = QCoreApplication::translate("NativeSVS", "Invalid segmented synthesis declaration");
			return false;
		}
	}
	output = std::move(parsed);
	error.clear();
	return true;
}

SelectedValue selectedValue(const QVector<QJsonObject>& values, const QString& id)
{
	SelectedValue result;
	bool first = true, present = false;
	for (const auto& object : values)
	{
		const bool has = object.contains(id);
		if (first)
		{
			first = false;
			present = has;
			result.value = object.value(id);
			result.state = has ? ValueState::Common : ValueState::Unset;
		}
		else if (has != present || (has && object.value(id) != result.value))
		{
			result.state = ValueState::Mixed;
			break;
		}
	}
	return result;
}

bool Dictionary::parse(const QByteArray& bytes, const QStringList& allowed, Dictionary& output, QString& error)
{
	if (bytes.size() > 4 * 1024 * 1024 || QString::fromUtf8(bytes).toUtf8() != bytes)
	{
		error = QCoreApplication::translate("NativeSVS", "Dictionary exceeds limit or is not UTF-8");
		return false;
	}
	QJsonParseError parseError;
	auto document = QJsonDocument::fromJson(bytes, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject())
	{
		error = QCoreApplication::translate("NativeSVS", "Dictionary JSON at byte %1: %2")
					.arg(parseError.offset)
					.arg(parseError.errorString());
		return false;
	}
	const auto root = document.object();
	Dictionary result;
	result.id = root["id"].toString();
	result.version = root["version"].toString();
	result.language = root["language"].toString();
	result.phonemeSet = root["phonemeSet"].toString();
	if (root["schemaVersion"].toInt() != 1 || result.id.isEmpty() || result.version.isEmpty()
		|| result.language.isEmpty() || result.phonemeSet.isEmpty() || !root["entries"].isArray())
	{
		error = QCoreApplication::translate("NativeSVS", "Missing dictionary fields");
		return false;
	}
	int index = 0;
	for (const auto& item : root["entries"].toArray())
	{
		const auto entry = item.toObject();
		const auto text = entry["text"].toString();
		const auto candidates = entry["candidates"].toArray();
		const QString location = QString("entries[%1] (%2): ").arg(index++).arg(text);
		if (text.isEmpty() || result.entries.contains(text) || candidates.isEmpty())
		{
			error = location + QCoreApplication::translate("NativeSVS", "missing/duplicate entry or no candidates");
			return false;
		}
		QSet<QString> readings;
		for (const auto& candidateValue : candidates)
		{
			const auto candidate = candidateValue.toObject();
			const auto reading = candidate["reading"].toString();
			if (reading.isEmpty() || readings.contains(reading) || !candidate["phonemes"].isArray()
				|| candidate["phonemes"].toArray().isEmpty())
			{
				error = location + QCoreApplication::translate("NativeSVS", "invalid or duplicate reading");
				return false;
			}
			readings.insert(reading);
			for (const auto& symbol : candidate["phonemes"].toArray())
				if (!symbol.isString() || !allowed.contains(symbol.toString()))
				{
					error = location
						+ QCoreApplication::translate("NativeSVS", "illegal phoneme %1").arg(symbol.toString());
					return false;
				}
		}
		result.entries[text] = candidates;
	}
	result.hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
	output = std::move(result);
	error.clear();
	return true;
}

QJsonObject Pronunciation::toJson() const
{
	return {{"text", text}, {"source", source}, {"diagnostic", diagnostic}, {"phonemeSet", phonemeSet},
		{"phonemes", QJsonArray::fromStringList(phonemes)}, {"candidates", candidates}, {"generated", generated},
		{"continuation", continuation}};
}

Pronunciation resolvePronunciation(const Note& note, const Capabilities& cap, const QVector<Dictionary>& voice,
	const QVector<Dictionary>& project, const QString& defaultLanguage, const Note* previous,
	const Pronunciation* previousResult)
{
	Pronunciation result;
	result.text = note.lyric;
	const auto language = note.language.isEmpty() ? defaultLanguage : note.language;
	if (!cap.languages.contains(language)
		|| (!cap.noteLanguage && !note.language.isEmpty() && language != defaultLanguage))
	{
		result.diagnostic = "Unsupported note language: " + language;
		return result;
	}
	auto validate = [&] {
		for (const auto& symbol : result.phonemes)
			if (!cap.phonemeSet.contains(symbol))
				result.diagnostic += "Illegal phoneme: " + symbol + "; ";
	};
	if (note.phonemes.contains("symbols"))
	{
		result.source = "manualPhonemes";
		result.generated = true;
		result.phonemeSet = cap.phonemeSetId;
		for (const auto& symbol : note.phonemes["symbols"].toArray())
			result.phonemes << symbol.toString();
		validate();
		return result;
	}
	if (!cap.continuation.isEmpty() && note.lyric == cap.continuation && note.pronunciation.isEmpty())
	{
		result.source = "continuation";
		const bool markerSource = previous && previous->lyric == cap.continuation && previous->pronunciation.isEmpty()
			&& !previous->phonemes.contains("symbols");
		if (!previous || std::abs(previous->tick + previous->duration - note.tick) > 1e-6
			|| (markerSource && (!previousResult || !previousResult->continuation))
			|| (previousResult && (!previousResult->generated || !previousResult->diagnostic.isEmpty())))
			result.diagnostic = "Continuation requires an adjacent resolved source note";
		else
		{
			result.continuation = true;
			result.generated = true;
			if (previousResult)
			{
				result.text = previousResult->text;
				result.phonemes = previousResult->phonemes;
				result.phonemeSet = previousResult->phonemeSet;
			}
		}
		return result;
	}
	auto lookup = [&](const QVector<Dictionary>& dictionaries, const QString& source) {
		for (const auto& dictionary : dictionaries)
		{
			if (dictionary.language != language)
				continue;
			const auto candidates = dictionary.entries[note.lyric].toArray();
			if (candidates.isEmpty())
				continue;
			result.candidates = candidates;
			result.phonemeSet = dictionary.phonemeSet;
			for (const auto& value : candidates)
			{
				const auto candidate = value.toObject();
				if (!note.pronunciation.isEmpty() && candidate["reading"].toString() != note.pronunciation)
					continue;
				result.text = candidate["reading"].toString();
				result.source = note.pronunciation.isEmpty() ? source : "manualPronunciation";
				for (const auto& symbol : candidate["phonemes"].toArray())
					result.phonemes << symbol.toString();
				result.generated = true;
				return true;
			}
		}
		return false;
	};
	if (lookup(project, "projectDictionary") || lookup(voice, "voiceDictionary"))
		return result;
	result.source = note.pronunciation.isEmpty() ? "unknown" : "manualPronunciation";
	result.text = note.pronunciation.isEmpty() ? note.lyric : note.pronunciation;
	result.diagnostic = "Unresolved pronunciation; original text retained";
	return result;
}

}
