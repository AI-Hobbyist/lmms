#ifndef LMMS_SVS_CAPABILITIES_H
#define LMMS_SVS_CAPABILITIES_H

#include "SVSModel.h"
#include <QJsonArray>
#include <QJsonValue>

namespace lmms::svs {

struct Parameter
{
	QString id, name, translationKey, group, type, scope, unit, scale, interpolation, disabledReason, color;
	int order = 0;
	int maxItems = 128;
	QJsonValue defaultValue;
	double minimum = 0, maximum = 1, step = 0.01;
	bool writable = true, curve = false, visible = true, enabled = true;
	QJsonArray choices;
	QStringList resourceIds;
	QJsonObject visibleWhen;
	bool accepts(const QJsonValue&) const;
	bool isVisible(const QJsonObject& context) const;
};

// A single base-value lookup for the editor and the synthesis snapshot.
inline QJsonValue parameterBase(
	const Parameter& p, const QJsonObject& track, const QJsonObject& clip, const QJsonObject& globals)
{
	const auto values = p.scope == "track" ? track : p.scope == "clip" ? clip : globals;
	const auto value = values.value(p.id);
	return p.accepts(value) ? value : p.defaultValue;
}
inline bool globalParameter(const Parameter& p)
{
	const bool numeric = p.type == "float" || p.type == "int";
	return (p.scope == "track" || p.scope == "clip" || p.scope == "note")
		&& (p.curve ? (numeric || p.type == "bool" || p.type == "enum")
					: numeric && (p.scope == "track" || p.scope == "clip"));
}

struct Capabilities
{
	QVector<Parameter> parameters, feedbackParameters;
	QStringList languages, phonemeSet;
	QString defaultLanguage, pitchInput, pitchUnit, parser, continuation, phonemeSetId;
	QStringList dictionaryResources;
	bool noteLanguage = false, phonemeTiming = false, cancel = false, concurrent = false;
	QJsonObject original;
	const Parameter* parameter(const QString& id, const QString& scope) const;
	static bool parse(const QJsonObject&, Capabilities&, QString& error);
};

enum class ValueState
{
	Unset,
	Common,
	Mixed
};
struct SelectedValue
{
	ValueState state = ValueState::Unset;
	QJsonValue value;
};
SelectedValue selectedValue(const QVector<QJsonObject>& values, const QString& id);

struct Pronunciation
{
	QString text, source, diagnostic, phonemeSet;
	QStringList phonemes;
	QJsonArray candidates;
	bool generated = false, continuation = false;
	QJsonObject toJson() const;
};

class Dictionary
{
public:
	QString id, version, language, phonemeSet, hash;
	QJsonObject entries;
	static bool parse(const QByteArray&, const QStringList& allowedPhonemes, Dictionary&, QString& error);
};

Pronunciation resolvePronunciation(const Note&, const Capabilities&, const QVector<Dictionary>& voice,
	const QVector<Dictionary>& project, const QString& language, const Note* previous = nullptr,
	const Pronunciation* previousResult = nullptr);

}
#endif
