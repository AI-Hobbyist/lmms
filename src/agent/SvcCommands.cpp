#include "SvcCommands.h"

#include "agent/CommandBus.h"
#include "CommandTargets.h"
#include "DiscoverySupport.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <cmath>

#include "Engine.h"
#include "SVCChunking.h"
#include "SVCClip.h"
#include "SVCCatalog.h"
#include "SVCConversion.h"
#include "SVCPlayback.h"
#include "SVCTrack.h"
#include "Song.h"
#include "TimePos.h"

namespace lmms::agent {
namespace {

CommandResult invalidArguments(const QString& message)
{
	return CommandResult::failure("invalid_arguments", message);
}

CommandResult engineUnavailable()
{
	return CommandResult::failure("engine_unavailable", "The song engine is unavailable.");
}

bool readString(const QJsonObject& arguments, const char* name, QString& value, QString& error, bool required)
{
	if (!arguments.contains(name))
	{
		if (required)
		{
			error = QStringLiteral("'%1' is required.").arg(QString::fromUtf8(name));
			return false;
		}
		value.clear();
		return true;
	}
	const auto item = arguments.value(name);
	if (!item.isString())
	{
		error = QStringLiteral("'%1' must be a string.").arg(QString::fromUtf8(name));
		return false;
	}
	value = item.toString();
	if (required && value.isEmpty())
	{
		error = QStringLiteral("'%1' must not be empty.").arg(QString::fromUtf8(name));
		return false;
	}
	return true;
}

bool readInteger(const QJsonObject& arguments, const char* name, int& value, QString& error, bool required)
{
	if (!arguments.contains(name))
	{
		if (required)
		{
			error = QStringLiteral("'%1' is required.").arg(QString::fromUtf8(name));
			return false;
		}
		return true;
	}
	const auto item = arguments.value(name);
	if (!item.isDouble() || std::floor(item.toDouble()) != item.toDouble())
	{
		error = QStringLiteral("'%1' must be an integer.").arg(QString::fromUtf8(name));
		return false;
	}
	value = item.toInt();
	return true;
}

bool readNumber(const QJsonObject& arguments, const char* name, double& value, QString& error)
{
	if (!arguments.contains(name))
	{
		return true;
	}
	const auto item = arguments.value(name);
	if (!item.isDouble())
	{
		error = QStringLiteral("'%1' must be a number.").arg(QString::fromUtf8(name));
		return false;
	}
	value = item.toDouble();
	return true;
}

bool resolveSvcTrack(const QJsonObject& arguments, SVCTrack*& track, int& trackIndex, CommandResult& failure)
{
	auto* song = Engine::getSong();
	if (song == nullptr)
	{
		failure = engineUnavailable();
		return false;
	}
	Track* base = nullptr;
	if (!resolveTrack(song, arguments, base, trackIndex, failure))
	{
		return false;
	}
	track = dynamic_cast<SVCTrack*>(base);
	if (track == nullptr)
	{
		failure = CommandResult::failure("wrong_track_type", "The selected track is not an SVC track.");
		return false;
	}
	return true;
}

bool resolveSvcClip(const QJsonObject& arguments, SVCTrack*& track, SVCClip*& clip, int& trackIndex, int& clipIndex,
	CommandResult& failure)
{
	auto* song = Engine::getSong();
	if (song == nullptr)
	{
		failure = engineUnavailable();
		return false;
	}
	ClipAddress address;
	if (!resolveClip(song, arguments, address, failure))
	{
		return false;
	}
	track = dynamic_cast<SVCTrack*>(address.track);
	clip = dynamic_cast<SVCClip*>(address.clip);
	if (track == nullptr || clip == nullptr)
	{
		failure = CommandResult::failure("wrong_clip_type", "The selected clip is not an SVC clip.");
		return false;
	}
	trackIndex = address.trackIndex;
	clipIndex = address.clipIndex;
	return true;
}

QJsonObject chunkJson(const svc::ChunkConfig& config)
{
	return {{"silenceThresholdDbfs", config.silenceThresholdDbfs},
		{"lengthThresholdSeconds", config.lengthThresholdSeconds},
		{"forcedChunkSeconds", config.forcedChunkSeconds}};
}

QJsonObject clipJson(SVCClip* clip, int trackIndex, int clipIndex)
{
	QJsonObject result{{"path", clipPath(trackIndex, clipIndex, clip->getTrack())}, {"index", clipIndex},
		{"type", "svc"}, {"name", clip->name()}, {"start", clip->startPosition().getTicks()},
		{"length", clip->length().getTicks()}, {"status", clip->status()},
		{"sourceFile", clip->sourceFile()}, {"conversionComplete", clip->conversionComplete()},
		{"conversionFailed", clip->conversionFailed()}, {"cacheReferences", clip->cacheReferences()}};
	if (const auto playback = clip->playback())
	{
		result.insert("progress", playback->progress());
		result.insert("finished", playback->finished());
		const auto snapshot = playback->snapshot();
		if (snapshot && snapshot->source)
		{
			result.insert("sourceRate", static_cast<int>(snapshot->source->rate));
			result.insert("sourceFrames", static_cast<double>(snapshot->source->frames()));
		}
	}
	return result;
}

QJsonObject selectionModel(const QJsonObject& selection)
{
	QJsonObject result;
	const auto profile = svc::Catalog::instance().engine(selection.value("engine_id").toString());
	result.insert("engine",
		QJsonObject{{"id", profile.id}, {"name", profile.name},
			{"status", svc::Catalog::instance().status(profile.id)},
			{"connected", profile.api != nullptr}});
	for (const auto& entry : profile.capabilities.value("models").toArray())
	{
		const auto model = entry.toObject();
		if (model.value("id") == selection.value("model_id"))
		{
			result.insert("model", model);
			result.insert("parameters", svc::parameterDefinitions(profile, model));
			break;
		}
	}
	return result;
}

QJsonObject trackJson(SVCTrack* track, int trackIndex)
{
	const auto selection = track->selection();
	auto result = selectionModel(selection);
	result.insert("path", trackPath(trackIndex, track));
	result.insert("index", trackIndex);
	result.insert("type", "svc");
	result.insert("name", track->name());
	result.insert("muted", track->isMuted());
	result.insert("solo", track->isSolo());
	result.insert("height", track->getHeight());
	result.insert("color", track->color() ? QJsonValue(track->color()->name()) : QJsonValue());
	result.insert("volume", track->volumeModel()->value());
	result.insert("panning", track->panningModel()->value());
	result.insert("mixerChannel", track->mixerChannelModel()->value());
	result.insert("selection", selection);
	result.insert("chunkConfig", chunkJson(track->chunkConfig()));
	QJsonArray clips;
	for (std::size_t index = 0; index < track->getClips().size(); ++index)
	{
		clips.append(clipJson(static_cast<SVCClip*>(track->getClips()[index]), trackIndex,
			static_cast<int>(index)));
	}
	result.insert("clips", clips);
	return result;
}

bool normalizeSelection(const QString& engineId, const QString& modelId, const QString& weightId,
	const QString& speakerId, const QJsonObject& parameters, QJsonObject& normalized, CommandResult& failure)
{
	auto& catalog = svc::Catalog::instance();
	const auto profile = catalog.engine(engineId);
	if (profile.id.isEmpty())
	{
		failure = CommandResult::failure("svc_engine_not_found", "The requested SVC engine is not installed.");
		return false;
	}
	if (profile.api == nullptr)
	{
		failure = CommandResult::failure("svc_engine_offline", "The selected SVC engine is not connected.",
			catalog.status(profile.id));
		return false;
	}
	QJsonObject model;
	for (const auto& entry : profile.capabilities.value("models").toArray())
	{
		const auto candidate = entry.toObject();
		if (candidate.value("id") == modelId)
		{
			model = candidate;
			break;
		}
	}
	if (model.isEmpty())
	{
		failure = CommandResult::failure("svc_model_not_found", "The requested SVC model is not available.");
		return false;
	}
	QJsonObject saved{{"engine_id", engineId}, {"model_id", modelId}};
	const auto weights = model.value("weights").toArray();
	if (!weightId.isEmpty())
	{
		saved.insert("weight_id", weightId);
	}
	else if (!weights.isEmpty())
	{
		if (weights.size() == 1 && !model.contains("parameters"))
		{
			saved.insert("weight_id", weights.first().toObject().value("id"));
		}
		else
		{
			failure = CommandResult::failure(
				"svc_selection_ambiguous", "Select a weight_id for this SVC model.");
			return false;
		}
	}
	const auto speakers = model.value("speakers").toArray();
	const bool multiSpeaker = model.value("multi_speaker").toBool(speakers.size() > 1);
	if (!speakerId.isEmpty())
	{
		saved.insert("speaker_id", speakerId);
	}
	else if (!model.contains("fixed_speaker_id") && !speakers.isEmpty())
	{
		if (multiSpeaker && speakers.size() > 1)
		{
			failure = CommandResult::failure(
				"svc_selection_ambiguous", "Select a speaker_id for this SVC model.");
			return false;
		}
		saved.insert("speaker_id", speakers.first().toObject().value("id"));
	}
	if (!parameters.isEmpty())
	{
		saved.insert("parameters", parameters);
	}
	QString error;
	normalized = catalog.requestSelection(saved, error);
	if (normalized.isEmpty())
	{
		failure = CommandResult::failure("svc_selection_invalid", error);
		return false;
	}
	return true;
}

CommandResult executeCreateTrack(const QJsonObject& arguments)
{
	auto* song = Engine::getSong();
	if (song == nullptr)
	{
		return engineUnavailable();
	}
	QString name;
	QString error;
	if (!readString(arguments, "name", name, error, false))
	{
		return invalidArguments(error);
	}
	int index = static_cast<int>(song->tracks().size());
	if (!readInteger(arguments, "index", index, error, false))
	{
		return invalidArguments(error);
	}
	if (index < 0 || index > static_cast<int>(song->tracks().size()))
	{
		return invalidArguments("'index' must be an insertion position in the song.");
	}
	QJsonObject selection;
	if (arguments.contains("selection"))
	{
		if (!arguments.value("selection").isObject())
		{
			return invalidArguments("'selection' must be an object.");
		}
		selection = arguments.value("selection").toObject();
	}

	auto* track = Track::create(Track::Type::SVC, song);
	if (track == nullptr)
	{
		return CommandResult::failure("track_creation_failed", "LMMS could not create the SVC track.");
	}
	song->moveTrack(track, index);
	auto* svcTrack = static_cast<SVCTrack*>(track);
	if (!name.isEmpty())
	{
		svcTrack->setName(name);
	}
	if (!selection.isEmpty())
	{
		QJsonObject normalized;
		CommandResult failure;
		if (!normalizeSelection(selection.value("engine_id").toString(), selection.value("model_id").toString(),
				selection.value("weight_id").toString(), selection.value("speaker_id").toString(),
				selection.value("parameters").toObject(), normalized, failure))
		{
			delete track;
			return failure;
		}
		if (!svcTrack->setSelection(normalized))
		{
			delete track;
			return CommandResult::failure("svc_selection_rejected", "The SVC track rejected the selection.");
		}
	}
	song->setModified();
	return CommandResult::success(trackJson(svcTrack, indexOfTrack(track)));
}

CommandResult executeCreateClip(const QJsonObject& arguments)
{
	SVCTrack* track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if (!resolveSvcTrack(arguments, track, trackIndex, failure))
	{
		return failure;
	}
	QString name;
	QString error;
	if (!readString(arguments, "name", name, error, false))
	{
		return invalidArguments(error);
	}
	int position = 0;
	int length = TimePos::ticksPerBar();
	if (!readInteger(arguments, "position", position, error, false)
		|| !readInteger(arguments, "length", length, error, false))
	{
		return invalidArguments(error);
	}
	if (position < 0 || length <= 0 || position > MaxSongLength || length > MaxSongLength - position)
	{
		return invalidArguments("The clip position and length must fit within the maximum song length.");
	}
	auto* clip = dynamic_cast<SVCClip*>(track->createClip(TimePos(position)));
	if (clip == nullptr)
	{
		return CommandResult::failure("clip_creation_failed", "LMMS could not create the SVC clip.");
	}
	clip->changeLength(TimePos(length));
	if (!name.isEmpty())
	{
		clip->setName(name);
	}
	Engine::getSong()->setModified();
	return CommandResult::success(clipJson(clip, trackIndex, indexOfClip(track, clip)));
}

CommandResult executeSetModel(const QJsonObject& arguments)
{
	SVCTrack* track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if (!resolveSvcTrack(arguments, track, trackIndex, failure))
	{
		return failure;
	}
	QString engineId;
	QString modelId;
	QString weightId;
	QString speakerId;
	QString error;
	if (!readString(arguments, "engine_id", engineId, error, true)
		|| !readString(arguments, "model_id", modelId, error, true)
		|| !readString(arguments, "weight_id", weightId, error, false)
		|| !readString(arguments, "speaker_id", speakerId, error, false))
	{
		return invalidArguments(error);
	}
	QJsonObject parameters;
	if (arguments.contains("parameters"))
	{
		if (!arguments.value("parameters").isObject())
		{
			return invalidArguments("'parameters' must be an object.");
		}
		parameters = arguments.value("parameters").toObject();
	}
	QJsonObject normalized;
	if (!normalizeSelection(engineId, modelId, weightId, speakerId, parameters, normalized, failure))
	{
		return failure;
	}
	if (!track->setSelection(normalized))
	{
		return CommandResult::failure("svc_selection_rejected", "The SVC track rejected the selection.");
	}
	return CommandResult::success(QJsonObject{{"path", trackPath(trackIndex, track)},
		{"selection", normalized}, {"model", selectionModel(normalized).value("model")},
		{"parameters", normalized.value("parameters")}});
}

CommandResult executeGetTrack(const QJsonObject& arguments)
{
	SVCTrack* track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if (!resolveSvcTrack(arguments, track, trackIndex, failure))
	{
		return failure;
	}
	return CommandResult::success(trackJson(track, trackIndex));
}

CommandResult executeSetSourceFile(const QJsonObject& arguments)
{
	SVCTrack* track = nullptr;
	SVCClip* clip = nullptr;
	int trackIndex = -1;
	int clipIndex = -1;
	CommandResult failure;
	if (!resolveSvcClip(arguments, track, clip, trackIndex, clipIndex, failure))
	{
		return failure;
	}
	QString path;
	QString error;
	if (!readString(arguments, "path", path, error, true))
	{
		return invalidArguments(error);
	}
	const QFileInfo file(path);
	if (!file.isFile() || !file.isReadable())
	{
		return CommandResult::failure("file_not_found", "The requested source audio is not readable.");
	}
	if (!clip->setSourceFile(file.absoluteFilePath()))
	{
		return CommandResult::failure("svc_source_decode_failed", clip->status());
	}
	auto result = clipJson(clip, trackIndex, clipIndex);
	result.insert("sourceFile", file.absoluteFilePath());
	return CommandResult::success(result);
}

CommandResult executeSetChunkConfig(const QJsonObject& arguments)
{
	SVCTrack* track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if (!resolveSvcTrack(arguments, track, trackIndex, failure))
	{
		return failure;
	}
	if (!arguments.contains("silenceThresholdDbfs") && !arguments.contains("lengthThresholdSeconds")
		&& !arguments.contains("forcedChunkSeconds"))
	{
		return invalidArguments("Provide at least one chunk configuration field.");
	}
	auto config = track->chunkConfig();
	QString error;
	if (!readNumber(arguments, "silenceThresholdDbfs", config.silenceThresholdDbfs, error)
		|| !readNumber(arguments, "lengthThresholdSeconds", config.lengthThresholdSeconds, error)
		|| !readNumber(arguments, "forcedChunkSeconds", config.forcedChunkSeconds, error))
	{
		return invalidArguments(error);
	}
	const auto validation = config.validate();
	if (!validation.isEmpty())
	{
		return invalidArguments(validation);
	}
	if (!track->setChunkConfig(config))
	{
		return CommandResult::failure("svc_chunk_config_invalid", "The SVC chunk configuration was rejected.");
	}
	return CommandResult::success(QJsonObject{{"path", trackPath(trackIndex, track)},
		{"chunkConfig", chunkJson(track->chunkConfig())}});
}

CommandResult executeConvert(const QJsonObject& arguments)
{
	SVCTrack* track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if (!resolveSvcTrack(arguments, track, trackIndex, failure))
	{
		return failure;
	}
	if (CommandBus::instance().batchDepth() != 0)
	{
		return CommandResult::failure(
			"async_work_in_batch", "Run SVC conversion after the project transaction has committed.");
	}
	if (arguments.value("dryRun").toBool())
	{
		return CommandResult::success(QJsonObject{{"wouldConvert", true}});
	}
	auto& service = svc::ConversionService::instance();
	QJsonArray statuses;
	if (arguments.contains("clip"))
	{
		SVCClip* clip = nullptr;
		int clipIndex = -1;
		if (!resolveSvcClip(arguments, track, clip, trackIndex, clipIndex, failure))
		{
			return failure;
		}
		service.renderClip(clip);
		statuses.append(clipJson(clip, trackIndex, clipIndex));
	}
	else
	{
		service.render(track);
		for (std::size_t index = 0; index < track->getClips().size(); ++index)
		{
			statuses.append(clipJson(static_cast<SVCClip*>(track->getClips()[index]), trackIndex,
				static_cast<int>(index)));
		}
	}
	return CommandResult::success(
		QJsonObject{{"path", trackPath(trackIndex, track)}, {"clips", statuses}});
}

CommandResult executeCancel(const QJsonObject& arguments)
{
	SVCTrack* track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if (!resolveSvcTrack(arguments, track, trackIndex, failure))
	{
		return failure;
	}
	if (CommandBus::instance().batchDepth() != 0)
	{
		return CommandResult::failure(
			"async_work_in_batch", "Cancel SVC conversion outside the project transaction.");
	}
	if (arguments.value("dryRun").toBool())
	{
		return CommandResult::success(QJsonObject{{"wouldCancel", true}});
	}
	svc::ConversionService::instance().cancel(track);
	return CommandResult::success(QJsonObject{{"path", trackPath(trackIndex, track)}, {"cancelled", true}});
}

CommandResult executeGetStatus(const QJsonObject& arguments)
{
	if (arguments.contains("clip"))
	{
		SVCTrack* track = nullptr;
		SVCClip* clip = nullptr;
		int trackIndex = -1;
		int clipIndex = -1;
		CommandResult failure;
		if (!resolveSvcClip(arguments, track, clip, trackIndex, clipIndex, failure))
		{
			return failure;
		}
		return CommandResult::success(clipJson(clip, trackIndex, clipIndex));
	}
	SVCTrack* track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if (!resolveSvcTrack(arguments, track, trackIndex, failure))
	{
		return failure;
	}
	return CommandResult::success(trackJson(track, trackIndex));
}

QJsonObject trackSchema()
{
	return {{"track",
				QJsonObject{{"anyOf", QJsonArray{discovery::integerSchema(), discovery::stringSchema()}},
					{"description", "Track index, unique name, or song/track:N path."}}},
		{"parent", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"song", "pattern"}}}}};
}

QJsonObject clipSchema()
{
	auto properties = trackSchema();
	properties.insert("clip", discovery::integerSchema());
	return properties;
}

void registerCommand(CommandBus& commandBus, const QString& name, const QString& summary,
	const QJsonObject& schema, Mutability mutability, TxScope scope, CommandHandler handler)
{
	CommandDescriptor descriptor;
	descriptor.name = name;
	descriptor.summary = summary;
	descriptor.argsSchema = schema;
	descriptor.mutability = mutability;
	descriptor.scope = scope;
	descriptor.handler = std::move(handler);
	commandBus.registerCommand(descriptor);
}

} // namespace

void registerSvcCommands(CommandBus& commandBus)
{
	QJsonObject createTrackProperties;
	createTrackProperties.insert("name", discovery::stringSchema());
	createTrackProperties.insert("index", discovery::integerSchema());
	auto selectionProperties = discovery::objectSchema(
		QJsonObject{{"engine_id", discovery::stringSchema()}, {"model_id", discovery::stringSchema()},
			{"weight_id", discovery::stringSchema()}, {"speaker_id", discovery::stringSchema()},
			{"parameters", QJsonObject{{"type", "object"}}}},
		QJsonArray{"engine_id", "model_id"});
	createTrackProperties.insert("selection", selectionProperties);
	registerCommand(commandBus, "svc.createTrack",
		"Create an SVC track, optionally selecting an engine and model.",
		discovery::objectSchema(createTrackProperties), Mutability::Mutating, TxScope::Single, executeCreateTrack);

	QJsonObject createClipProperties = trackSchema();
	createClipProperties.insert("position", discovery::integerSchema());
	createClipProperties.insert("length", discovery::integerSchema());
	createClipProperties.insert("name", discovery::stringSchema());
	registerCommand(commandBus, "svc.createClip", "Create an SVC clip on an SVC track.",
		discovery::objectSchema(createClipProperties, QJsonArray{"track"}), Mutability::Mutating, TxScope::Single,
		executeCreateClip);

	QJsonObject setModelProperties = trackSchema();
	setModelProperties.insert("engine_id", discovery::stringSchema());
	setModelProperties.insert("model_id", discovery::stringSchema());
	setModelProperties.insert("weight_id", discovery::stringSchema());
	setModelProperties.insert("speaker_id", discovery::stringSchema());
	setModelProperties.insert("parameters", QJsonObject{{"type", "object"}});
	registerCommand(commandBus, "svc.setModel",
		"Select and normalize an SVC model, weight, speaker and parameters.",
		discovery::objectSchema(setModelProperties, QJsonArray{"track", "engine_id", "model_id"}),
		Mutability::Mutating, TxScope::Single, executeSetModel);

	registerCommand(commandBus, "svc.getTrack", "Return SVC selection, chunk config and clip status.",
		discovery::objectSchema(trackSchema(), QJsonArray{"track"}), Mutability::ReadOnly, TxScope::None,
		executeGetTrack);

	QJsonObject sourceProperties = clipSchema();
	sourceProperties.insert("path", discovery::stringSchema());
	registerCommand(commandBus, "svc.setSourceFile", "Decode and attach a source audio file to an SVC clip.",
		discovery::objectSchema(sourceProperties, QJsonArray{"track", "clip", "path"}), Mutability::Mutating,
		TxScope::Single, executeSetSourceFile);

	QJsonObject chunkProperties = trackSchema();
	chunkProperties.insert("silenceThresholdDbfs", QJsonObject{{"type", "number"}, {"minimum", -120}, {"maximum", 0}});
	chunkProperties.insert("lengthThresholdSeconds", QJsonObject{{"type", "number"}, {"minimum", 0}});
	chunkProperties.insert("forcedChunkSeconds", QJsonObject{{"type", "number"}, {"minimum", 0}});
	registerCommand(commandBus, "svc.setChunkConfig", "Set SVC silence, length and forced chunk thresholds.",
		discovery::objectSchema(chunkProperties, QJsonArray{"track"}), Mutability::Mutating, TxScope::Single,
		executeSetChunkConfig);

	registerCommand(commandBus, "svc.convert", "Start asynchronous SVC conversion for a clip or track.",
		discovery::objectSchema(clipSchema(), QJsonArray{"track"}), Mutability::Mutating, TxScope::None,
		executeConvert);
	registerCommand(commandBus, "svc.cancel", "Cancel SVC conversion for a track.",
		discovery::objectSchema(clipSchema(), QJsonArray{"track"}), Mutability::Mutating, TxScope::None,
		executeCancel);
	registerCommand(commandBus, "svc.getStatus", "Return SVC clip or track conversion status.",
		discovery::objectSchema(clipSchema(), QJsonArray{"track"}), Mutability::ReadOnly, TxScope::None,
		executeGetStatus);
}

} // namespace lmms::agent
