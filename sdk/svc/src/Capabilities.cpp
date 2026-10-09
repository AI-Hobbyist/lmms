#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cmath>
#include <cstring>

#include "svc.h"

extern "C" svc_status svc_validate_capabilities(const char* json, size_t count, char* error, size_t capacity)
{
	QString reason;
	if (!json || count > SVC_MAX_PART) { reason = "Missing or oversized capability document"; }
	else
	{
		QJsonParseError parse;
		const auto document = QJsonDocument::fromJson(QByteArray(json, static_cast<int>(count)), &parse);
		const auto root = document.object();
		if (parse.error || !document.isObject() || root.value("schema_version").toInt() != 1
			|| root.value("engine_id").toString().isEmpty() || !root.value("models").isArray()
			|| !root.value("parameters").isArray() || !root.value("limits").isObject())
		{
			reason = "Missing fields or unsupported capability schema version";
		}
		QSet<QString> models;
		for (const auto& entry : root.value("models").toArray())
		{
			const auto model = entry.toObject();
			const auto id = model.value("id").toString();
			if (id.isEmpty() || models.contains(id) || !model.value("name").isString()
				|| !model.value("speakers").isArray())
			{
				reason = "Invalid or duplicate model ID / missing speaker list";
			}
			models.insert(id);
			QSet<QString> speakers;
			for (const auto& speakerEntry : model.value("speakers").toArray())
			{
				const auto speaker = speakerEntry.toObject();
				const auto speakerId = speaker.value("id").toString();
				if (speakerId.isEmpty() || speakers.contains(speakerId) || !speaker.value("name").isString())
				{
					reason = "Invalid speaker metadata";
				}
				speakers.insert(speakerId);
			}
		}
		QSet<QString> parameters;
		for (const auto& entry : root.value("parameters").toArray())
		{
			const auto parameter = entry.toObject();
			const auto id = parameter.value("id").toString();
			const auto type = parameter.value("type").toString();
			if (id.isEmpty() || parameters.contains(id) || !parameter.value("name").isString()
				|| !parameter.value("unit").isString() || !parameter.value("scope").isString()
				|| !parameter.contains("default") || (type != "number" && type != "integer" && type != "enum"))
			{
				reason = "Invalid parameter metadata";
			}
			parameters.insert(id);
			if (type == "number" || type == "integer")
			{
				const auto value = parameter.value("default");
				if (!value.isDouble() || !std::isfinite(value.toDouble())
					|| (type == "integer" && std::floor(value.toDouble()) != value.toDouble()))
				{
					reason = "Invalid numeric default";
				}
				for (const auto& key : {"minimum", "maximum", "step"})
				{
					if (parameter.contains(key) && !parameter.value(key).isDouble())
					{
						reason = "Numeric bound must be a number, never an expression";
					}
				}
				if ((parameter.contains("step") && parameter.value("step").toDouble() <= 0)
					|| (parameter.contains("minimum") && value.toDouble() < parameter.value("minimum").toDouble())
					|| (parameter.contains("maximum") && value.toDouble() > parameter.value("maximum").toDouble()))
				{
					reason = "Default outside bounds or nonpositive step";
				}
			}
			else if (type == "enum")
			{
				QSet<QString> choices;
				bool found = false;
				for (const auto& optionEntry : parameter.value("options").toArray())
				{
					const auto option = optionEntry.toObject();
					const auto optionId = option.value("id").toString();
					if (optionId.isEmpty() || choices.contains(optionId) || !option.value("available").isBool()
						|| !option.value("name").isString()
						|| (!option.value("available").toBool() && !option.value("reason").isString()))
					{
						reason = "Invalid option availability metadata";
					}
					choices.insert(optionId);
					found |= optionId == parameter.value("default").toString();
				}
				if (!found) { reason = "Enum default absent from options"; }
			}
		}
		const auto limits = root.value("limits").toObject();
		for (const auto& key : {"max_upload_bytes", "max_seconds", "min_seconds"})
		{
			if (!limits.value(key).isDouble() || limits.value(key).toDouble() <= 0)
			{
				reason = "Missing or invalid audio limits";
			}
		}
		if (limits.value("min_seconds").toDouble() > limits.value("max_seconds").toDouble())
		{
			reason = "Minimum duration exceeds maximum";
		}
	}
	if (error && capacity)
	{
		const auto bytes = reason.toUtf8();
		const auto copied = std::min(capacity - 1, static_cast<size_t>(bytes.size()));
		std::memcpy(error, bytes.constData(), copied);
		error[copied] = 0;
	}
	return reason.isEmpty() ? SVC_OK : SVC_INVALID;
}
