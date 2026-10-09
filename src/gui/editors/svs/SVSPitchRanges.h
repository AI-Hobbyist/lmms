#ifndef LMMS_SVS_PITCH_RANGES_H
#define LMMS_SVS_PITCH_RANGES_H
#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
namespace lmms::gui {
struct SVSPitchRanges
{
	QSet<int> available, comfortable, weak;
	QString availableText, comfortableText, weakText;
	QStringList invalid;
	bool present = false;
	static int pitch(const QString& text)
	{
		static const QRegularExpression note("^([A-Ga-g])([#b♯♭]?)(-?[0-9]+)$");
		const auto match = note.match(text.trimmed());
		if (!match.hasMatch())
			return -1;
		const auto letter = match.captured(1).toUpper();
		const int semitone = letter == "C" ? 0
			: letter == "D"				   ? 2
			: letter == "E"				   ? 4
			: letter == "F"				   ? 5
			: letter == "G"				   ? 7
			: letter == "A"				   ? 9
										   : 11;
		bool ok = false;
		const auto octave = match.captured(3).toInt(&ok);
		if (!ok || octave < -1 || octave > 9)
			return -1;
		const auto accidental = match.captured(2);
		const int value = (octave + 1) * 12 + semitone
			+ (accidental == "#" || accidental == "♯"		 ? 1
					: accidental == "b" || accidental == "♭" ? -1
															 : 0);
		return value >= 0 && value <= 127 ? value : -1;
	}
	static SVSPitchRanges fromMetadata(const QJsonObject& metadata)
	{
		SVSPitchRanges result;
		const auto data = metadata["metadata"].toObject()["pitchRanges"].toObject();
		if (data.isEmpty())
			return result;
		result.present = true;
		auto parse = [&](const QJsonValue& source, QSet<int>& values) {
			QStringList texts;
			if (source.isArray())
			{
				for (const auto& value : source.toArray())
				{
					if (value.isString())
						texts << value.toString();
					else
						result.invalid << "Non-string pitch range";
				}
			}
			else if (source.isString())
				texts << source.toString();
			else if (!source.isUndefined())
				result.invalid << "Invalid pitch range";
			for (const auto& text : texts)
				for (const auto& token : text.split(QRegularExpression("[,，;；]"), Qt::SkipEmptyParts))
				{
					auto range = token.trimmed();
					range.remove(QRegularExpression("\\s+"));
					const int single = pitch(range);
					if (single >= 0)
					{
						values.insert(single);
						continue;
					}
					static const QRegularExpression pair("^([A-Ga-g][#b♯♭]?-?[0-9]+)[-–~～]([A-Ga-g][#b♯♭]?-?[0-9]+)$");
					const auto match = pair.match(range);
					const int first = match.hasMatch() ? pitch(match.captured(1)) : -1,
							  last = match.hasMatch() ? pitch(match.captured(2)) : -1;
					if (first < 0 || last < first)
					{
						result.invalid << token.trimmed();
						continue;
					}
					for (int value = first; value <= last; ++value)
						values.insert(value);
				}
			return texts.join(", ");
		};
		result.availableText = parse(data["available"], result.available);
		result.comfortableText = parse(data["comfort"], result.comfortable);
		result.weakText = parse(data["weak"], result.weak);
		return result;
	}
	QColor keyColor(QColor base, int value) const
	{
		if (!present || (available.isEmpty() && comfortable.isEmpty() && weak.isEmpty()))
			return base;
		const bool comfortableKey = comfortable.contains(value) && !weak.contains(value);
		const double factor = weak.contains(value) ? .65 : comfortableKey ? 1. : available.contains(value) ? .88 : .45;
		auto component = [&](int channel) {
			return comfortableKey ? int(channel + (255 - channel) * .25) : int(channel * factor);
		};
		return QColor(component(base.red()), component(base.green()), component(base.blue()), base.alpha());
	}
};
}
#endif
