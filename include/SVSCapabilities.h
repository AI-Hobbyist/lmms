#ifndef LMMS_SVS_CAPABILITIES_H
#define LMMS_SVS_CAPABILITIES_H

#include "SVSModel.h"
#include <QJsonArray>
#include <QJsonValue>

namespace lmms::svs {

struct Parameter {
 QString id, name, translationKey, group, type, scope, unit, scale, interpolation, disabledReason, color;
 int order = 0;
 QJsonValue defaultValue;
 double minimum = 0, maximum = 1, step = 0.01;
 bool writable = true, curve = false, visible = true, enabled = true;
 QJsonArray choices;
 QStringList resourceIds;
 QJsonObject visibleWhen;
 bool accepts(const QJsonValue&) const;
 bool isVisible(const QJsonObject& context) const;
};

struct Capabilities {
 QVector<Parameter> parameters, feedbackParameters;
 QStringList languages, phonemeSet;
 QString defaultLanguage, pitchInput, pitchUnit, parser, continuation, phonemeSetId;
 QStringList dictionaryResources;
 bool noteLanguage = false, phonemeTiming = false, cancel = false, concurrent = false;
 QJsonObject original;
 const Parameter* parameter(const QString& id, const QString& scope) const;
 static bool parse(const QJsonObject&, Capabilities&, QString& error);
};

enum class ValueState { Unset, Common, Mixed };
struct SelectedValue { ValueState state = ValueState::Unset; QJsonValue value; };
SelectedValue selectedValue(const QVector<QJsonObject>& values, const QString& id);

struct Pronunciation {
 QString text, source, diagnostic, phonemeSet;
 QStringList phonemes;
 QJsonArray candidates;
 bool generated = false, continuation = false;
 QJsonObject toJson() const;
};

class Dictionary {
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
