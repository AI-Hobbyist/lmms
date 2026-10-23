#include "SvsCommands.h"

#include "agent/CommandBus.h"
#include "CommandTargets.h"
#include "DiscoverySupport.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <cmath>

#include "Engine.h"
#include "SVSCapabilities.h"
#include "SVSClip.h"
#include "SVSLyricEditor.h"
#include "SVSModel.h"
#include "SVSTrack.h"
#include "Song.h"
#include "TimePos.h"

namespace lmms::agent {
namespace {

using discovery::Page;

template <typename T>
QVector<T> pageSlice(const QVector<T>& values, const Page& page)
{
	const qint64 offset = static_cast<qint64>(page.page) * page.pageSize;
	if (offset >= values.size())
	{
		return {};
	}
	const int begin = static_cast<int>(offset);
	const int end = std::min(begin + page.pageSize, static_cast<int>(values.size()));
	return values.mid(begin, end - begin);
}

CommandResult invalidArguments(const QString& message)
{
	return CommandResult::failure("invalid_arguments", message);
}

CommandResult engineUnavailable()
{
	return CommandResult::failure("engine_unavailable", "The song engine is unavailable.");
}

CommandResult capabilitiesPending()
{
	return CommandResult::failure("svs_capabilities_pending",
		"The SVS voice capabilities are still loading.", "Retry after svs.getTrack reports capabilitiesReady true.");
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

bool readBool(const QJsonObject& arguments, const char* name, bool& value, QString& error)
{
	if (!arguments.contains(name))
	{
		return true;
	}
	const auto item = arguments.value(name);
	if (!item.isBool())
	{
		error = QStringLiteral("'%1' must be a boolean.").arg(QString::fromUtf8(name));
		return false;
	}
	value = item.toBool();
	return true;
}

bool readNumber(const QJsonObject& arguments, const char* name, double& value, QString& error, bool required)
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
	if (!item.isDouble())
	{
		error = QStringLiteral("'%1' must be a number.").arg(QString::fromUtf8(name));
		return false;
	}
	value = item.toDouble();
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

bool readStringArray(const QJsonObject& arguments, const char* name, QStringList& values, QString& error, bool required)
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
	if (!item.isArray())
	{
		error = QStringLiteral("'%1' must be an array of strings.").arg(QString::fromUtf8(name));
		return false;
	}
	values.clear();
	for (const auto& entry : item.toArray())
	{
		if (!entry.isString())
		{
			error = QStringLiteral("'%1' must contain only strings.").arg(QString::fromUtf8(name));
			return false;
		}
		values.append(entry.toString());
	}
	if (required && values.isEmpty())
	{
		error = QStringLiteral("'%1' must not be empty.").arg(QString::fromUtf8(name));
		return false;
	}
	return true;
}

bool readRange(const QJsonObject& arguments, bool& present, double& start, double& end, QString& error)
{
	present = arguments.contains("range");
	if (!present)
	{
		return true;
	}
	const auto range = arguments.value("range");
	if (!range.isObject())
	{
		error = "'range' must be an object with start and end.";
		return false;
	}
	const auto object = range.toObject();
	if (!object.value("start").isDouble() || !object.value("end").isDouble())
	{
		error = "'range.start' and 'range.end' must be numbers.";
		return false;
	}
	start = object.value("start").toDouble();
	end = object.value("end").toDouble();
	if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || start > end)
	{
		error = "'range' must be finite with 0 <= start <= end.";
		return false;
	}
	return true;
}

bool resolveSvsTrack(const QJsonObject& arguments, SVSTrack*& track, int& trackIndex, CommandResult& failure)
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
	track = dynamic_cast<SVSTrack*>(base);
	if (track == nullptr)
	{
		failure = CommandResult::failure("wrong_track_type", "The selected track is not an SVS track.");
		return false;
	}
	return true;
}

bool resolveSvsClip(const QJsonObject& arguments, SVSTrack*& track, SVSClip*& clip, int& trackIndex, int& clipIndex,
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
	track = dynamic_cast<SVSTrack*>(address.track);
	clip = dynamic_cast<SVSClip*>(address.clip);
	if (track == nullptr || clip == nullptr)
	{
		failure = CommandResult::failure("wrong_clip_type", "The selected clip is not an SVS clip.");
		return false;
	}
	trackIndex = address.trackIndex;
	clipIndex = address.clipIndex;
	return true;
}

bool validateNote(const svs::Note& note, QString& error)
{
	if (!std::isfinite(note.tick) || note.tick < 0)
	{
		error = "Note tick must be a finite non-negative number.";
		return false;
	}
	if (!std::isfinite(note.duration) || note.duration <= 0)
	{
		error = "Note duration must be a finite positive number.";
		return false;
	}
	if (!std::isfinite(note.pitch) || note.pitch < 0 || note.pitch > 127)
	{
		error = "Note pitch must be between 0 and 127.";
		return false;
	}
	if (note.tick + note.duration > MaxSongLength)
	{
		error = "The note exceeds the maximum song length.";
		return false;
	}
	return true;
}

bool applyNoteFields(const QJsonObject& object, svs::Note& note, bool requireCore, QString& error)
{
	if (object.contains("id"))
	{
		if (!object.value("id").isString())
		{
			error = "'id' must be a string.";
			return false;
		}
		note.id = object.value("id").toString();
	}
	if (object.contains("tick"))
	{
		if (!object.value("tick").isDouble())
		{
			error = "'tick' must be a number.";
			return false;
		}
		note.tick = object.value("tick").toDouble();
	}
	else if (requireCore)
	{
		error = "'tick' is required.";
		return false;
	}
	if (object.contains("duration"))
	{
		if (!object.value("duration").isDouble())
		{
			error = "'duration' must be a number.";
			return false;
		}
		note.duration = object.value("duration").toDouble();
	}
	else if (requireCore)
	{
		error = "'duration' is required.";
		return false;
	}
	if (object.contains("pitch"))
	{
		if (!object.value("pitch").isDouble())
		{
			error = "'pitch' must be a number.";
			return false;
		}
		note.pitch = object.value("pitch").toDouble();
	}
	else if (requireCore)
	{
		error = "'pitch' is required.";
		return false;
	}
	if (object.contains("lyric"))
	{
		if (!object.value("lyric").isString())
		{
			error = "'lyric' must be a string.";
			return false;
		}
		note.lyric = object.value("lyric").toString();
	}
	else if (requireCore)
	{
		note.lyric = "la";
	}
	if (!readString(object, "language", note.language, error, false)
		|| !readString(object, "pronunciation", note.pronunciation, error, false))
	{
		return false;
	}
	for (const auto& key : {QString("parameters"), QString("phonemes"), QString("xmlExtras")})
	{
		if (!object.contains(key))
		{
			continue;
		}
		if (!object.value(key).isObject())
		{
			error = QStringLiteral("'%1' must be an object.").arg(key);
			return false;
		}
		if (key == "parameters")
		{
			note.parameters = object.value(key).toObject();
		}
		else if (key == "phonemes")
		{
			note.phonemes = object.value(key).toObject();
		}
		else
		{
			note.xmlExtras = object.value(key).toObject();
		}
	}
	return validateNote(note, error);
}

QJsonObject noteJson(const svs::Note& note, int index)
{
	return {{"index", index}, {"id", note.id}, {"tick", note.tick}, {"duration", note.duration},
		{"pitch", note.pitch}, {"lyric", note.lyric}, {"language", note.language},
		{"pronunciation", note.pronunciation}, {"parameters", note.parameters}, {"phonemes", note.phonemes},
		{"xmlExtras", note.xmlExtras}};
}

QJsonObject voiceJson(const svs::Voice& voice)
{
	if (voice.id.isEmpty())
	{
		return {};
	}
	return {{"pluginId", voice.pluginId}, {"id", voice.id}, {"name", voice.name}, {"version", voice.version},
		{"language", voice.language}, {"defaultLyric", voice.defaultLyric}, {"avatar", voice.avatar},
		{"portrait", voice.portrait}, {"package", voice.package}};
}

QJsonObject parameterJson(const svs::Parameter& parameter, bool feedback = false)
{
	QJsonObject result{{"id", parameter.id}, {"name", parameter.name},
		{"translationKey", parameter.translationKey}, {"group", parameter.group}, {"type", parameter.type},
		{"scope", parameter.scope}, {"unit", parameter.unit}, {"scale", parameter.scale},
		{"interpolation", parameter.interpolation}, {"order", parameter.order}, {"maxItems", parameter.maxItems},
		{"minimum", parameter.minimum}, {"maximum", parameter.maximum}, {"step", parameter.step},
		{"writable", parameter.writable}, {"curve", parameter.curve}, {"visible", parameter.visible},
		{"enabled", parameter.enabled}, {"choices", parameter.choices}, {"disabledReason", parameter.disabledReason},
		{"color", parameter.color}, {"visibleWhen", parameter.visibleWhen},
		{"global", svs::globalParameter(parameter)}, {"feedback", feedback}};
	if (!parameter.defaultValue.isUndefined())
	{
		result.insert("default", parameter.defaultValue);
	}
	return result;
}

QJsonObject capabilitiesJson(SVSTrack* track)
{
	const auto& capabilities = track->capabilities();
	QJsonArray parameters;
	for (const auto& parameter : capabilities.parameters)
	{
		parameters.append(parameterJson(parameter));
	}
	for (const auto& parameter : capabilities.feedbackParameters)
	{
		parameters.append(parameterJson(parameter, true));
	}
	QJsonArray languages;
	for (const auto& language : capabilities.languages)
	{
		languages.append(language);
	}
	QJsonArray phonemeSet;
	for (const auto& item : capabilities.phonemeSet)
	{
		phonemeSet.append(item);
	}
	QJsonArray dictionaryResources;
	for (const auto& resource : capabilities.dictionaryResources)
	{
		dictionaryResources.append(resource);
	}
	QJsonArray dictionaries;
	for (const auto& dictionary : track->dictionaries())
	{
		dictionaries.append(QJsonObject{{"id", dictionary.id}, {"version", dictionary.version},
			{"language", dictionary.language}, {"phonemeSet", dictionary.phonemeSet},
			{"hash", dictionary.hash}});
	}
	QJsonArray diagnostics;
	for (const auto& diagnostic : track->capabilityDiagnostics())
	{
		diagnostics.append(diagnostic);
	}
	return {{"ready", track->capabilitiesReady()}, {"parameters", parameters}, {"languages", languages},
		{"phonemeSet", phonemeSet}, {"phonemeSetId", capabilities.phonemeSetId},
		{"defaultLanguage", capabilities.defaultLanguage}, {"pitchInput", capabilities.pitchInput},
		{"pitchUnit", capabilities.pitchUnit}, {"parser", capabilities.parser},
		{"continuation", capabilities.continuation}, {"dictionaryResources", dictionaryResources},
		{"dictionaries", dictionaries}, {"noteLanguage", capabilities.noteLanguage},
		{"phonemeTiming", capabilities.phonemeTiming}, {"cancel", capabilities.cancel},
		{"concurrent", capabilities.concurrent}, {"diagnostics", diagnostics}};
}

QJsonObject clipJson(SVSClip* clip, int trackIndex, int clipIndex, bool detail)
{
	QJsonObject result{{"path", clipPath(trackIndex, clipIndex, clip->getTrack())}, {"index", clipIndex},
		{"type", "svs"}, {"name", clip->name()}, {"start", clip->startPosition().getTicks()},
		{"length", clip->length().getTicks()}, {"noteCount", static_cast<int>(clip->notes().size())},
		{"status", clip->status()}, {"readOnly", clip->readOnly()}};
	if (detail)
	{
		result.insert("id", clip->id());
		result.insert("audioAvailable", clip->audio() != nullptr);
		result.insert("segments", static_cast<int>(clip->synthesisSegments().size()));
		result.insert("migrationDiagnostic", clip->migrationDiagnostic());
	}
	return result;
}

QJsonObject trackJson(SVSTrack* track, int trackIndex)
{
	QJsonObject result{{"path", trackPath(trackIndex, track)}, {"index", trackIndex}, {"type", "svs"},
		{"name", track->name()}, {"muted", track->isMuted()}, {"solo", track->isSolo()},
		{"height", track->getHeight()},
		{"color", track->color() ? QJsonValue(track->color()->name()) : QJsonValue()},
		{"parent", "song"}, {"volume", track->volumeModel()->value()},
		{"panning", track->panningModel()->value()}, {"mixerChannel", track->mixerChannelModel()->value()},
		{"pluginId", track->pluginId()}, {"voiceId", track->voiceId()}, {"voice", voiceJson(track->voice())},
		{"language", track->language()}, {"parameters", track->parameters()},
		{"capabilitiesReady", track->capabilitiesReady()},
		{"capabilityDiagnostics", QJsonArray::fromStringList(track->capabilityDiagnostics())},
		{"readOnly", track->readOnly()}, {"migrationDiagnostic", track->migrationDiagnostic()},
		{"scanning", svs::Registry::instance().scanning(track->pluginId())}};
	QJsonArray clips;
	for (std::size_t index = 0; index < track->getClips().size(); ++index)
	{
		clips.append(clipJson(static_cast<SVSClip*>(track->getClips()[index]), trackIndex,
			static_cast<int>(index), false));
	}
	result.insert("clips", clips);
	return result;
}

CommandResult bindVoice(SVSTrack* track, const QString& pluginId, const QString& voiceId)
{
	if (track->readOnly())
	{
		return CommandResult::failure(
			"svs_track_read_only", "This SVS track is read-only because its project data could not be migrated.");
	}
	auto& registry = svs::Registry::instance();
	const auto& voices = registry.voices();
	for (const auto& voice : voices)
	{
		if (voice.pluginId == pluginId && voice.id == voiceId)
		{
			track->bindVoice(pluginId, voiceId);
			return CommandResult::success(QJsonObject{{"pluginId", pluginId}, {"voiceId", voiceId},
				{"name", voice.name}, {"version", voice.version}, {"language", voice.language},
				{"capabilitiesPending", true}});
		}
	}
	if (registry.scanning(pluginId))
	{
		return CommandResult::failure("svs_catalog_scanning",
			"The SVS voicebank catalog is still scanning.", "Retry after svs.listVoices reports scanning false.");
	}
	return CommandResult::failure("svs_voice_not_found", "The requested SVS voicebank is not installed.");
}

CommandResult executeCreateTrack(const QJsonObject& arguments)
{
	auto* song = Engine::getSong();
	if (song == nullptr)
	{
		return engineUnavailable();
	}
	QString name;
	QString pluginId;
	QString voiceId;
	QString error;
	if (!readString(arguments, "name", name, error, false)
		|| !readString(arguments, "pluginId", pluginId, error, false)
		|| !readString(arguments, "voiceId", voiceId, error, false))
	{
		return invalidArguments(error);
	}
	if (pluginId.isEmpty() != voiceId.isEmpty())
	{
		return invalidArguments("'pluginId' and 'voiceId' must be provided together.");
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

	auto* track = Track::create(Track::Type::SVS, song);
	if (track == nullptr)
	{
		return CommandResult::failure("track_creation_failed", "LMMS could not create the SVS track.");
	}
	song->moveTrack(track, index);
	auto* svsTrack = static_cast<SVSTrack*>(track);
	if (!name.isEmpty())
	{
		svsTrack->setName(name);
	}
	if (!pluginId.isEmpty())
	{
		const auto bound = bindVoice(svsTrack, pluginId, voiceId);
		if (!bound.ok)
		{
			delete track;
			return bound;
		}
	}
	song->setModified();
	return CommandResult::success(trackJson(svsTrack, indexOfTrack(track)));
}

CommandResult executeCreateClip(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if (!resolveSvsTrack(arguments, track, trackIndex, failure))
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
	auto* clip = dynamic_cast<SVSClip*>(track->createClip(TimePos(position)));
	if (clip == nullptr)
	{
		return CommandResult::failure("clip_creation_failed", "LMMS could not create the SVS clip.");
	}
	clip->changeLength(TimePos(length));
	if (!name.isEmpty())
	{
		clip->setName(name);
	}
	Engine::getSong()->setModified();
	return CommandResult::success(clipJson(clip, trackIndex, indexOfClip(track, clip), true));
}

CommandResult executeSetVoice(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if (!resolveSvsTrack(arguments, track, trackIndex, failure))
	{
		return failure;
	}
	QString pluginId;
	QString voiceId;
	QString error;
	if (!readString(arguments, "pluginId", pluginId, error, true)
		|| !readString(arguments, "voiceId", voiceId, error, true))
	{
		return invalidArguments(error);
	}
	const auto bound = bindVoice(track, pluginId, voiceId);
	if (!bound.ok)
	{
		return bound;
	}
	auto result = bound.data;
	result.insert("track", trackJson(track, trackIndex));
	return CommandResult::success(result);
}

CommandResult executeGetTrack(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if (!resolveSvsTrack(arguments, track, trackIndex, failure))
	{
		return failure;
	}
	return CommandResult::success(trackJson(track, trackIndex));
}

CommandResult executeGetCapabilities(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if (!resolveSvsTrack(arguments, track, trackIndex, failure))
	{
		return failure;
	}
	auto result = capabilitiesJson(track);
	result.insert("path", trackPath(trackIndex, track));
	return CommandResult::success(result);
}

CommandResult executeGetNotes(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	SVSClip* clip = nullptr;
	int trackIndex = -1;
	int clipIndex = -1;
	CommandResult failure;
	if (!resolveSvsClip(arguments, track, clip, trackIndex, clipIndex, failure))
	{
		return failure;
	}
	Page page;
	QString error;
	if (!discovery::readPage(arguments, page, error))
	{
		return invalidArguments(error);
	}
	bool includePronunciation = false;
	double rangeStart = 0;
	double rangeEnd = 0;
	bool hasRange = false;
	if (!readBool(arguments, "includePronunciation", includePronunciation, error)
		|| !readRange(arguments, hasRange, rangeStart, rangeEnd, error))
	{
		return invalidArguments(error);
	}
	const bool pronunciation = includePronunciation;
	if (pronunciation && !track->capabilitiesReady())
	{
		return capabilitiesPending();
	}

	struct NoteEntry
	{
		int index;
		svs::Note note;
	};
	QVector<NoteEntry> entries;
	const auto& notes = clip->notes();
	for (int index = 0; index < notes.size(); ++index)
	{
		const auto& note = notes[index];
		if (hasRange && (note.tick < rangeStart || note.tick >= rangeEnd))
		{
			continue;
		}
		entries.append({index, note});
	}
	const auto pageItems = pageSlice(entries, page);
	QJsonArray notesJson;
	for (const auto& entry : pageItems)
	{
		auto item = noteJson(entry.note, entry.index);
		if (pronunciation)
		{
			item.insert("pronunciation", gui::editorPronunciation(clip, entry.note).toJson());
		}
		notesJson.append(item);
	}
	auto result = discovery::pageFields(entries.size(), page, notesJson.size());
	result.insert("path", clipPath(trackIndex, clipIndex, track));
	result.insert("notes", notesJson);
	return CommandResult::success(result);
}

CommandResult executeAddNotes(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	SVSClip* clip = nullptr;
	int trackIndex = -1;
	int clipIndex = -1;
	CommandResult failure;
	if (!resolveSvsClip(arguments, track, clip, trackIndex, clipIndex, failure))
	{
		return failure;
	}
	if (clip->readOnly())
	{
		return CommandResult::failure("svs_clip_read_only", "The SVS clip is read-only.");
	}
	const auto value = arguments.value("notes");
	if (!value.isArray() || value.toArray().isEmpty())
	{
		return invalidArguments("'notes' must be a non-empty array.");
	}
	auto notes = clip->notes();
	QSet<QString> ids;
	for (const auto& note : notes)
	{
		ids.insert(note.id);
	}
	QStringList added;
	QString error;
	for (const auto& item : value.toArray())
	{
		if (!item.isObject())
		{
			return invalidArguments("Each note must be an object.");
		}
		svs::Note note;
		if (!applyNoteFields(item.toObject(), note, true, error))
		{
			return invalidArguments(error);
		}
		if (note.id.isEmpty())
		{
			note.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		}
		if (ids.contains(note.id))
		{
			return invalidArguments("A note with this id already exists: " + note.id);
		}
		ids.insert(note.id);
		added.append(note.id);
		notes.append(note);
	}
	clip->setNotes(notes);
	return CommandResult::success(QJsonObject{{"path", clipPath(trackIndex, clipIndex, track)},
		{"added", QJsonArray::fromStringList(added)}, {"noteCount", static_cast<int>(clip->notes().size())}});
}

CommandResult executeUpdateNote(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	SVSClip* clip = nullptr;
	int trackIndex = -1;
	int clipIndex = -1;
	CommandResult failure;
	if (!resolveSvsClip(arguments, track, clip, trackIndex, clipIndex, failure))
	{
		return failure;
	}
	if (clip->readOnly())
	{
		return CommandResult::failure("svs_clip_read_only", "The SVS clip is read-only.");
	}
	QString id;
	QString error;
	if (!readString(arguments, "id", id, error, true))
	{
		return invalidArguments(error);
	}
	const auto fields = arguments.value("fields");
	if (!fields.isObject() || fields.toObject().isEmpty())
	{
		return invalidArguments("'fields' must be a non-empty object.");
	}
	auto notes = clip->notes();
	int index = -1;
	for (int i = 0; i < notes.size(); ++i)
	{
		if (notes[i].id == id)
		{
			index = i;
			break;
		}
	}
	if (index < 0)
	{
		return CommandResult::failure("svs_note_not_found", "The requested SVS note does not exist.");
	}
	if (!applyNoteFields(fields.toObject(), notes[index], false, error))
	{
		return invalidArguments(error);
	}
	notes[index].id = id;
	clip->setNotes(notes);
	return CommandResult::success(noteJson(notes[index], index));
}

CommandResult executeRemoveNotes(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	SVSClip* clip = nullptr;
	int trackIndex = -1;
	int clipIndex = -1;
	CommandResult failure;
	if (!resolveSvsClip(arguments, track, clip, trackIndex, clipIndex, failure))
	{
		return failure;
	}
	if (clip->readOnly())
	{
		return CommandResult::failure("svs_clip_read_only", "The SVS clip is read-only.");
	}
	QString error;
	QStringList requested;
	double rangeStart = 0;
	double rangeEnd = 0;
	bool hasRange = false;
	if (!readStringArray(arguments, "ids", requested, error, false)
		|| !readRange(arguments, hasRange, rangeStart, rangeEnd, error))
	{
		return invalidArguments(error);
	}
	if (requested.isEmpty() == !hasRange)
	{
		return invalidArguments("Provide exactly one of 'ids' or 'range'.");
	}
	QSet<QString> requestedIds(requested.cbegin(), requested.cend());
	auto notes = clip->notes();
	QStringList removed;
	if (!requested.isEmpty())
	{
		for (const auto& id : requested)
		{
			bool found = false;
			for (const auto& note : notes)
			{
				found = found || note.id == id;
			}
			if (!found)
			{
				return CommandResult::failure("svs_note_not_found", "The requested SVS note does not exist: " + id);
			}
		}
	}
	QVector<svs::Note> remaining;
	for (const auto& note : notes)
	{
		const bool remove = !requested.isEmpty() ? requestedIds.contains(note.id)
			: hasRange						? note.tick >= rangeStart && note.tick < rangeEnd
											: false;
		if (remove)
		{
			removed.append(note.id);
		}
		else
		{
			remaining.append(note);
		}
	}
	clip->setNotes(remaining);
	return CommandResult::success(QJsonObject{{"path", clipPath(trackIndex, clipIndex, track)},
		{"removed", QJsonArray::fromStringList(removed)}, {"noteCount", static_cast<int>(remaining.size())}});
}

CommandResult executeSetLyrics(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	SVSClip* clip = nullptr;
	int trackIndex = -1;
	int clipIndex = -1;
	CommandResult failure;
	if (!resolveSvsClip(arguments, track, clip, trackIndex, clipIndex, failure))
	{
		return failure;
	}
	if (clip->readOnly())
	{
		return CommandResult::failure("svs_clip_read_only", "The SVS clip is read-only.");
	}
	QString text;
	QString error;
	if (!readString(arguments, "text", text, error, true))
	{
		return invalidArguments(error);
	}
	if (!track->capabilitiesReady())
	{
		return capabilitiesPending();
	}
	QStringList requestedIds;
	double rangeStart = 0;
	double rangeEnd = 0;
	bool hasRange = false;
	bool skipContinuation = false;
	if (!readStringArray(arguments, "noteIds", requestedIds, error, false)
		|| !readRange(arguments, hasRange, rangeStart, rangeEnd, error)
		|| !readBool(arguments, "skipContinuation", skipContinuation, error))
	{
		return invalidArguments(error);
	}
	if (!requestedIds.isEmpty() && hasRange)
	{
		return invalidArguments("Provide at most one of 'noteIds' or 'range'.");
	}
	auto notes = clip->notes();
	QSet<QString> requestedSet(requestedIds.cbegin(), requestedIds.cend());
	QVector<int> indices;
	for (int index = 0; index < notes.size(); ++index)
	{
		if (!requestedSet.isEmpty() && !requestedSet.contains(notes[index].id))
		{
			continue;
		}
		if (requestedSet.isEmpty() && hasRange && (notes[index].tick < rangeStart || notes[index].tick >= rangeEnd))
		{
			continue;
		}
		indices.append(index);
	}
	if (!requestedSet.isEmpty() && indices.size() != requestedSet.size())
	{
		return CommandResult::failure("svs_note_not_found", "One or more requested SVS notes do not exist.");
	}
	std::stable_sort(indices.begin(), indices.end(),
		[&notes](int left, int right) { return notes[left].tick < notes[right].tick; });

	auto tokens = gui::SVSLyricEditor::splitLyrics(text);
	const auto marker = track->capabilities().continuation;
	if (skipContinuation && !marker.isEmpty())
	{
		tokens.removeAll(marker);
	}
	int assigned = 0;
	for (int index : indices)
	{
		if (assigned >= tokens.size())
		{
			break;
		}
		const bool continuation = skipContinuation && !marker.isEmpty() && notes[index].lyric == marker;
		if (!continuation)
		{
			notes[index].lyric = tokens[assigned++];
		}
	}
	clip->setNotes(notes);
	const int unused = std::max(0, static_cast<int>(indices.size()) - assigned);
	return CommandResult::success(QJsonObject{{"path", clipPath(trackIndex, clipIndex, track)},
		{"tokens", static_cast<int>(tokens.size())}, {"assigned", assigned}, {"unusedNotes", unused},
		{"remainingTokens", std::max(0, static_cast<int>(tokens.size()) - assigned)}});
}

CommandResult executeSetNoteParameter(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	SVSClip* clip = nullptr;
	int trackIndex = -1;
	int clipIndex = -1;
	CommandResult failure;
	if (!resolveSvsClip(arguments, track, clip, trackIndex, clipIndex, failure))
	{
		return failure;
	}
	if (clip->readOnly())
	{
		return CommandResult::failure("svs_clip_read_only", "The SVS clip is read-only.");
	}
	if (!track->capabilitiesReady())
	{
		return capabilitiesPending();
	}
	QString parameter;
	QStringList noteIds;
	QString error;
	bool phoneme = false;
	if (!readString(arguments, "parameter", parameter, error, true)
		|| !readStringArray(arguments, "noteIds", noteIds, error, true)
		|| !readBool(arguments, "phoneme", phoneme, error))
	{
		return invalidArguments(error);
	}
	const auto* definition = track->capabilities().parameter(parameter, phoneme ? "phoneme" : "note");
	if (definition == nullptr)
	{
		return CommandResult::failure("svs_parameter_not_found", "The requested SVS note parameter is not declared.");
	}
	const auto value = arguments.value("value");
	if (!definition->writable || !definition->enabled || !definition->accepts(value))
	{
		return invalidArguments("The value is not accepted by this SVS note parameter.");
	}
	QSet<QString> existing;
	for (const auto& note : clip->notes())
	{
		existing.insert(note.id);
	}
	for (const auto& id : noteIds)
	{
		if (!existing.contains(id))
		{
			return CommandResult::failure("svs_note_not_found", "The requested SVS note does not exist: " + id);
		}
	}
	if (!clip->setNoteParameter(noteIds, parameter, value, phoneme))
	{
		return CommandResult::failure("svs_parameter_value_invalid", "The SVS note parameter could not be applied.");
	}
	return CommandResult::success(QJsonObject{{"path", clipPath(trackIndex, clipIndex, track)},
		{"parameter", parameter}, {"updated", noteIds.size()}});
}

CommandResult executeSetParameters(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	SVSClip* clip = nullptr;
	int trackIndex = -1;
	int clipIndex = -1;
	CommandResult failure;
	if (!resolveSvsClip(arguments, track, clip, trackIndex, clipIndex, failure))
	{
		return failure;
	}
	if (clip->readOnly())
	{
		return CommandResult::failure("svs_clip_read_only", "The SVS clip is read-only.");
	}
	if (!track->capabilitiesReady())
	{
		return capabilitiesPending();
	}
	QString scope;
	QString error;
	if (!readString(arguments, "scope", scope, error, true))
	{
		return invalidArguments(error);
	}
	if (scope != "track" && scope != "clip" && scope != "global")
	{
		return invalidArguments("'scope' must be track, clip or global.");
	}
	const auto value = arguments.value("parameters");
	if (!value.isObject() || value.toObject().isEmpty())
	{
		return invalidArguments("'parameters' must be a non-empty object.");
	}
	const auto parameters = value.toObject();
	for (auto it = parameters.begin(); it != parameters.end(); ++it)
	{
		const auto* definition = track->capabilities().parameter(it.key(), scope);
		if (definition == nullptr)
		{
			return CommandResult::failure(
				"svs_parameter_not_found", "The SVS parameter is not declared: " + it.key());
		}
		if (!definition->writable || !definition->enabled || !definition->accepts(it.value()))
		{
			return invalidArguments("The value is not accepted by this SVS parameter: " + it.key());
		}
	}
	for (auto it = parameters.begin(); it != parameters.end(); ++it)
	{
		const bool applied = scope == "track" ? track->setParameter(it.key(), it.value())
			: scope == "clip"				  ? clip->setParameter(it.key(), it.value())
										  : clip->setGlobalParameter(it.key(), it.value());
		if (!applied)
		{
			return CommandResult::failure(
				"svs_parameter_value_invalid", "The SVS parameter could not be applied: " + it.key());
		}
	}
	return CommandResult::success(QJsonObject{{"path", clipPath(trackIndex, clipIndex, track)}, {"scope", scope},
		{"parameters", parameters}});
}

CommandResult executeGetCurves(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	SVSClip* clip = nullptr;
	int trackIndex = -1;
	int clipIndex = -1;
	CommandResult failure;
	if (!resolveSvsClip(arguments, track, clip, trackIndex, clipIndex, failure))
	{
		return failure;
	}
	return CommandResult::success(QJsonObject{{"path", clipPath(trackIndex, clipIndex, track)},
		{"ready", track->capabilitiesReady()}, {"curves", svs::curvesToJson(clip->curves())}});
}

CommandResult executeSetCurve(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	SVSClip* clip = nullptr;
	int trackIndex = -1;
	int clipIndex = -1;
	CommandResult failure;
	if (!resolveSvsClip(arguments, track, clip, trackIndex, clipIndex, failure))
	{
		return failure;
	}
	if (clip->readOnly())
	{
		return CommandResult::failure("svs_clip_read_only", "The SVS clip is read-only.");
	}
	QString id;
	QString error;
	if (!readString(arguments, "id", id, error, false))
	{
		return invalidArguments(error);
	}
	auto curves = clip->curves();
	if (arguments.contains("curve") && !arguments.value("curve").isNull())
	{
		if (!track->capabilitiesReady())
		{
			return capabilitiesPending();
		}
		const auto value = arguments.value("curve");
		if (!value.isObject())
		{
			return invalidArguments("'curve' must be an object or null.");
		}
		const auto object = value.toObject();
		const auto curveId = object.value("id").toString();
		if (curveId.isEmpty())
		{
			return invalidArguments("The curve object requires a non-empty 'id'.");
		}
		if (!id.isEmpty() && id != curveId)
		{
			return invalidArguments("'id' and curve.id must identify the same curve.");
		}
		const auto& capabilities = track->capabilities();
		const auto requestedScope = object.value("scope").toString();
		const svs::Parameter* descriptor = nullptr;
		if (!requestedScope.isEmpty())
		{
			descriptor = capabilities.parameter(curveId, requestedScope);
		}
		for (const auto& scope : {QString("clip"), QString("track"), QString("global")})
		{
			if (descriptor == nullptr)
			{
				descriptor = capabilities.parameter(curveId, scope);
			}
		}
		if (descriptor == nullptr)
		{
			return CommandResult::failure(
				"svs_parameter_not_found", "The SVS curve parameter is not declared: " + curveId);
		}
		if (!descriptor->curve || !descriptor->writable)
		{
			return CommandResult::failure(
				"svs_curve_not_supported", "The SVS parameter does not expose an editable curve: " + curveId);
		}
		svs::Curve curve;
		if (!svs::Curve::fromJson(object, curve, error, descriptor))
		{
			return CommandResult::failure("svs_curve_invalid", error);
		}
		if (curve.id != curveId)
		{
			return CommandResult::failure("svs_curve_invalid", "The parsed curve id does not match the request.");
		}
		if (curve.scope.isEmpty())
		{
			curve.scope = descriptor->scope;
		}
		curves[curveId] = curve;
	}
	else
	{
		if (id.isEmpty())
		{
			return invalidArguments("Provide 'id' when removing a curve with 'curve: null'.");
		}
		if (!curves.contains(id))
		{
			return CommandResult::failure("svs_curve_not_found", "The SVS curve does not exist: " + id);
		}
		curves.remove(id);
	}
	clip->setEditorData(clip->notes(), curves);
	return CommandResult::success(QJsonObject{{"path", clipPath(trackIndex, clipIndex, track)},
		{"curves", svs::curvesToJson(clip->curves())}});
}

CommandResult executeImportDictionary(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	SVSClip* clip = nullptr;
	int trackIndex = -1;
	int clipIndex = -1;
	CommandResult failure;
	if (!resolveSvsClip(arguments, track, clip, trackIndex, clipIndex, failure))
	{
		return failure;
	}
	if (clip->readOnly())
	{
		return CommandResult::failure("svs_clip_read_only", "The SVS clip is read-only.");
	}
	if (!track->capabilitiesReady())
	{
		return capabilitiesPending();
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
		return CommandResult::failure("file_not_found", "The dictionary file is not readable.");
	}
	constexpr qint64 MaxDictionaryBytes = 4 * 1024 * 1024;
	if (file.size() > MaxDictionaryBytes)
	{
		return CommandResult::failure("file_too_large", "The dictionary file exceeds 4 MiB.");
	}
	QFile input(file.absoluteFilePath());
	if (!input.open(QIODevice::ReadOnly))
	{
		return CommandResult::failure("file_not_found", input.errorString());
	}
	const auto bytes = input.readAll();
	QString importError;
	if (!clip->importDictionary(bytes, importError))
	{
		return CommandResult::failure("svs_dictionary_invalid", importError);
	}
	const auto root = QJsonDocument::fromJson(bytes).object();
	return CommandResult::success(QJsonObject{{"path", clipPath(trackIndex, clipIndex, track)},
		{"id", root.value("id").toString()}, {"language", root.value("language").toString()},
		{"phonemeSet", root.value("phonemeSet").toString()}, {"source", file.absoluteFilePath()}});
}

CommandResult executeGetPronunciation(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	SVSClip* clip = nullptr;
	int trackIndex = -1;
	int clipIndex = -1;
	CommandResult failure;
	if (!resolveSvsClip(arguments, track, clip, trackIndex, clipIndex, failure))
	{
		return failure;
	}
	if (!track->capabilitiesReady())
	{
		return capabilitiesPending();
	}
	QString noteId;
	QString lyric;
	QString language;
	QString afterNoteId;
	QString error;
	bool candidates = false;
	if (!readString(arguments, "noteId", noteId, error, false)
		|| !readString(arguments, "lyric", lyric, error, false)
		|| !readString(arguments, "language", language, error, false)
		|| !readString(arguments, "afterNoteId", afterNoteId, error, false)
		|| !readBool(arguments, "candidates", candidates, error))
	{
		return invalidArguments(error);
	}
	const auto& notes = clip->notes();
	if (!noteId.isEmpty())
	{
		for (const auto& note : notes)
		{
			if (note.id == noteId)
			{
				return CommandResult::success(
					gui::editorPronunciation(clip, note, candidates).toJson());
			}
		}
		return CommandResult::failure("svs_note_not_found", "The requested SVS note does not exist.");
	}
	if (lyric.isEmpty())
	{
		return invalidArguments("Provide 'noteId' or 'lyric'.");
	}
	svs::Note note;
	note.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	note.lyric = lyric;
	note.language = language.isEmpty() ? track->language() : language;
	note.duration = 48;
	note.pitch = 60;
	if (!afterNoteId.isEmpty())
	{
		bool found = false;
		for (const auto& candidate : notes)
		{
			if (candidate.id == afterNoteId)
			{
				note.tick = candidate.tick + candidate.duration;
				found = true;
				break;
			}
		}
		if (!found)
		{
			return CommandResult::failure("svs_note_not_found", "The afterNoteId does not exist.");
		}
	}
	const svs::Note* previous = nullptr;
	for (const auto& candidate : notes)
	{
		if (candidate.tick < note.tick && (previous == nullptr || candidate.tick > previous->tick))
		{
			previous = &candidate;
		}
	}
	QVector<svs::Dictionary> project;
	for (const auto& value : clip->projectDictionaryData())
	{
		svs::Dictionary dictionary;
		QString dictionaryError;
		if (svs::Dictionary::parse(QJsonDocument(value.toObject()).toJson(), track->capabilities().phonemeSet,
				dictionary, dictionaryError))
		{
			project.append(dictionary);
		}
	}
	return CommandResult::success(svs::resolvePronunciation(note, track->capabilities(), track->dictionaries(),
		project, track->language(), previous, nullptr)
									  .toJson());
}

CommandResult executeSynthesize(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if (!resolveSvsTrack(arguments, track, trackIndex, failure))
	{
		return failure;
	}
	if (CommandBus::instance().batchDepth() != 0)
	{
		return CommandResult::failure(
			"async_work_in_batch", "Run SVS synthesis after the project transaction has committed.");
	}
	if (arguments.value("dryRun").toBool())
	{
		return CommandResult::success(QJsonObject{{"wouldSynthesize", true}});
	}
	QJsonArray statuses;
	SVSClip* single = nullptr;
	int clipIndex = -1;
	if (arguments.contains("clip"))
	{
		if (!resolveSvsClip(arguments, track, single, trackIndex, clipIndex, failure))
		{
			return failure;
		}
		single->synthesize();
		statuses.append(QJsonObject{{"clip", clipIndex}, {"status", single->status()}});
	}
	else
	{
		for (std::size_t index = 0; index < track->getClips().size(); ++index)
		{
			auto* clip = static_cast<SVSClip*>(track->getClips()[index]);
			clip->synthesize();
			statuses.append(QJsonObject{{"clip", static_cast<int>(index)}, {"status", clip->status()}});
		}
	}
	return CommandResult::success(
		QJsonObject{{"path", trackPath(trackIndex, track)}, {"clips", statuses}});
}

CommandResult executeCancelSynthesis(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if (!resolveSvsTrack(arguments, track, trackIndex, failure))
	{
		return failure;
	}
	if (CommandBus::instance().batchDepth() != 0)
	{
		return CommandResult::failure(
			"async_work_in_batch", "Cancel SVS synthesis outside the project transaction.");
	}
	if (arguments.value("dryRun").toBool())
	{
		return CommandResult::success(QJsonObject{{"wouldCancel", true}});
	}
	QJsonArray statuses;
	SVSClip* single = nullptr;
	int clipIndex = -1;
	if (arguments.contains("clip"))
	{
		if (!resolveSvsClip(arguments, track, single, trackIndex, clipIndex, failure))
		{
			return failure;
		}
		single->cancelSynthesis();
		statuses.append(QJsonObject{{"clip", clipIndex}, {"status", single->status()}});
	}
	else
	{
		for (std::size_t index = 0; index < track->getClips().size(); ++index)
		{
			auto* clip = static_cast<SVSClip*>(track->getClips()[index]);
			clip->cancelSynthesis();
			statuses.append(QJsonObject{{"clip", static_cast<int>(index)}, {"status", clip->status()}});
		}
	}
	return CommandResult::success(
		QJsonObject{{"path", trackPath(trackIndex, track)}, {"clips", statuses}});
}

CommandResult executeGetStatus(const QJsonObject& arguments)
{
	SVSTrack* track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if (arguments.contains("clip"))
	{
		SVSClip* clip = nullptr;
		int clipIndex = -1;
		if (!resolveSvsClip(arguments, track, clip, trackIndex, clipIndex, failure))
		{
			return failure;
		}
		return CommandResult::success(clipJson(clip, trackIndex, clipIndex, true));
	}
	if (!resolveSvsTrack(arguments, track, trackIndex, failure))
	{
		return failure;
	}
	QJsonArray clips;
	for (std::size_t index = 0; index < track->getClips().size(); ++index)
	{
		clips.append(clipJson(static_cast<SVSClip*>(track->getClips()[index]), trackIndex,
			static_cast<int>(index), true));
	}
	return CommandResult::success(QJsonObject{{"path", trackPath(trackIndex, track)},
		{"pluginId", track->pluginId()}, {"voiceId", track->voiceId()},
		{"capabilitiesReady", track->capabilitiesReady()},
		{"capabilityDiagnostics", QJsonArray::fromStringList(track->capabilityDiagnostics())},
		{"scanning", svs::Registry::instance().scanning(track->pluginId())}, {"clips", clips}});
}

QJsonObject trackSchema()
{
	QJsonObject properties{
		{"track",
			QJsonObject{{"anyOf", QJsonArray{discovery::integerSchema(), discovery::stringSchema()}},
				{"description", "Track index, unique name, or song/track:N path."}}},
		{"parent", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"song", "pattern"}}}}};
	return properties;
}

QJsonObject clipSchema()
{
	auto properties = trackSchema();
	properties.insert("clip", discovery::integerSchema());
	return properties;
}

QJsonObject noteSchema()
{
	QJsonObject properties{{"id", discovery::stringSchema()},
		{"tick", QJsonObject{{"type", "number"}, {"minimum", 0}}},
		{"duration", QJsonObject{{"type", "number"}, {"minimum", 0}}},
		{"pitch", QJsonObject{{"type", "number"}, {"minimum", 0}, {"maximum", 127}}},
		{"lyric", discovery::stringSchema()}, {"language", discovery::stringSchema()},
		{"pronunciation", discovery::stringSchema()}, {"parameters", QJsonObject{{"type", "object"}}},
		{"phonemes", QJsonObject{{"type", "object"}}}, {"xmlExtras", QJsonObject{{"type", "object"}}}};
	auto schema = discovery::objectSchema(properties, QJsonArray{"tick", "duration", "pitch"});
	return schema;
}

QJsonObject rangeSchema()
{
	auto schema = discovery::objectSchema(
		QJsonObject{{"start", QJsonObject{{"type", "number"}, {"minimum", 0}}},
			{"end", QJsonObject{{"type", "number"}, {"minimum", 0}}}},
		QJsonArray{"start", "end"});
	schema.insert("description", "Clip-local tick range; start inclusive, end exclusive.");
	return schema;
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

void registerSvsCommands(CommandBus& commandBus)
{
	QJsonObject createTrackProperties = trackSchema();
	createTrackProperties.remove("track");
	createTrackProperties.insert("name", discovery::stringSchema());
	createTrackProperties.insert("index", discovery::integerSchema());
	createTrackProperties.insert("pluginId", discovery::stringSchema());
	createTrackProperties.insert("voiceId", discovery::stringSchema());
	registerCommand(commandBus, "svs.createTrack", "Create an SVS track, optionally binding a voicebank.",
		discovery::objectSchema(createTrackProperties), Mutability::Mutating, TxScope::Single, executeCreateTrack);

	QJsonObject createClipProperties = trackSchema();
	createClipProperties.insert("position", discovery::integerSchema());
	createClipProperties.insert("length", discovery::integerSchema());
	createClipProperties.insert("name", discovery::stringSchema());
	registerCommand(commandBus, "svs.createClip", "Create an SVS clip on an SVS track.",
		discovery::objectSchema(createClipProperties, QJsonArray{"track"}), Mutability::Mutating, TxScope::Single,
		executeCreateClip);

	QJsonObject voiceProperties = trackSchema();
	voiceProperties.insert("pluginId", discovery::stringSchema());
	voiceProperties.insert("voiceId", discovery::stringSchema());
	registerCommand(commandBus, "svs.setVoice", "Bind an installed SVS voicebank to an SVS track.",
		discovery::objectSchema(voiceProperties, QJsonArray{"track", "pluginId", "voiceId"}), Mutability::Mutating,
		TxScope::Single, executeSetVoice);

	registerCommand(commandBus, "svs.getTrack", "Return SVS track, voice, parameters and clip status.",
		discovery::objectSchema(trackSchema(), QJsonArray{"track"}), Mutability::ReadOnly, TxScope::None,
		executeGetTrack);

	registerCommand(commandBus, "svs.getCapabilities",
		"Return SVS parameter, language, phoneme and dictionary capabilities.",
		discovery::objectSchema(trackSchema(), QJsonArray{"track"}), Mutability::ReadOnly, TxScope::None,
		executeGetCapabilities);

	QJsonObject getNotesProperties = clipSchema();
	getNotesProperties.insert("range", rangeSchema());
	getNotesProperties.insert("includePronunciation", discovery::booleanSchema());
	getNotesProperties.insert("page", QJsonObject{{"type", "integer"}, {"minimum", 0}});
	getNotesProperties.insert("pageSize", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 500}});
	registerCommand(commandBus, "svs.getNotes", "List SVS notes with stable ids and optional pronunciation.",
		discovery::objectSchema(getNotesProperties, QJsonArray{"track", "clip"}), Mutability::ReadOnly, TxScope::None,
		executeGetNotes);

	QJsonObject addNotesProperties = clipSchema();
	addNotesProperties.insert("notes",
		QJsonObject{{"type", "array"}, {"items", noteSchema()}, {"minItems", 1}, {"maxItems", 4096}});
	registerCommand(commandBus, "svs.addNotes", "Append notes with lyrics to an SVS clip.",
		discovery::objectSchema(addNotesProperties, QJsonArray{"track", "clip", "notes"}), Mutability::Mutating,
		TxScope::Single, executeAddNotes);

	QJsonObject updateNoteProperties = clipSchema();
	updateNoteProperties.insert("id", discovery::stringSchema());
	auto fieldsSchema = noteSchema();
	fieldsSchema.remove("required");
	updateNoteProperties.insert("fields", fieldsSchema);
	registerCommand(commandBus, "svs.updateNote", "Update SVS note fields by stable id.",
		discovery::objectSchema(updateNoteProperties, QJsonArray{"track", "clip", "id", "fields"}),
		Mutability::Mutating, TxScope::Single, executeUpdateNote);

	QJsonObject removeNotesProperties = clipSchema();
	removeNotesProperties.insert("ids", discovery::stringArraySchema());
	removeNotesProperties.insert("range", rangeSchema());
	registerCommand(commandBus, "svs.removeNotes", "Remove SVS notes by id or clip-local tick range.",
		discovery::objectSchema(removeNotesProperties, QJsonArray{"track", "clip"}), Mutability::Destructive,
		TxScope::Single, executeRemoveNotes);

	QJsonObject lyricsProperties = clipSchema();
	lyricsProperties.insert("text", discovery::stringSchema());
	lyricsProperties.insert("noteIds", discovery::stringArraySchema());
	lyricsProperties.insert("range", rangeSchema());
	lyricsProperties.insert("skipContinuation", discovery::booleanSchema());
	registerCommand(commandBus, "svs.setLyrics", "Assign lyrics to selected SVS notes in tick order.",
		discovery::objectSchema(lyricsProperties, QJsonArray{"track", "clip", "text"}), Mutability::Mutating,
		TxScope::Single, executeSetLyrics);

	QJsonObject noteParameterProperties = clipSchema();
	noteParameterProperties.insert("noteIds", discovery::stringArraySchema());
	noteParameterProperties.insert("parameter", discovery::stringSchema());
	noteParameterProperties.insert("value", QJsonObject{});
	noteParameterProperties.insert("phoneme", discovery::booleanSchema());
	registerCommand(commandBus, "svs.setNoteParameter",
		"Set a declared note or phoneme parameter on selected SVS notes.",
		discovery::objectSchema(noteParameterProperties,
			QJsonArray{"track", "clip", "noteIds", "parameter", "value"}),
		Mutability::Mutating, TxScope::Single, executeSetNoteParameter);

	QJsonObject parametersProperties = clipSchema();
	parametersProperties.insert("scope", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"track", "clip", "global"}}});
	parametersProperties.insert("parameters", QJsonObject{{"type", "object"}});
	registerCommand(commandBus, "svs.setParameters", "Set declared track, clip or global SVS parameters.",
		discovery::objectSchema(parametersProperties, QJsonArray{"track", "clip", "scope", "parameters"}),
		Mutability::Mutating, TxScope::Single, executeSetParameters);

	registerCommand(commandBus, "svs.getCurves", "List editable SVS parameter curves for a clip.",
		discovery::objectSchema(clipSchema(), QJsonArray{"track", "clip"}), Mutability::ReadOnly, TxScope::None,
		executeGetCurves);

	QJsonObject curveProperties = clipSchema();
	curveProperties.insert("id", discovery::stringSchema());
	curveProperties.insert("curve",
		QJsonObject{{"anyOf",
			QJsonArray{QJsonObject{{"type", "object"}}, QJsonObject{{"type", "null"}}}}});
	registerCommand(commandBus, "svs.setCurve",
		"Replace or remove one declared SVS parameter curve on a clip.",
		discovery::objectSchema(curveProperties, QJsonArray{"track", "clip"}), Mutability::Mutating,
		TxScope::Single, executeSetCurve);

	QJsonObject dictionaryProperties = clipSchema();
	dictionaryProperties.insert("path", discovery::stringSchema());
	registerCommand(commandBus, "svs.importDictionary", "Import a project dictionary into an SVS clip.",
		discovery::objectSchema(dictionaryProperties, QJsonArray{"track", "clip", "path"}), Mutability::Mutating,
		TxScope::Single, executeImportDictionary);

	QJsonObject pronunciationProperties = clipSchema();
	pronunciationProperties.insert("noteId", discovery::stringSchema());
	pronunciationProperties.insert("lyric", discovery::stringSchema());
	pronunciationProperties.insert("language", discovery::stringSchema());
	pronunciationProperties.insert("afterNoteId", discovery::stringSchema());
	pronunciationProperties.insert("candidates", discovery::booleanSchema());
	registerCommand(commandBus, "svs.getPronunciation",
		"Resolve lyrics to phonemes and candidates without changing the clip.",
		discovery::objectSchema(pronunciationProperties, QJsonArray{"track", "clip"}), Mutability::ReadOnly,
		TxScope::None, executeGetPronunciation);

	registerCommand(commandBus, "svs.synthesize", "Start asynchronous SVS synthesis for a clip or track.",
		discovery::objectSchema(clipSchema(), QJsonArray{"track"}), Mutability::Mutating, TxScope::None,
		executeSynthesize);
	registerCommand(commandBus, "svs.cancelSynthesis", "Cancel asynchronous SVS synthesis.",
		discovery::objectSchema(clipSchema(), QJsonArray{"track"}), Mutability::Mutating, TxScope::None,
		executeCancelSynthesis);
	registerCommand(commandBus, "svs.getStatus", "Return SVS clip or track synthesis status.",
		discovery::objectSchema(clipSchema(), QJsonArray{"track"}), Mutability::ReadOnly, TxScope::None,
		executeGetStatus);
}

} // namespace lmms::agent
