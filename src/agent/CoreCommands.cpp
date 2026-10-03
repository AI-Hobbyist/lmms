#include "CoreCommands.h"

#include <QColor>
#include <QDomDocument>
#include <QDomElement>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "Clip.h"
#include "AudioEngine.h"
#include "DataFile.h"
#include "DetuningHelper.h"
#include "Engine.h"
#include "Editor.h"
#include "AutomationClip.h"
#include "AutomationNode.h"
#include "AutomationTrack.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MeterModel.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "Note.h"
#include "PatternStore.h"
#include "Pitch.h"
#include "Plugin.h"
#include "PluginFactory.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"
#include "Timeline.h"
#include "Track.h"
#include "agent/CommandBus.h"
#include "panning.h"
#include "volume.h"

namespace lmms::agent
{

namespace
{

CommandResult invalidArguments( const QString &message )
{
	return CommandResult::failure( "invalid_arguments", message );
}




CommandResult engineUnavailable()
{
	return CommandResult::failure( "engine_unavailable", "The song model is unavailable." );
}




CommandResult mixerUnavailable()
{
	return CommandResult::failure( "mixer_unavailable", "The mixer model is unavailable." );
}




QJsonObject objectSchema( QJsonObject properties = {}, QJsonArray required = {} )
{
	QJsonObject schema{
		{ "type", "object" },
		{ "properties", properties }
	};
	if( !required.isEmpty() )
	{
		schema.insert( "required", required );
	}
	return schema;
}




QJsonObject integerSchema()
{
	return { { "type", "integer" } };
}




QJsonObject numberSchema()
{
	return { { "type", "number" } };
}




QJsonObject booleanSchema()
{
	return { { "type", "boolean" } };
}




QJsonObject stringSchema()
{
	return { { "type", "string" } };
}




QString argumentName( const char *name )
{
	return QString::fromLatin1( name );
}




bool readInteger( const QJsonObject &arguments, const char *name, int &result, QString &error )
{
	const auto value = arguments.value( argumentName( name ) );
	if( !value.isDouble() )
	{
		error = QString( "'%1' must be an integer." ).arg( argumentName( name ) );
		return false;
	}

	const double number = value.toDouble();
	if( !std::isfinite( number ) || std::floor( number ) != number ||
		number < std::numeric_limits<int>::lowest() ||
		number > std::numeric_limits<int>::max() )
	{
		error = QString( "'%1' must be an integer in the supported range." ).arg( argumentName( name ) );
		return false;
	}

	result = static_cast<int>( number );
	return true;
}




bool readRequiredInteger( const QJsonObject &arguments, const char *name, int &result, QString &error )
{
	if( !arguments.contains( argumentName( name ) ) )
	{
		error = QString( "Missing required argument '%1'." ).arg( argumentName( name ) );
		return false;
	}
	return readInteger( arguments, name, result, error );
}




bool readOptionalInteger( const QJsonObject &arguments, const char *name, bool &present,
	int &result, QString &error )
{
	present = arguments.contains( argumentName( name ) );
	return !present || readInteger( arguments, name, result, error );
}




bool readNumber( const QJsonObject &arguments, const char *name, double &result, QString &error )
{
	const auto value = arguments.value( argumentName( name ) );
	if( !value.isDouble() || !std::isfinite( value.toDouble() ) )
	{
		error = QString( "'%1' must be a finite number." ).arg( argumentName( name ) );
		return false;
	}

	result = value.toDouble();
	return true;
}




bool readRequiredNumber( const QJsonObject &arguments, const char *name, double &result, QString &error )
{
	if( !arguments.contains( argumentName( name ) ) )
	{
		error = QString( "Missing required argument '%1'." ).arg( argumentName( name ) );
		return false;
	}
	return readNumber( arguments, name, result, error );
}




bool readOptionalNumber( const QJsonObject &arguments, const char *name, bool &present,
	double &result, QString &error )
{
	present = arguments.contains( argumentName( name ) );
	return !present || readNumber( arguments, name, result, error );
}




bool readRequiredBool( const QJsonObject &arguments, const char *name, bool &result, QString &error )
{
	if( !arguments.contains( argumentName( name ) ) )
	{
		error = QString( "Missing required argument '%1'." ).arg( argumentName( name ) );
		return false;
	}

	const auto value = arguments.value( argumentName( name ) );
	if( !value.isBool() )
	{
		error = QString( "'%1' must be a boolean." ).arg( argumentName( name ) );
		return false;
	}

	result = value.toBool();
	return true;
}




bool readOptionalBool( const QJsonObject &arguments, const char *name, bool &present,
	bool &result, QString &error )
{
	present = arguments.contains( argumentName( name ) );
	if( !present )
	{
		return true;
	}

	const auto value = arguments.value( argumentName( name ) );
	if( !value.isBool() )
	{
		error = QString( "'%1' must be a boolean." ).arg( argumentName( name ) );
		return false;
	}

	result = value.toBool();
	return true;
}



bool readRequiredString( const QJsonObject &arguments, const char *name, QString &result, QString &error,
	bool allowEmpty = false )
{
	if( !arguments.contains( argumentName( name ) ) )
	{
		error = QString( "Missing required argument '%1'." ).arg( argumentName( name ) );
		return false;
	}

	const auto value = arguments.value( argumentName( name ) );
	if( !value.isString() || ( !allowEmpty && value.toString().isEmpty() ) )
	{
		error = QStringLiteral( "'%1' must be a string%2." ).arg( argumentName( name ),
			allowEmpty ? QString() : QStringLiteral( " and must not be empty" ) );
		return false;
	}

	result = value.toString();
	return true;
}




bool readOptionalString( const QJsonObject &arguments, const char *name, bool &present,
	QString &result, QString &error )
{
	present = arguments.contains( argumentName( name ) );
	if( !present )
	{
		return true;
	}

	const auto value = arguments.value( argumentName( name ) );
	if( !value.isString() )
	{
		error = QString( "'%1' must be a string." ).arg( argumentName( name ) );
		return false;
	}

	result = value.toString();
	return true;
}




bool readNonNegativeInteger( const QJsonObject &arguments, const char *name, int &result, QString &error )
{
	if( !readRequiredInteger( arguments, name, result, error ) )
	{
		return false;
	}
	if( result < 0 )
	{
		error = QString( "'%1' must not be negative." ).arg( argumentName( name ) );
		return false;
	}
	return true;
}




QString trackTypeName( Track::Type type )
{
	switch( type )
	{
	case Track::Type::Instrument: return "instrument";
	case Track::Type::Pattern: return "pattern";
	case Track::Type::Sample: return "sample";
	case Track::Type::Event: return "event";
	case Track::Type::Video: return "video";
	case Track::Type::Automation: return "automation";
	case Track::Type::HiddenAutomation: return "hiddenAutomation";
	case Track::Type::Count: return "unknown";
	}
	return "unknown";
}




QString clipTypeName( const Clip *clip )
{
	if( dynamic_cast<const MidiClip *>( clip ) != nullptr )
	{
		return "midi";
	}

	switch( clip->getTrack()->type() )
	{
	case Track::Type::Sample: return "sample";
	case Track::Type::Automation: return "automation";
	default: return "unknown";
	}
}




QString playModeName( Song::PlayMode mode )
{
	switch( mode )
	{
	case Song::PlayMode::None: return "none";
	case Song::PlayMode::Song: return "song";
	case Song::PlayMode::Pattern: return "pattern";
	case Song::PlayMode::MidiClip: return "midiClip";
	case Song::PlayMode::AutomationClip: return "automationClip";
	case Song::PlayMode::Count: return "unknown";
	}
	return "unknown";
}




QString trackPath( int trackIndex, const Track *track = nullptr )
{
	return QStringLiteral( "%1/track:%2" ).arg(
		track && track->trackContainer() == Engine::patternStore() ? "pattern" : "song" ).arg( trackIndex );
}




QString clipPath( int trackIndex, int clipIndex, const Track *track = nullptr )
{
	return QStringLiteral( "%1/clip:%2" ).arg( trackPath( trackIndex, track ) ).arg( clipIndex );
}




int indexOfTrack( const Song *, const Track *track )
{
	const auto &tracks = track->trackContainer()->tracks();
	for( std::size_t index = 0; index < tracks.size(); ++index )
	{
		if( tracks[index] == track )
		{
			return static_cast<int>( index );
		}
	}
	return -1;
}




int indexOfClip( const Track *track, const Clip *clip )
{
	const auto &clips = track->getClips();
	for( std::size_t index = 0; index < clips.size(); ++index )
	{
		if( clips[index] == clip )
		{
			return static_cast<int>( index );
		}
	}
	return -1;
}




TrackContainer *resolveParent( Song *song, const QJsonObject &arguments, CommandResult &failure )
{
	const auto parent = arguments.value( "parent" ).toString( "song" );
	if( parent == "song" ) { return song; }
	if( parent == "pattern" ) { return Engine::patternStore(); }
	failure = invalidArguments( "'parent' must be song or pattern." );
	return nullptr;
}

bool resolveTrack( Song *song, const QJsonObject &arguments, Track *&track, int &trackIndex,
	CommandResult &failure )
{
	auto *container = resolveParent( song, arguments, failure );
	if( !container ) { return false; }
	QString error;
	const auto selector = arguments.value( "track" );
	if( selector.isString() )
	{
		const auto name = selector.toString();
		const auto path = QRegularExpression( "^(song|pattern)/track:([0-9]+)$" ).match( name );
		if( path.hasMatch() )
		{
			if( arguments.contains( "parent" ) && arguments.value( "parent" ).toString() != path.captured( 1 ) )
			{
				failure = invalidArguments( "The track path conflicts with 'parent'." );
				return false;
			}
			container = path.captured( 1 ) == "song" ? static_cast<TrackContainer *>( song ) : Engine::patternStore();
			bool valid = false;
			trackIndex = path.captured( 2 ).toInt( &valid );
			if( !valid ) { failure = invalidArguments( "The track index is out of range." ); return false; }
		}
		else
		{
			trackIndex = -1;
			for( std::size_t index = 0; index < container->tracks().size(); ++index )
			{
				if( container->tracks()[index]->name() != name ) { continue; }
				if( trackIndex >= 0 )
				{
					failure = CommandResult::failure( "ambiguous_track", "Use a track path or index for duplicate names." );
					return false;
				}
				trackIndex = static_cast<int>( index );
			}
		}
	}
	else if( !readNonNegativeInteger( arguments, "track", trackIndex, error ) )
	{
		failure = invalidArguments( error );
		return false;
	}
	const auto &tracks = container->tracks();
	if( trackIndex < 0 || trackIndex >= static_cast<int>( tracks.size() ) )
	{
		failure = CommandResult::failure( "track_not_found",
			QStringLiteral( "Track %1 does not exist." ).arg( trackIndex ) );
		return false;
	}

	track = tracks[trackIndex];
	return true;
}




struct ClipAddress
{
	Track *track = nullptr;
	Clip *clip = nullptr;
	int trackIndex = -1;
	int clipIndex = -1;
};




bool resolveClip( Song *song, const QJsonObject &arguments, ClipAddress &address,
	CommandResult &failure )
{
	if( !resolveTrack( song, arguments, address.track, address.trackIndex, failure ) )
	{
		return false;
	}

	QString error;
	if( !readNonNegativeInteger( arguments, "clip", address.clipIndex, error ) )
	{
		failure = invalidArguments( error );
		return false;
	}

	const auto &clips = address.track->getClips();
	if( address.clipIndex >= static_cast<int>( clips.size() ) )
	{
		failure = CommandResult::failure( "clip_not_found",
			QStringLiteral( "Clip %1 does not exist on track %2." )
				.arg( address.clipIndex ).arg( address.trackIndex ) );
		return false;
	}

	address.clip = clips[address.clipIndex];
	return true;
}




bool resolveInstrumentTrack( Song *song, const QJsonObject &arguments,
	InstrumentTrack *&instrumentTrack, int &trackIndex, CommandResult &failure )
{
	Track *track = nullptr;
	if( !resolveTrack( song, arguments, track, trackIndex, failure ) )
	{
		return false;
	}

	instrumentTrack = dynamic_cast<InstrumentTrack *>( track );
	if( instrumentTrack == nullptr )
	{
		failure = CommandResult::failure( "wrong_track_type",
			QStringLiteral( "Track %1 is not an instrument track." ).arg( trackIndex ) );
		return false;
	}
	return true;
}




bool resolveMidiClip( Song *song, const QJsonObject &arguments, MidiClip *&midiClip,
	ClipAddress &address, CommandResult &failure )
{
	if( !resolveClip( song, arguments, address, failure ) )
	{
		return false;
	}

	midiClip = dynamic_cast<MidiClip *>( address.clip );
	if( midiClip == nullptr )
	{
		failure = CommandResult::failure( "wrong_clip_type",
			QStringLiteral( "Clip %1 on track %2 is not a MIDI clip." )
				.arg( address.clipIndex ).arg( address.trackIndex ) );
		return false;
	}
	return true;
}




bool resolveSampleClip( Song *song, const QJsonObject &arguments, SampleClip *&sampleClip,
	ClipAddress &address, CommandResult &failure )
{
	if( !resolveClip( song, arguments, address, failure ) )
	{
		return false;
	}

	sampleClip = dynamic_cast<SampleClip *>( address.clip );
	if( sampleClip == nullptr )
	{
		failure = CommandResult::failure( "wrong_clip_type",
			QStringLiteral( "Clip %1 on track %2 is not a sample clip." )
				.arg( address.clipIndex ).arg( address.trackIndex ) );
		return false;
	}
	return true;
}



QJsonObject songSummary( Song *song )
{
	return {
		{ "tempo", song->getTempo() },
		{ "timeSignature", QJsonObject{
			{ "numerator", song->getTimeSigModel().getNumerator() },
			{ "denominator", song->getTimeSigModel().getDenominator() }
		} },
		{ "lengthBars", song->length() },
		{ "trackCount", static_cast<int>( song->tracks().size() ) },
		{ "playMode", playModeName( song->playMode() ) },
		{ "playing", song->isPlaying() },
		{ "paused", song->isPaused() },
		{ "modified", song->isModified() }
	};
}




QJsonObject instrumentParameters( InstrumentTrack *track )
{
	return {
		{ "volume", track->volumeModel()->value() },
		{ "panning", track->panningModel()->value() },
		{ "pitch", track->pitchModel()->value() },
		{ "pitchRange", track->pitchRangeModel()->value() },
		{ "baseNote", track->baseNoteModel()->value() }
	};
}




QJsonObject clipDetail( const ClipAddress &address );
QJsonArray trackEffects( Track *track );

QJsonObject trackDetail( Song *song, Track *track, int trackIndex )
{
	QJsonObject detail{
		{ "path", trackPath( trackIndex, track ) },
		{ "index", trackIndex },
		{ "type", trackTypeName( track->type() ) },
		{ "name", track->name() },
		{ "muted", track->isMuted() },
		{ "solo", track->isSolo() },
		{ "parent", track->trackContainer() == Engine::patternStore() ? "pattern" : "song" },
		{ "height", track->getHeight() },
		{ "color", track->color() ? QJsonValue( track->color()->name() ) : QJsonValue() },
		{ "clipCount", static_cast<int>( track->getClips().size() ) }
	};

	if( auto *instrumentTrack = dynamic_cast<InstrumentTrack *>( track ) )
	{
		detail.insert( "instrumentParameters", instrumentParameters( instrumentTrack ) );
		detail.insert( "mixerChannel", instrumentTrack->mixerChannelModel()->value() );
		if( instrumentTrack->instrument() != nullptr )
		{
			detail.insert( "instrument", instrumentTrack->instrumentName() );
		}
	}
	if( auto *sampleTrack = dynamic_cast<SampleTrack *>( track ) )
	{
		detail.insert( "mixerChannel", sampleTrack->mixerChannelModel()->value() );
	}

	QJsonArray clips;
	for( std::size_t index = 0; index < track->getClips().size(); ++index )
	{
		clips.append( clipDetail( { track, track->getClips()[index], trackIndex, static_cast<int>( index ) } ) );
	}
	detail.insert( "clips", clips );
	detail.insert( "effects", trackEffects( track ) );
	return detail;
}




QJsonObject clipDetail( const ClipAddress &address )
{
	QJsonObject detail{
		{ "path", clipPath( address.trackIndex, address.clipIndex, address.track ) },
		{ "track", address.trackIndex },
		{ "index", address.clipIndex },
		{ "type", clipTypeName( address.clip ) },
		{ "name", address.clip->name() },
		{ "start", address.clip->startPosition().getTicks() },
		{ "length", address.clip->length().getTicks() },
		{ "autoResize", address.clip->getAutoResize() },
		{ "startTimeOffset", address.clip->startTimeOffset().getTicks() },
		{ "color", address.clip->color() ? QJsonValue( address.clip->color()->name() ) : QJsonValue() },
		{ "muted", address.clip->isMuted() }
	};

	if( const auto *midiClip = dynamic_cast<const MidiClip *>( address.clip ) )
	{
		detail.insert( "noteCount", static_cast<int>( midiClip->notes().size() ) );
	}

	return detail;
}




QString midiClipTypeName( MidiClip::Type type )
{
	return type == MidiClip::Type::BeatClip ? "beat" : "melody";
}



QJsonObject sampleInfo( const ClipAddress &address, SampleClip *sampleClip )
{
	const auto &sample = sampleClip->sample();
	double peak = 0.0;
	const auto *frames = sample.data();
	for( std::size_t index = 0; index < sample.sampleSize(); ++index )
	{
		peak = std::max( peak, std::abs( static_cast<double>( frames[index].left() ) ) );
		peak = std::max( peak, std::abs( static_cast<double>( frames[index].right() ) ) );
	}

	return {
		{ "path", clipPath( address.trackIndex, address.clipIndex, address.track ) },
		{ "source", sampleClip->sampleFile() },
		{ "frames", static_cast<double>( sample.sampleSize() ) },
		{ "sampleRate", sample.sampleRate() },
		{ "durationMs", static_cast<double>( sample.sampleDuration().count() ) },
		{ "peak", peak },
		{ "reversed", sampleClip->reversed() },
		{ "offsetTicks", sampleClip->startTimeOffset().getTicks() }
	};
}



QString mixerChannelPath( int channelIndex )
{
	return QStringLiteral( "song/channel:%1" ).arg( channelIndex );
}




QJsonObject mixerChannelDetail( MixerChannel *channel )
{
	QJsonArray sends;
	for( auto *route : channel->m_sends )
	{
		sends.append( QJsonObject{
			{ "to", route->receiverIndex() },
			{ "amount", route->amount()->value() }
		} );
	}

	QJsonArray receives;
	for( auto *route : channel->m_receives )
	{
		receives.append( QJsonObject{
			{ "from", route->senderIndex() },
			{ "amount", route->amount()->value() }
		} );
	}

	QJsonObject detail{
		{ "path", mixerChannelPath( channel->index() ) },
		{ "channel", channel->index() },
		{ "master", channel->isMaster() },
		{ "name", channel->m_name },
		{ "volume", channel->m_volumeModel.value() },
		{ "muted", channel->m_muteModel.value() },
		{ "solo", channel->m_soloModel.value() },
		{ "sends", sends },
		{ "receives", receives }
	};
	detail.insert( "color", channel->color().has_value()
		? QJsonValue( channel->color()->name() ) : QJsonValue( QJsonValue::Null ) );
	return detail;
}




bool resolveMixerChannel( Mixer *mixer, const QJsonObject &arguments, const char *name,
	MixerChannel *&channel, int &channelIndex, CommandResult &failure )
{
	QString error;
	if( !readNonNegativeInteger( arguments, name, channelIndex, error ) )
	{
		failure = invalidArguments( error );
		return false;
	}
	if( channelIndex >= mixer->numChannels() )
	{
		failure = CommandResult::failure( "mixer_channel_not_found",
			QStringLiteral( "Mixer channel %1 does not exist." ).arg( channelIndex ) );
		return false;
	}
	channel = mixer->mixerChannel( channelIndex );
	return true;
}




struct ModelAddress
{
	QString path;
	AutomatableModel *model = nullptr;
};




void appendEffectModels( std::vector<ModelAddress> &models, const QString &ownerPath,
	EffectChain *effectChain )
{
	if( effectChain == nullptr )
	{
		return;
	}

	for( int slot = 0; slot < effectChain->effectCount(); ++slot )
	{
		auto *effect = effectChain->effectAt( slot );
		if( effect == nullptr )
		{
			continue;
		}
		const QString effectPath = QStringLiteral( "%1/fx:%2" ).arg( ownerPath ).arg( slot );
		models.push_back( { effectPath + "/enabled", effect->enabledModel() } );
		models.push_back( { effectPath + "/wet", effect->wetDryModel() } );
	}
}




std::vector<ModelAddress> addressableModels( Song *song )
{
	std::vector<ModelAddress> models{
		{ "song/tempo", &song->tempoModel() },
		{ "song/masterVolume", &song->masterVolumeModel() },
		{ "song/masterPitch", &song->masterPitchModel() }
	};

	for( auto *container : { static_cast<TrackContainer *>( song ), static_cast<TrackContainer *>( Engine::patternStore() ) } )
	{
		const auto &tracks = container->tracks();
		for( std::size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex )
		{
			const auto path = trackPath( static_cast<int>( trackIndex ), tracks[trackIndex] );
			models.push_back( { path + "/mute", tracks[trackIndex]->getMutedModel() } );
			auto *instrumentTrack = dynamic_cast<InstrumentTrack *>( tracks[trackIndex] );
			if( instrumentTrack == nullptr )
			{
				if( auto *sample = dynamic_cast<SampleTrack *>( tracks[trackIndex] ) )
				{
					models.push_back( { path + "/mixerChannel", sample->mixerChannelModel() } );
					appendEffectModels( models, path, sample->audioBusHandle()->effects() );
				}
				continue;
			}

			const QString instrumentPath = path + "/instrument";
			models.push_back( { instrumentPath + "/volume", instrumentTrack->volumeModel() } );
			models.push_back( { instrumentPath + "/panning", instrumentTrack->panningModel() } );
			models.push_back( { instrumentPath + "/pitch", instrumentTrack->pitchModel() } );
			models.push_back( { instrumentPath + "/pitchRange", instrumentTrack->pitchRangeModel() } );
			models.push_back( { instrumentPath + "/baseNote", instrumentTrack->baseNoteModel() } );
			models.push_back( { instrumentPath + "/mixerChannel", instrumentTrack->mixerChannelModel() } );
			appendEffectModels( models, path,
				instrumentTrack->audioBusHandle()->effects() );
		}
	}

	auto *mixer = Engine::mixer();
	if( mixer == nullptr )
	{
		return models;
	}

	for( int channelIndex = 0; channelIndex < mixer->numChannels(); ++channelIndex )
	{
		auto *channel = mixer->mixerChannel( channelIndex );
		const QString channelPath = mixerChannelPath( channelIndex );
		models.push_back( { channelPath + "/volume", &channel->m_volumeModel } );
		models.push_back( { channelPath + "/mute", &channel->m_muteModel } );
		models.push_back( { channelPath + "/solo", &channel->m_soloModel } );
		appendEffectModels( models, channelPath, &channel->m_fxChain );
	}

	return models;
}




QString modelTypeName( const AutomatableModel *model )
{
	if( model->dynamicCast<BoolModel>() != nullptr )
	{
		return "boolean";
	}
	if( model->dynamicCast<IntModel>() != nullptr )
	{
		return "integer";
	}
	return "number";
}




QJsonObject modelDetail( const ModelAddress &address )
{
	const auto *model = address.model;
	const bool mixerChannel = address.path.endsWith( "/mixerChannel" ) && Engine::mixer();
	return {
		{ "path", address.path },
		{ "name", model->fullDisplayName() },
		{ "type", modelTypeName( model ) },
		{ "value", model->dynamicCast<BoolModel>() ? QJsonValue( model->value<bool>() ) : QJsonValue( model->value<float>() ) },
		{ "min", mixerChannel ? 0.0f : model->minValue<float>() },
		{ "max", mixerChannel ? static_cast<float>( Engine::mixer()->numChannels() - 1 ) : model->maxValue<float>() },
		{ "step", model->step<float>() },
		{ "center", model->centerValue() },
		{ "automated", model->isAutomated() }
	};
}




bool resolveModel( Song *song, const QString &path, ModelAddress &address,
	CommandResult &failure )
{
	for( const auto &candidate : addressableModels( song ) )
	{
		if( candidate.path == path )
		{
			address = candidate;
			return true;
		}
	}

	failure = CommandResult::failure( "model_not_found",
		QStringLiteral( "'%1' is not an addressable automation model." ).arg( path ) );
	return false;
}




bool validateModelValue( AutomatableModel *model, double value, QString &error )
{
	if( value < model->minValue<float>() || value > model->maxValue<float>() )
	{
		error = QStringLiteral( "'value' must be between %1 and %2." )
			.arg( model->minValue<float>() ).arg( model->maxValue<float>() );
		return false;
	}
	if( model->dynamicCast<BoolModel>() != nullptr && value != 0.0 && value != 1.0 )
	{
		error = "'value' must be 0 or 1 for a boolean model.";
		return false;
	}
	if( model->dynamicCast<IntModel>() != nullptr && std::floor( value ) != value )
	{
		error = "'value' must be an integer for an integer model.";
		return false;
	}
	return true;
}




CommandResult executeMixerGetChannel( const QJsonObject &arguments )
{
	auto *mixer = Engine::mixer();
	if( mixer == nullptr )
	{
		return mixerUnavailable();
	}

	MixerChannel *channel = nullptr;
	int channelIndex = -1;
	CommandResult failure;
	if( !resolveMixerChannel( mixer, arguments, "channel", channel, channelIndex, failure ) )
	{
		return failure;
	}
	return CommandResult::success( mixerChannelDetail( channel ) );
}





CommandResult executeMixerGetMaster( const QJsonObject & )
{
	return executeMixerGetChannel( QJsonObject{ { "channel", 0 } } );
}




CommandResult executeMixerListChannels( const QJsonObject & )
{
	auto *mixer = Engine::mixer();
	if( mixer == nullptr )
	{
		return mixerUnavailable();
	}

	QJsonArray channels;
	for( int channelIndex = 0; channelIndex < mixer->numChannels(); ++channelIndex )
	{
		channels.append( mixerChannelDetail( mixer->mixerChannel( channelIndex ) ) );
	}
	return CommandResult::success( QJsonObject{ { "channels", channels } } );
}




CommandResult executeMixerSetName( const QJsonObject &arguments )
{
	auto *mixer = Engine::mixer();
	if( mixer == nullptr )
	{
		return mixerUnavailable();
	}

	MixerChannel *channel = nullptr;
	int channelIndex = -1;
	CommandResult failure;
	if( !resolveMixerChannel( mixer, arguments, "channel", channel, channelIndex, failure ) )
	{
		return failure;
	}

	QString name;
	QString error;
	if( !readRequiredString( arguments, "name", name, error ) )
	{
		return invalidArguments( error );
	}
	channel->m_name = name;
	return CommandResult::success( mixerChannelDetail( channel ) );
}




CommandResult executeMixerSetVolume( const QJsonObject &arguments )
{
	auto *mixer = Engine::mixer();
	if( mixer == nullptr )
	{
		return mixerUnavailable();
	}

	MixerChannel *channel = nullptr;
	int channelIndex = -1;
	CommandResult failure;
	if( !resolveMixerChannel( mixer, arguments, "channel", channel, channelIndex, failure ) )
	{
		return failure;
	}

	double value = 0.0;
	QString error;
	if( !readRequiredNumber( arguments, "value", value, error ) ||
		!validateModelValue( &channel->m_volumeModel, value, error ) )
	{
		return invalidArguments( error );
	}
	channel->m_volumeModel.setValue( static_cast<float>( value ) );
	return CommandResult::success( mixerChannelDetail( channel ) );
}




CommandResult executeMixerSetMute( const QJsonObject &arguments )
{
	auto *mixer = Engine::mixer();
	if( mixer == nullptr )
	{
		return mixerUnavailable();
	}

	MixerChannel *channel = nullptr;
	int channelIndex = -1;
	CommandResult failure;
	if( !resolveMixerChannel( mixer, arguments, "channel", channel, channelIndex, failure ) )
	{
		return failure;
	}

	bool value = false;
	QString error;
	if( !readRequiredBool( arguments, "value", value, error ) )
	{
		return invalidArguments( error );
	}
	channel->m_muteModel.setValue( value );
	return CommandResult::success( mixerChannelDetail( channel ) );
}




CommandResult executeMixerSetSolo( const QJsonObject &arguments )
{
	auto *mixer = Engine::mixer();
	if( mixer == nullptr )
	{
		return mixerUnavailable();
	}

	MixerChannel *channel = nullptr;
	int channelIndex = -1;
	CommandResult failure;
	if( !resolveMixerChannel( mixer, arguments, "channel", channel, channelIndex, failure ) )
	{
		return failure;
	}
	if( channel->isMaster() )
	{
		return invalidArguments( "The master channel cannot be soloed." );
	}

	bool value = false;
	QString error;
	if( !readRequiredBool( arguments, "value", value, error ) )
	{
		return invalidArguments( error );
	}
	if( channel->m_soloModel.value() != value )
	{
		channel->m_soloModel.setValue( value );
		mixer->toggledSolo();
	}
	return CommandResult::success( mixerChannelDetail( channel ) );
}




CommandResult executeMixerSetColor( const QJsonObject &arguments )
{
	auto *mixer = Engine::mixer();
	if( mixer == nullptr )
	{
		return mixerUnavailable();
	}

	MixerChannel *channel = nullptr;
	int channelIndex = -1;
	CommandResult failure;
	if( !resolveMixerChannel( mixer, arguments, "channel", channel, channelIndex, failure ) )
	{
		return failure;
	}

	QString value;
	QString error;
	if( !readRequiredString( arguments, "value", value, error, true ) )
	{
		return invalidArguments( error );
	}
	if( value.isEmpty() )
	{
		channel->setColor( std::nullopt );
	}
	else
	{
		const QColor color( value );
		if( !color.isValid() )
		{
			return invalidArguments( "'value' must be a valid color name or an empty string." );
		}
		channel->setColor( color );
	}
	return CommandResult::success( mixerChannelDetail( channel ) );
}




CommandResult executeMixerAddSend( const QJsonObject &arguments )
{
	auto *mixer = Engine::mixer();
	if( mixer == nullptr )
	{
		return mixerUnavailable();
	}

	MixerChannel *fromChannel = nullptr;
	MixerChannel *toChannel = nullptr;
	int fromIndex = -1;
	int toIndex = -1;
	CommandResult failure;
	if( !resolveMixerChannel( mixer, arguments, "from", fromChannel, fromIndex, failure ) ||
		!resolveMixerChannel( mixer, arguments, "to", toChannel, toIndex, failure ) )
	{
		return failure;
	}

	double amount = 1.0;
	QString error;
	if( arguments.contains( "amount" ) && !readRequiredNumber( arguments, "amount", amount, error ) )
	{
		return invalidArguments( error );
	}
	if( amount < 0.0 || amount > 1.0 )
	{
		return invalidArguments( "'amount' must be between 0 and 1." );
	}
	if( mixer->isInfiniteLoop( fromIndex, toIndex ) )
	{
		return CommandResult::failure( "mixer_send_loop",
			"The requested send would create a mixer routing loop." );
	}

	if( mixer->createChannelSend( fromIndex, toIndex, static_cast<float>( amount ) ) == nullptr )
	{
		return CommandResult::failure( "mixer_send_failed", "LMMS could not create the requested send." );
	}
	return CommandResult::success( mixerChannelDetail( fromChannel ) );
}




CommandResult executeMixerSetSendAmount( const QJsonObject &arguments )
{
	auto *mixer = Engine::mixer();
	if( mixer == nullptr )
	{
		return mixerUnavailable();
	}

	MixerChannel *fromChannel = nullptr;
	MixerChannel *toChannel = nullptr;
	int fromIndex = -1;
	int toIndex = -1;
	CommandResult failure;
	if( !resolveMixerChannel( mixer, arguments, "from", fromChannel, fromIndex, failure ) ||
		!resolveMixerChannel( mixer, arguments, "to", toChannel, toIndex, failure ) )
	{
		return failure;
	}

	double amount = 0.0;
	QString error;
	if( !readRequiredNumber( arguments, "amount", amount, error ) )
	{
		return invalidArguments( error );
	}
	if( amount < 0.0 || amount > 1.0 )
	{
		return invalidArguments( "'amount' must be between 0 and 1." );
	}

	auto *amountModel = mixer->channelSendModel( fromIndex, toIndex );
	if( amountModel == nullptr )
	{
		return CommandResult::failure( "mixer_send_not_found", "The requested mixer send does not exist." );
	}
	amountModel->setValue( static_cast<float>( amount ) );
	return CommandResult::success( mixerChannelDetail( fromChannel ) );
}




CommandResult executeMixerRemoveSend( const QJsonObject &arguments )
{
	auto *mixer = Engine::mixer();
	if( mixer == nullptr )
	{
		return mixerUnavailable();
	}

	MixerChannel *fromChannel = nullptr;
	MixerChannel *toChannel = nullptr;
	int fromIndex = -1;
	int toIndex = -1;
	CommandResult failure;
	if( !resolveMixerChannel( mixer, arguments, "from", fromChannel, fromIndex, failure ) ||
		!resolveMixerChannel( mixer, arguments, "to", toChannel, toIndex, failure ) )
	{
		return failure;
	}
	if( mixer->channelSendModel( fromIndex, toIndex ) == nullptr )
	{
		return CommandResult::failure( "mixer_send_not_found", "The requested mixer send does not exist." );
	}
	mixer->deleteChannelSend( fromIndex, toIndex );
	return CommandResult::success( mixerChannelDetail( fromChannel ) );
}




CommandResult executeMixerClearChannel( const QJsonObject &arguments )
{
	auto *mixer = Engine::mixer();
	if( mixer == nullptr )
	{
		return mixerUnavailable();
	}

	MixerChannel *channel = nullptr;
	int channelIndex = -1;
	CommandResult failure;
	if( !resolveMixerChannel( mixer, arguments, "channel", channel, channelIndex, failure ) )
	{
		return failure;
	}
	mixer->clearChannel( channelIndex );
	return CommandResult::success( mixerChannelDetail( channel ) );
}




CommandResult executeModelGetValue( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	QString path;
	QString error;
	if( !readRequiredString( arguments, "path", path, error ) )
	{
		return invalidArguments( error );
	}

	ModelAddress address;
	CommandResult failure;
	if( !resolveModel( song, path, address, failure ) )
	{
		return failure;
	}
	return CommandResult::success( modelDetail( address ) );
}




CommandResult executeModelSetValue( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	QString path;
	double value = 0.0;
	QString error;
	if( !readRequiredString( arguments, "path", path, error ) )
	{
		return invalidArguments( error );
	}

	ModelAddress address;
	CommandResult failure;
	if( !resolveModel( song, path, address, failure ) )
	{
		return failure;
	}
	if( arguments.value( "value" ).isBool() )
	{
		if( !address.model->dynamicCast<BoolModel>() ) { return invalidArguments( "Boolean values require a boolean model." ); }
		value = arguments.value( "value" ).toBool() ? 1.0 : 0.0;
	}
	else if( !readRequiredNumber( arguments, "value", value, error ) ) { return invalidArguments( error ); }
	if( address.path.endsWith( "/mixerChannel" ) && Engine::mixer() )
	{
		address.model->setRange( 0, Engine::mixer()->numChannels() - 1, 1 );
	}
	if( !validateModelValue( address.model, value, error ) )
	{
		return invalidArguments( error );
	}
	address.model->setValue( static_cast<float>( value ) );
	return CommandResult::success( modelDetail( address ) );
}




CommandResult executeModelList( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	bool hasPrefix = false;
	QString prefix;
	QString error;
	if( !readOptionalString( arguments, "prefix", hasPrefix, prefix, error ) )
	{
		return invalidArguments( error );
	}

	QJsonArray models;
	for( const auto &address : addressableModels( song ) )
	{
		if( !hasPrefix || address.path.startsWith( prefix, Qt::CaseInsensitive ) )
		{
			models.append( modelDetail( address ) );
		}
	}
	return CommandResult::success( QJsonObject{ { "models", models } } );
}




CommandResult executeModelSearch( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	bool hasKeyword = false;
	QString keyword;
	QString error;
	if( !readOptionalString( arguments, "keyword", hasKeyword, keyword, error ) )
	{
		return invalidArguments( error );
	}

	QJsonArray models;
	for( const auto &address : addressableModels( song ) )
	{
		const auto detail = modelDetail( address );
		if( !hasKeyword || address.path.contains( keyword, Qt::CaseInsensitive ) ||
			detail.value( "name" ).toString().contains( keyword, Qt::CaseInsensitive ) )
		{
			models.append( detail );
		}
	}
	return CommandResult::success( QJsonObject{ { "models", models } } );
}




struct EffectOwner
{
	QString path;
	EffectChain *effectChain = nullptr;
};




bool parseOwnerIndex( const QString &owner, const QString &prefix, int &index )
{
	if( !owner.startsWith( prefix, Qt::CaseInsensitive ) )
	{
		return false;
	}

	bool ok = false;
	const auto suffix = owner.sliced( prefix.size() );
	index = suffix.toInt( &ok );
	return ok && index >= 0;
}




bool resolveEffectOwner( Song *song, const QJsonObject &arguments, EffectOwner &owner,
	CommandResult &failure )
{
	QString ownerArgument;
	QString error;
	if( !readRequiredString( arguments, "owner", ownerArgument, error ) )
	{
		failure = invalidArguments( error );
		return false;
	}

	QString normalizedOwner = ownerArgument;
	const bool patternOwner = normalizedOwner.startsWith( "pattern/", Qt::CaseInsensitive );
	if( patternOwner ) { normalizedOwner = normalizedOwner.sliced( 8 ); }
	if( normalizedOwner.startsWith( "song/", Qt::CaseInsensitive ) )
	{
		normalizedOwner = normalizedOwner.sliced( 5 );
	}

	int index = -1;
	if( parseOwnerIndex( normalizedOwner, "channel:", index ) )
	{
		if( patternOwner ) { failure = invalidArguments( "Mixer channels belong to song, not pattern." ); return false; }
		auto *mixer = Engine::mixer();
		if( mixer == nullptr )
		{
			failure = mixerUnavailable();
			return false;
		}
		if( index >= mixer->numChannels() )
		{
			failure = CommandResult::failure( "mixer_channel_not_found",
				QStringLiteral( "Mixer channel %1 does not exist." ).arg( index ) );
			return false;
		}
		owner.path = mixerChannelPath( index );
		owner.effectChain = &mixer->mixerChannel( index )->m_fxChain;
		return true;
	}

	if( parseOwnerIndex( normalizedOwner, "track:", index ) )
	{
		const auto &tracks = patternOwner ? Engine::patternStore()->tracks() : song->tracks();
		if( index >= static_cast<int>( tracks.size() ) )
		{
			failure = CommandResult::failure( "track_not_found",
				QStringLiteral( "Track %1 does not exist." ).arg( index ) );
			return false;
		}

		if( auto *instrumentTrack = dynamic_cast<InstrumentTrack *>( tracks[index] ) )
		{
			owner.path = trackPath( index, tracks[index] );
			owner.effectChain = instrumentTrack->audioBusHandle()->effects();
			return true;
		}
		if( auto *sampleTrack = dynamic_cast<SampleTrack *>( tracks[index] ) )
		{
			owner.path = trackPath( index, tracks[index] );
			owner.effectChain = sampleTrack->audioBusHandle()->effects();
			return true;
		}

		failure = CommandResult::failure( "wrong_effect_owner_type",
			QStringLiteral( "Track %1 does not provide an effect chain." ).arg( index ) );
		return false;
	}

	failure = CommandResult::failure( "invalid_effect_owner",
		"'owner' must identify a mixer channel or an instrument/sample track." );
	return false;
}




QJsonObject effectDetail( const EffectOwner &owner, int slot, Effect *effect )
{
	const auto *descriptor = effect->descriptor();
	return {
		{ "path", QStringLiteral( "%1/fx:%2" ).arg( owner.path ).arg( slot ) },
		{ "owner", owner.path },
		{ "slot", slot },
		{ "plugin", descriptor != nullptr ? QString::fromUtf8( descriptor->name ) : QString() },
		{ "name", descriptor != nullptr ? effect->displayName() : effect->Model::displayName() },
		{ "enabled", effect->enabledModel()->value() },
		{ "wetDry", effect->wetDryModel()->value() }
	};
}

QJsonArray trackEffects( Track *track )
{
	EffectChain *chain = nullptr;
	if( auto *instrument = dynamic_cast<InstrumentTrack *>( track ) ) { chain = instrument->audioBusHandle()->effects(); }
	if( auto *sample = dynamic_cast<SampleTrack *>( track ) ) { chain = sample->audioBusHandle()->effects(); }
	QJsonArray result;
	if( !chain ) { return result; }
	const auto &tracks = track->trackContainer()->tracks();
	const auto index = static_cast<int>( std::distance( tracks.begin(), std::find( tracks.begin(), tracks.end(), track ) ) );
	EffectOwner owner;
	owner.path = trackPath( index, track );
	owner.effectChain = chain;
	for( int slot = 0; slot < chain->effectCount(); ++slot )
	{
		result.append( effectDetail( owner, slot, chain->effectAt( slot ) ) );
	}
	return result;
}




bool resolveEffect( Song *song, const QJsonObject &arguments, EffectOwner &owner, Effect *&effect,
	int &slot, CommandResult &failure )
{
	if( !resolveEffectOwner( song, arguments, owner, failure ) )
	{
		return false;
	}

	QString error;
	if( !readNonNegativeInteger( arguments, "slot", slot, error ) )
	{
		failure = invalidArguments( error );
		return false;
	}
	if( slot >= owner.effectChain->effectCount() )
	{
		failure = CommandResult::failure( "effect_not_found",
			QStringLiteral( "Effect slot %1 does not exist on '%2'." ).arg( slot ).arg( owner.path ) );
		return false;
	}
	effect = owner.effectChain->effectAt( slot );
	return true;
}




bool isSerializedMetadata( const QString &name )
{
	return name == "id" || name == "nodename" || name == "scale_type" || name == "value";
}




QJsonValue serializedParameterValue( const QString &value )
{
	if( value == "true" )
	{
		return true;
	}
	if( value == "false" )
	{
		return false;
	}

	bool ok = false;
	const double number = value.toDouble( &ok );
	return ok && std::isfinite( number ) ? QJsonValue( number ) : QJsonValue( value );
}




QString serializedParameterText( const QJsonValue &value )
{
	if( value.isBool() )
	{
		return value.toBool() ? "true" : "false";
	}
	if( value.isDouble() )
	{
		return QString::number( value.toDouble(), 'g', 15 );
	}
	return value.toString();
}




void appendSerializedParameters( const QDomElement &element, const QString &prefix,
	QJsonArray &parameters )
{
	const auto attributes = element.attributes();
	for( int index = 0; index < attributes.count(); ++index )
	{
		const auto attribute = attributes.item( index ).toAttr();
		if( isSerializedMetadata( attribute.name() ) )
		{
			continue;
		}
		parameters.append( QJsonObject{
			{ "name", prefix.isEmpty() ? attribute.name() : prefix + "/" + attribute.name() },
			{ "value", serializedParameterValue( attribute.value() ) }
		} );
	}

	for( auto child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement() )
	{
		if( child.tagName() == "connection" )
		{
			continue;
		}

		const QString childName = child.attribute( "nodename", child.tagName() );
		const QString childPath = prefix.isEmpty() ? childName : prefix + "/" + childName;
		if( child.hasAttribute( "value" ) )
		{
			parameters.append( QJsonObject{
				{ "name", childPath },
				{ "value", serializedParameterValue( child.attribute( "value" ) ) }
			} );
		}
		appendSerializedParameters( child, childPath, parameters );
	}
}




bool setSerializedParameter( QDomElement &element, const QString &prefix, const QString &name,
	const QString &value )
{
	if( !prefix.isEmpty() && prefix == name && element.hasAttribute( "value" ) )
	{
		element.setAttribute( "value", value );
		return true;
	}

	const auto attributes = element.attributes();
	for( int index = 0; index < attributes.count(); ++index )
	{
		const auto attribute = attributes.item( index ).toAttr();
		if( isSerializedMetadata( attribute.name() ) )
		{
			continue;
		}
		const QString attributePath = prefix.isEmpty() ? attribute.name() : prefix + "/" + attribute.name();
		if( attributePath == name )
		{
			element.setAttribute( attribute.name(), value );
			return true;
		}
	}

	for( auto child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement() )
	{
		if( child.tagName() == "connection" )
		{
			continue;
		}

		const QString childName = child.attribute( "nodename", child.tagName() );
		const QString childPath = prefix.isEmpty() ? childName : prefix + "/" + childName;
		if( childPath == name && child.hasAttribute( "value" ) )
		{
			child.setAttribute( "value", value );
			return true;
		}
		if( setSerializedParameter( child, childPath, name, value ) )
		{
			return true;
		}
	}

	return false;
}




bool readRequiredSerializedValue( const QJsonObject &arguments, const char *name,
	QJsonValue &value, QString &error )
{
	if( !arguments.contains( argumentName( name ) ) )
	{
		error = QStringLiteral( "Missing required argument '%1'." ).arg( argumentName( name ) );
		return false;
	}

	value = arguments.value( argumentName( name ) );
	if( value.isDouble() )
	{
		if( std::isfinite( value.toDouble() ) )
		{
			return true;
		}
	}
	else if( value.isBool() || value.isString() )
	{
		return true;
	}

	error = QStringLiteral( "'%1' must be a finite number, boolean, or string." ).arg( argumentName( name ) );
	return false;
}




CommandResult executeEffectListAvailable( const QJsonObject &arguments )
{
	auto *pluginFactory = PluginFactory::instance();
	if( pluginFactory == nullptr )
	{
		return CommandResult::failure( "plugin_factory_unavailable", "The plugin factory is unavailable." );
	}

	bool hasKind = false;
	QString kind;
	QString error;
	if( !readOptionalString( arguments, "kind", hasKind, kind, error ) )
	{
		return invalidArguments( error );
	}

	Plugin::Type type = Plugin::Type::Effect;
	if( hasKind && kind.compare( "effect", Qt::CaseInsensitive ) != 0 )
	{
		if( kind.compare( "instrument", Qt::CaseInsensitive ) == 0 )
		{
			type = Plugin::Type::Instrument;
		}
		else
		{
			return invalidArguments( "'kind' must be 'effect' or 'instrument'." );
		}
	}

	QJsonArray plugins;
	for( const auto *descriptor : pluginFactory->descriptors( type ) )
	{
		if( descriptor == nullptr )
		{
			continue;
		}
		QJsonArray subKeys;
		if( descriptor->subPluginFeatures )
		{
			Plugin::Descriptor::SubPluginFeatures::KeyList keys;
			descriptor->subPluginFeatures->listSubPluginKeys( descriptor, keys );
			for( const auto &key : keys )
			{
				QJsonObject attributes;
				for( auto it = key.attributes.begin(); it != key.attributes.end(); ++it ) { attributes.insert( it.key(), it.value() ); }
				subKeys.append( QJsonObject{ { "name", key.name }, { "attributes", attributes } } );
			}
		}
		plugins.append( QJsonObject{
			{ "plugin", QString::fromUtf8( descriptor->name ) },
			{ "name", QString::fromUtf8( descriptor->displayName ) },
			{ "description", QString::fromUtf8( descriptor->description ) },
			{ "author", QString::fromUtf8( descriptor->author ) },
			{ "subKeys", subKeys }
		} );
	}
	return CommandResult::success( QJsonObject{ { "kind", type == Plugin::Type::Effect ? "effect" : "instrument" },
		{ "plugins", plugins } } );
}




bool readSubPluginKey( const Plugin::Descriptor *descriptor, const QJsonObject &arguments,
	std::optional<Plugin::Descriptor::SubPluginFeatures::Key> &key, CommandResult &failure );

CommandResult executeEffectAdd( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	EffectOwner owner;
	CommandResult failure;
	if( !resolveEffectOwner( song, arguments, owner, failure ) )
	{
		return failure;
	}

	QString plugin;
	QString error;
	if( !readRequiredString( arguments, "plugin", plugin, error ) )
	{
		return invalidArguments( error );
	}

	bool hasIndex = false;
	int index = -1;
	if( !readOptionalInteger( arguments, "index", hasIndex, index, error ) )
	{
		return invalidArguments( error );
	}
	if( hasIndex && ( index < 0 || index > owner.effectChain->effectCount() ) )
	{
		return invalidArguments( "'index' must identify an insertion slot in the effect chain." );
	}

	auto *pluginFactory = PluginFactory::instance();
	if( pluginFactory == nullptr )
	{
		return CommandResult::failure( "plugin_factory_unavailable", "The plugin factory is unavailable." );
	}
	const QByteArray pluginName = plugin.toUtf8();
	const auto pluginInfo = pluginFactory->pluginInfo( pluginName.constData() );
	if( pluginInfo.isNull() || pluginInfo.descriptor == nullptr ||
		pluginInfo.descriptor->type != Plugin::Type::Effect )
	{
		return CommandResult::failure( "effect_not_found",
			QStringLiteral( "'%1' is not an available effect plugin." ).arg( plugin ) );
	}

	std::optional<Plugin::Descriptor::SubPluginFeatures::Key> subKey;
	if( !readSubPluginKey( pluginInfo.descriptor, arguments, subKey, failure ) ) { return failure; }
	auto guard = Engine::audioEngine()->requestChangesGuard();
	auto *effect = Effect::instantiate( plugin, owner.effectChain, subKey ? &*subKey : nullptr );
	if( effect == nullptr || !effect->isOkay() )
	{
		delete effect;
		return CommandResult::failure( "effect_load_failed",
			QStringLiteral( "LMMS could not load effect '%1'." ).arg( plugin ) );
	}

	owner.effectChain->appendEffect( effect );
	int slot = owner.effectChain->effectCount() - 1;
	if( hasIndex )
	{
		while( slot > index )
		{
			owner.effectChain->moveUp( effect );
			--slot;
		}
		emit owner.effectChain->dataChanged();
	}
	return CommandResult::success( effectDetail( owner, slot, effect ) );
}




CommandResult executeEffectRemove( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	EffectOwner owner;
	Effect *effect = nullptr;
	int slot = -1;
	CommandResult failure;
	if( !resolveEffect( song, arguments, owner, effect, slot, failure ) )
	{
		return failure;
	}

	const QString path = QStringLiteral( "%1/fx:%2" ).arg( owner.path ).arg( slot );
	owner.effectChain->removeEffect( effect );
	effect->deleteLater();
	return CommandResult::success( QJsonObject{ { "removed", path } } );
}




CommandResult executeEffectMove( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	EffectOwner owner;
	Effect *effect = nullptr;
	int slot = -1;
	CommandResult failure;
	if( !resolveEffect( song, arguments, owner, effect, slot, failure ) )
	{
		return failure;
	}

	bool hasIndex = false;
	int index = -1;
	QString error;
	if( !readOptionalInteger( arguments, "index", hasIndex, index, error ) )
	{
		return invalidArguments( error );
	}
	if( !hasIndex )
	{
		return invalidArguments( "Missing required argument 'index'." );
	}
	if( index < 0 || index >= owner.effectChain->effectCount() )
	{
		return invalidArguments( "'index' must identify an existing effect slot." );
	}

	auto guard = Engine::audioEngine()->requestChangesGuard();
	while( slot > index )
	{
		owner.effectChain->moveUp( effect );
		--slot;
	}
	while( slot < index )
	{
		owner.effectChain->moveDown( effect );
		++slot;
	}
	emit owner.effectChain->dataChanged();
	return CommandResult::success( effectDetail( owner, slot, effect ) );
}




CommandResult executeEffectSetEnabled( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	EffectOwner owner;
	Effect *effect = nullptr;
	int slot = -1;
	CommandResult failure;
	if( !resolveEffect( song, arguments, owner, effect, slot, failure ) )
	{
		return failure;
	}

	bool value = false;
	QString error;
	if( !readRequiredBool( arguments, "value", value, error ) )
	{
		return invalidArguments( error );
	}
	effect->enabledModel()->setValue( value );
	return CommandResult::success( effectDetail( owner, slot, effect ) );
}




CommandResult executeEffectSetWetDry( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	EffectOwner owner;
	Effect *effect = nullptr;
	int slot = -1;
	CommandResult failure;
	if( !resolveEffect( song, arguments, owner, effect, slot, failure ) )
	{
		return failure;
	}

	double value = 0.0;
	QString error;
	if( !readRequiredNumber( arguments, "value", value, error ) ||
		!validateModelValue( effect->wetDryModel(), value, error ) )
	{
		return invalidArguments( error );
	}
	effect->wetDryModel()->setValue( static_cast<float>( value ) );
	return CommandResult::success( effectDetail( owner, slot, effect ) );
}




QJsonObject serializedParameters( const QDomElement &element )
{
	QJsonArray parameters;
	appendSerializedParameters( element, QString(), parameters );
	return { { "parameters", parameters } };
}




CommandResult executeEffectGetParams( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	EffectOwner owner;
	Effect *effect = nullptr;
	int slot = -1;
	CommandResult failure;
	if( !resolveEffect( song, arguments, owner, effect, slot, failure ) )
	{
		return failure;
	}

	QDomDocument document;
	auto root = document.createElement( "agent" );
	document.appendChild( root );
	const auto state = effect->controls()->saveState( document, root );
	auto data = effectDetail( owner, slot, effect );
	data.insert( "parameters", serializedParameters( state ).value( "parameters" ) );
	return CommandResult::success( data );
}




CommandResult executeEffectSetParam( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	EffectOwner owner;
	Effect *effect = nullptr;
	int slot = -1;
	CommandResult failure;
	if( !resolveEffect( song, arguments, owner, effect, slot, failure ) )
	{
		return failure;
	}

	QString name;
	QJsonValue value;
	QString error;
	if( !readRequiredString( arguments, "name", name, error ) ||
		!readRequiredSerializedValue( arguments, "value", value, error ) )
	{
		return invalidArguments( error );
	}

	QDomDocument document;
	auto root = document.createElement( "agent" );
	document.appendChild( root );
	auto state = effect->controls()->saveState( document, root );
	if( !setSerializedParameter( state, QString(), name, serializedParameterText( value ) ) )
	{
		return CommandResult::failure( "effect_parameter_not_found",
			QStringLiteral( "Effect parameter '%1' does not exist." ).arg( name ) );
	}
	auto guard = Engine::audioEngine()->requestChangesGuard();
	effect->controls()->restoreState( state );

	auto data = effectDetail( owner, slot, effect );
	data.insert( "parameters", serializedParameters( state ).value( "parameters" ) );
	return CommandResult::success( data );
}




CommandResult executeInstrumentGetParams( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	InstrumentTrack *track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if( !resolveInstrumentTrack( song, arguments, track, trackIndex, failure ) )
	{
		return failure;
	}
	if( track->instrument() == nullptr )
	{
		return CommandResult::failure( "instrument_not_loaded", "The instrument track has no loaded instrument." );
	}

	QDomDocument document;
	auto root = document.createElement( "agent" );
	document.appendChild( root );
	const auto state = track->instrument()->saveState( document, root );
	auto data = serializedParameters( state );
	data.insert( "path", trackPath( trackIndex, track ) );
	data.insert( "plugin", track->instrumentName() );
	return CommandResult::success( data );
}




CommandResult executeInstrumentSetParam( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	InstrumentTrack *track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if( !resolveInstrumentTrack( song, arguments, track, trackIndex, failure ) )
	{
		return failure;
	}
	if( track->instrument() == nullptr )
	{
		return CommandResult::failure( "instrument_not_loaded", "The instrument track has no loaded instrument." );
	}

	QString name;
	QJsonValue value;
	QString error;
	if( !readRequiredString( arguments, "name", name, error ) ||
		!readRequiredSerializedValue( arguments, "value", value, error ) )
	{
		return invalidArguments( error );
	}

	QDomDocument document;
	auto root = document.createElement( "agent" );
	document.appendChild( root );
	auto state = track->instrument()->saveState( document, root );
	if( !setSerializedParameter( state, QString(), name, serializedParameterText( value ) ) )
	{
		return CommandResult::failure( "instrument_parameter_not_found",
			QStringLiteral( "Instrument parameter '%1' does not exist." ).arg( name ) );
	}
	track->instrument()->restoreState( state );

	auto data = serializedParameters( state );
	data.insert( "path", trackPath( trackIndex, track ) );
	data.insert( "plugin", track->instrumentName() );
	return CommandResult::success( data );
}




bool resolveAutomationTrack( Song *song, const QJsonObject &arguments,
	AutomationTrack *&automationTrack, int &trackIndex, CommandResult &failure )
{
	Track *track = nullptr;
	if( !resolveTrack( song, arguments, track, trackIndex, failure ) )
	{
		return false;
	}

	automationTrack = dynamic_cast<AutomationTrack *>( track );
	if( automationTrack == nullptr )
	{
		failure = CommandResult::failure( "wrong_track_type",
			QStringLiteral( "Track %1 is not an automation track." ).arg( trackIndex ) );
		return false;
	}
	return true;
}




bool resolveAutomationClip( Song *song, const QJsonObject &arguments,
	AutomationClip *&automationClip, ClipAddress &address, CommandResult &failure )
{
	if( !resolveClip( song, arguments, address, failure ) )
	{
		return false;
	}

	automationClip = dynamic_cast<AutomationClip *>( address.clip );
	if( automationClip == nullptr )
	{
		failure = CommandResult::failure( "wrong_clip_type",
			QStringLiteral( "Clip %1 on track %2 is not an automation clip." )
				.arg( address.clipIndex ).arg( address.trackIndex ) );
		return false;
	}
	return true;
}




QString progressionTypeName( AutomationClip::ProgressionType progressionType )
{
	switch( progressionType )
	{
	case AutomationClip::ProgressionType::Discrete: return "discrete";
	case AutomationClip::ProgressionType::Linear: return "linear";
	case AutomationClip::ProgressionType::CubicHermite: return "cubicHermite";
	}
	return "unknown";
}




bool progressionTypeFromName( const QString &name, AutomationClip::ProgressionType &progressionType )
{
	if( name.compare( "discrete", Qt::CaseInsensitive ) == 0 )
	{
		progressionType = AutomationClip::ProgressionType::Discrete;
		return true;
	}
	if( name.compare( "linear", Qt::CaseInsensitive ) == 0 )
	{
		progressionType = AutomationClip::ProgressionType::Linear;
		return true;
	}
	if( name.compare( "cubichermite", Qt::CaseInsensitive ) == 0 ||
		name.compare( "cubicHermite", Qt::CaseInsensitive ) == 0 )
	{
		progressionType = AutomationClip::ProgressionType::CubicHermite;
		return true;
	}
	return false;
}




QString modelPathForPointer( Song *song, const AutomatableModel *model )
{
	for( const auto &candidate : addressableModels( song ) )
	{
		if( candidate.model == model )
		{
			return candidate.path;
		}
	}
	return QString();
}




QJsonObject automationClipDetail( Song *song, const ClipAddress &address, AutomationClip *automationClip )
{
	auto detail = clipDetail( address );
	QJsonArray targets;
	for( const auto &target : automationClip->objects() )
	{
		if( target == nullptr )
		{
			continue;
		}
		const QString path = modelPathForPointer( song, target );
		if( !path.isEmpty() )
		{
			targets.append( modelDetail( { path, target } ) );
		}
	}

	QJsonArray nodes;
	for( auto iterator = automationClip->getTimeMap().cbegin();
		iterator != automationClip->getTimeMap().cend(); ++iterator )
	{
		const auto &node = iterator.value();
		nodes.append( QJsonObject{
			{ "position", iterator.key() },
			{ "inValue", node.getInValue() },
			{ "outValue", node.getOutValue() }
		} );
	}

	detail.insert( "targets", targets );
	detail.insert( "nodes", nodes );
	detail.insert( "progression", progressionTypeName( automationClip->progressionType() ) );
	detail.insert( "tension", automationClip->getTension() );
	return detail;
}




bool readAutomationPosition( const QJsonObject &arguments, const char *name, int &position,
	QString &error )
{
	if( !readNonNegativeInteger( arguments, name, position, error ) )
	{
		return false;
	}
	if( position > MaxSongLength )
	{
		error = "The automation position exceeds the maximum song length.";
		return false;
	}
	return true;
}




bool validateAutomationValue( AutomationClip *automationClip, double value, QString &error )
{
	const auto &targets = automationClip->objects();
	if( targets.empty() )
	{
		error = "An automation clip must have at least one target before writing values.";
		return false;
	}
	for( const auto &target : targets )
	{
		if( !target.isNull() && !validateModelValue( target.data(), value, error ) )
		{
			return false;
		}
	}
	return true;
}




float automationStoredValue( AutomationClip *automationClip, double value )
{
	return automationClip->objects().front()->inverseScaledValue( static_cast<float>( value ) );
}




struct AutomationValues
{
	int position = 0;
	double inValue = 0.0;
	double outValue = 0.0;
};




bool readAutomationValues( const QJsonArray &nodes, AutomationClip *automationClip,
	std::vector<AutomationValues> &values, QString &error )
{
	if( nodes.isEmpty() )
	{
		error = "'nodes' must contain at least one automation node.";
		return false;
	}

	for( const auto &nodeValue : nodes )
	{
		if( !nodeValue.isObject() )
		{
			error = "Each automation node must be an object.";
			return false;
		}

		const auto node = nodeValue.toObject();
		AutomationValues valuesAtPosition;
		if( !readAutomationPosition( node, "pos", valuesAtPosition.position, error ) ||
			!readRequiredNumber( node, "inValue", valuesAtPosition.inValue, error ) )
		{
			return false;
		}
		bool hasOutValue = false;
		if( !readOptionalNumber( node, "outValue", hasOutValue, valuesAtPosition.outValue, error ) )
		{
			return false;
		}
		if( !hasOutValue )
		{
			valuesAtPosition.outValue = valuesAtPosition.inValue;
		}
		if( !validateAutomationValue( automationClip, valuesAtPosition.inValue, error ) ||
			!validateAutomationValue( automationClip, valuesAtPosition.outValue, error ) )
		{
			return false;
		}
		values.push_back( valuesAtPosition );
	}
	return true;
}




void putAutomationValues( AutomationClip *automationClip, const std::vector<AutomationValues> &values )
{
	for( const auto &value : values )
	{
		automationClip->putValues( TimePos( value.position ),
			automationStoredValue( automationClip, value.inValue ),
			automationStoredValue( automationClip, value.outValue ), false, true );
	}
}




CommandResult executeTrackCreate( const QJsonObject &arguments );

CommandResult executeAutomationCreateTrack( QJsonObject arguments )
{
	arguments.insert( "type", "automation" );
	return executeTrackCreate( arguments );
}




CommandResult executeAutomationAddClip( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	AutomationTrack *track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if( !resolveAutomationTrack( song, arguments, track, trackIndex, failure ) )
	{
		return failure;
	}

	int position = 0;
	int length = 0;
	QString error;
	if( !readAutomationPosition( arguments, "position", position, error ) ||
		!readRequiredInteger( arguments, "length", length, error ) )
	{
		return invalidArguments( error );
	}
	if( length <= 0 || length > MaxSongLength - position )
	{
		return invalidArguments( "The automation clip position and length must fit within the maximum song length." );
	}

	bool hasName = false;
	QString name;
	if( !readOptionalString( arguments, "name", hasName, name, error ) )
	{
		return invalidArguments( error );
	}

	auto *automationClip = new AutomationClip( track );
	automationClip->movePosition( TimePos( position ) );
	automationClip->changeLength( TimePos( length ) );
	if( hasName )
	{
		automationClip->setName( name );
	}
	ClipAddress address{ track, automationClip, trackIndex, indexOfClip( track, automationClip ) };
	return CommandResult::success( automationClipDetail( song, address, automationClip ) );
}




CommandResult executeAutomationAddTarget( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	AutomationClip *automationClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveAutomationClip( song, arguments, automationClip, address, failure ) )
	{
		return failure;
	}

	QString targetPath;
	QString error;
	if( !readRequiredString( arguments, "target", targetPath, error ) )
	{
		return invalidArguments( error );
	}
	ModelAddress target;
	if( !resolveModel( song, targetPath, target, failure ) )
	{
		return failure;
	}
	if( !automationClip->addObject( target.model, true ) )
	{
		return CommandResult::failure( "automation_target_exists",
			QStringLiteral( "'%1' is already an automation target for this clip." ).arg( targetPath ) );
	}

	if( arguments.contains( "nodes" ) )
	{
		const auto nodeValue = arguments.value( "nodes" );
		if( !nodeValue.isArray() )
		{
			return invalidArguments( "'nodes' must be an array." );
		}
		std::vector<AutomationValues> values;
		if( !readAutomationValues( nodeValue.toArray(), automationClip, values, error ) )
		{
			return invalidArguments( error );
		}
		putAutomationValues( automationClip, values );
	}
	return CommandResult::success( automationClipDetail( song, address, automationClip ) );
}




CommandResult executeAutomationPutValue( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	AutomationClip *automationClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveAutomationClip( song, arguments, automationClip, address, failure ) )
	{
		return failure;
	}

	int position = 0;
	double value = 0.0;
	QString error;
	if( !readAutomationPosition( arguments, "pos", position, error ) ||
		!readRequiredNumber( arguments, "value", value, error ) ||
		!validateAutomationValue( automationClip, value, error ) )
	{
		return invalidArguments( error );
	}
	automationClip->putValue( TimePos( position ), automationStoredValue( automationClip, value ), false, true );
	auto data = automationClipDetail( song, address, automationClip );
	data.insert( "valueAt", automationClip->valueAt( TimePos( position ) ) );
	return CommandResult::success( data );
}




CommandResult executeAutomationPutValues( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	AutomationClip *automationClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveAutomationClip( song, arguments, automationClip, address, failure ) )
	{
		return failure;
	}

	const auto nodeValue = arguments.value( "nodes" );
	if( !nodeValue.isArray() )
	{
		return invalidArguments( "'nodes' must be an array." );
	}
	std::vector<AutomationValues> values;
	QString error;
	if( !readAutomationValues( nodeValue.toArray(), automationClip, values, error ) )
	{
		return invalidArguments( error );
	}
	putAutomationValues( automationClip, values );
	return CommandResult::success( automationClipDetail( song, address, automationClip ) );
}




CommandResult executeAutomationRemoveNode( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	AutomationClip *automationClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveAutomationClip( song, arguments, automationClip, address, failure ) )
	{
		return failure;
	}

	int position = 0;
	QString error;
	if( !readAutomationPosition( arguments, "pos", position, error ) )
	{
		return invalidArguments( error );
	}
	automationClip->removeNode( TimePos( position ) );
	return CommandResult::success( automationClipDetail( song, address, automationClip ) );
}




CommandResult executeAutomationRemoveNodes( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	AutomationClip *automationClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveAutomationClip( song, arguments, automationClip, address, failure ) )
	{
		return failure;
	}

	int start = 0;
	int end = 0;
	QString error;
	if( arguments.contains( "range" ) && ( arguments.contains( "start" ) || arguments.contains( "end" ) ) )
	{
		return invalidArguments( "Specify 'range' or 'start/end', not both." );
	}
	const auto range = arguments.contains( "range" ) ? arguments.value( "range" ).toObject() : arguments;
	if( !readAutomationPosition( range, "start", start, error ) ||
		!readAutomationPosition( range, "end", end, error ) )
	{
		return invalidArguments( error );
	}
	automationClip->removeNodes( start, end );
	return CommandResult::success( automationClipDetail( song, address, automationClip ) );
}




CommandResult executeAutomationSetProgression( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	AutomationClip *automationClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveAutomationClip( song, arguments, automationClip, address, failure ) )
	{
		return failure;
	}

	QString type;
	QString error;
	if( !readRequiredString( arguments, "type", type, error ) )
	{
		return invalidArguments( error );
	}
	AutomationClip::ProgressionType progressionType;
	if( !progressionTypeFromName( type, progressionType ) )
	{
		return invalidArguments( "'type' must be discrete, linear, or cubicHermite." );
	}
	automationClip->setProgressionType( progressionType );
	return CommandResult::success( automationClipDetail( song, address, automationClip ) );
}




CommandResult executeAutomationSetTension( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	AutomationClip *automationClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveAutomationClip( song, arguments, automationClip, address, failure ) )
	{
		return failure;
	}

	double value = 0.0;
	QString error;
	if( !readRequiredNumber( arguments, "value", value, error ) )
	{
		return invalidArguments( error );
	}
	if( value < 0.0 || value > 1.0 )
	{
		return invalidArguments( "'value' must be between 0 and 1." );
	}
	automationClip->setTension( QString::number( value, 'g', 15 ) );
	return CommandResult::success( automationClipDetail( song, address, automationClip ) );
}




CommandResult executeAutomationListTargets( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( !song ) { return engineUnavailable(); }
	const auto scope = arguments.value( "scope" ).toString();
	QJsonArray models;
	for( const auto &address : addressableModels( song ) )
	{
		const bool effect = address.path.contains( "/fx:" );
		const bool mixer = address.path.startsWith( "song/channel:" );
		const bool track = address.path.startsWith( "song/track:" ) || address.path.startsWith( "pattern/track:" );
		if( scope.isEmpty() || ( scope == "effect" && effect ) ||
			( scope == "mixer" && mixer && !effect ) || ( scope == "track" && track && !effect ) ||
			( scope == "song" && !track && !mixer && !effect ) )
		{
			models.append( modelDetail( address ) );
		}
	}
	return CommandResult::success( QJsonObject{ { "models", models } } );
}




QJsonObject transportPosition( Song *song, Song::PlayMode mode )
{
	const auto &position = song->getPlayPos( mode );
	return {
		{ "mode", playModeName( mode ) },
		{ "ticks", position.getTicks() },
		{ "bar", position.getBar() },
		{ "seconds", song->getTimeline( mode ).getElapsedSeconds() },
		{ "playing", song->isPlaying() },
		{ "paused", song->isPaused() }
	};
}




CommandResult executeSongGetInfo( const QJsonObject & )
{
	auto *song = Engine::getSong();
	return song == nullptr ? engineUnavailable() : CommandResult::success( songSummary( song ) );
}




CommandResult executeSongSetTempo( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	int tempo = 0;
	QString error;
	if( !readRequiredInteger( arguments, "bpm", tempo, error ) )
	{
		return invalidArguments( error );
	}
	if( tempo < MinTempo || tempo > MaxTempo )
	{
		return invalidArguments( QStringLiteral( "'bpm' must be between %1 and %2." )
			.arg( MinTempo ).arg( MaxTempo ) );
	}

	song->tempoModel().setValue( tempo );
	return CommandResult::success( QJsonObject{ { "tempo", song->getTempo() } } );
}




bool validDenominator( int denominator )
{
	return denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8 ||
		denominator == 16 || denominator == 32 || denominator == 64;
}




CommandResult executeSongSetTimeSignature( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	int numerator = 0;
	int denominator = 0;
	QString error;
	if( !readRequiredInteger( arguments, "numerator", numerator, error ) ||
		!readRequiredInteger( arguments, "denominator", denominator, error ) )
	{
		return invalidArguments( error );
	}
	if( numerator < 1 || numerator > 64 || !validDenominator( denominator ) )
	{
		return invalidArguments( "The time signature must have a numerator from 1 to 64 and a power-of-two denominator." );
	}

	auto &timeSignature = song->getTimeSigModel();
	timeSignature.setNumerator( numerator );
	timeSignature.setDenominator( denominator );
	return CommandResult::success( QJsonObject{
		{ "numerator", timeSignature.getNumerator() },
		{ "denominator", timeSignature.getDenominator() }
	} );
}




CommandResult executeSongSetMasterVolume( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	int value = 0;
	QString error;
	if( !readRequiredInteger( arguments, "value", value, error ) )
	{
		return invalidArguments( error );
	}
	if( value < MinVolume || value > MaxVolume )
	{
		return invalidArguments( QStringLiteral( "'value' must be between %1 and %2." )
			.arg( MinVolume ).arg( MaxVolume ) );
	}

	song->setMasterVolume( value );
	return CommandResult::success( QJsonObject{ { "masterVolume", song->masterVolume() } } );
}




CommandResult executeSongSetMasterPitch( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	int value = 0;
	QString error;
	if( !readRequiredInteger( arguments, "semitones", value, error ) )
	{
		return invalidArguments( error );
	}
	if( value < -12 || value > 12 )
	{
		return invalidArguments( "'semitones' must be between -12 and 12." );
	}

	song->setMasterPitch( value );
	return CommandResult::success( QJsonObject{ { "masterPitch", song->masterPitch() } } );
}




CommandResult executeSongClearProject( const QJsonObject & )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	song->clearProject();
	song->setModified();
	return CommandResult::success( songSummary( song ) );
}



CommandResult executeSongLoad( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	QString path;
	QString error;
	if( !readRequiredString( arguments, "path", path, error ) )
	{
		return invalidArguments( error );
	}
	const QFileInfo projectFile( path );
	if( !projectFile.exists() || !projectFile.isFile() )
	{
		return CommandResult::failure( "project_file_not_found", "The requested project file does not exist." );
	}

	const QString absolutePath = projectFile.absoluteFilePath();
	DataFile dataFile( absolutePath );
	const QString extension = projectFile.suffix().toLower();
	if( dataFile.head().isNull() || !dataFile.validate( extension ) )
	{
		return CommandResult::failure( "invalid_project_file", "The requested file is not a valid LMMS project." );
	}
	if( dataFile.hasLocalPlugins() )
	{
		return CommandResult::failure( "unsafe_project_file",
			"The project contains local plugin paths and cannot be loaded." );
	}
	if( arguments.value( "dryRun" ).toBool() )
	{
		return CommandResult::success( QJsonObject{ { "wouldLoad", absolutePath } } );
	}

	song->loadProject( absolutePath );
	if( song->hasErrors() )
	{
		return CommandResult::failure( "project_load_failed", song->errorSummary() );
	}
	return CommandResult::success( QJsonObject{
		{ "path", song->projectFileName() },
		{ "summary", songSummary( song ) }
	} );
}



CommandResult executeSongSave( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	bool hasPath = false;
	QString path;
	QString error;
	if( !readOptionalString( arguments, "path", hasPath, path, error ) )
	{
		return invalidArguments( error );
	}
	if( !hasPath )
	{
		path = song->projectFileName();
	}
	if( path.isEmpty() )
	{
		return invalidArguments( "Specify 'path' when the song has not been saved before." );
	}

	bool hasBundle = false;
	bool asBundle = false;
	if( !readOptionalBool( arguments, "asBundle", hasBundle, asBundle, error ) )
	{
		return invalidArguments( error );
	}

	DataFile dataFile( DataFile::Type::SongProject );
	const QString savedPath = dataFile.nameWithExtension( path );
	if( arguments.value( "dryRun" ).toBool() )
	{
		return CommandResult::success( QJsonObject{
			{ "wouldSave", savedPath }, { "asBundle", asBundle }
		} );
	}
	const bool saved = asBundle ? song->saveProjectFile( path, true ) : song->guiSaveProjectAs( path );
	if( !saved )
	{
		return CommandResult::failure( "project_save_failed", "LMMS could not save the project file." );
	}
	return CommandResult::success( QJsonObject{
		{ "path", savedPath }, { "asBundle", asBundle }
	} );
}



bool readTransportPosition( Song *song, const QJsonObject &arguments, const char *barArgument,
	int &ticks, QString &error )
{
	const bool hasTicks = arguments.contains( "ticks" );
	const bool hasBar = arguments.contains( argumentName( barArgument ) );
	if( hasTicks == hasBar )
	{
		error = QStringLiteral( "Specify exactly one of 'ticks' or '%1'." ).arg( argumentName( barArgument ) );
		return false;
	}

	if( hasTicks )
	{
		if( !readNonNegativeInteger( arguments, "ticks", ticks, error ) )
		{
			return false;
		}
	}
	else
	{
		int bars = 0;
		if( !readNonNegativeInteger( arguments, barArgument, bars, error ) )
		{
			return false;
		}
		const auto position = static_cast<long long>( bars ) * song->ticksPerBar();
		if( position > MaxSongLength )
		{
			error = "The requested position exceeds the maximum song length.";
			return false;
		}
		ticks = static_cast<int>( position );
	}

	if( ticks > MaxSongLength )
	{
		error = "The requested position exceeds the maximum song length.";
		return false;
	}
	return true;
}




bool resolvePlaybackSelection( Song *song, const QJsonObject &arguments, Song::PlayMode &mode,
	const Clip *&clip, int &pattern, CommandResult &failure )
{
	QString name = arguments.value( "mode" ).toString( playModeName( song->playMode() ) ).toLower();
	if( name == "none" && !arguments.contains( "mode" ) ) { name = "song"; }
	if( name == "song" ) { mode = Song::PlayMode::Song; }
	else if( name == "pattern" ) { mode = Song::PlayMode::Pattern; }
	else if( name == "midiclip" || name == "midi" ) { mode = Song::PlayMode::MidiClip; }
	else if( name == "automationclip" || name == "automation" ) { mode = Song::PlayMode::AutomationClip; }
	else if( name == "none" ) { mode = Song::PlayMode::None; }
	else { failure = CommandResult::failure( "unsupported_play_mode", "Use song, pattern, midiClip, automationClip or none." ); return false; }
	pattern = Engine::patternStore()->currentPattern();
	if( arguments.contains( "pattern" ) )
	{
		QString error;
		if( mode != Song::PlayMode::Pattern || !readNonNegativeInteger( arguments, "pattern", pattern, error ) ||
			pattern >= Engine::patternStore()->numOfPatterns() )
		{
			failure = invalidArguments( "'pattern' must identify an existing pattern in pattern mode." );
			return false;
		}
	}
	clip = nullptr;
	if( mode == Song::PlayMode::MidiClip || mode == Song::PlayMode::AutomationClip )
	{
		if( !arguments.contains( "track" ) && !arguments.contains( "clip" ) && mode == song->playMode() ) { clip = song->previewClip(); }
		else
		{
			ClipAddress address;
			if( !resolveClip( song, arguments, address, failure ) ) { return false; }
			clip = address.clip;
		}
		if( ( mode == Song::PlayMode::MidiClip && !dynamic_cast<const MidiClip *>( clip ) ) ||
			( mode == Song::PlayMode::AutomationClip && !dynamic_cast<const AutomationClip *>( clip ) ) )
		{
			failure = CommandResult::failure( "wrong_clip_type", "The selected clip does not match the playback mode." );
			return false;
		}
	}
	return true;
}

CommandResult executeSongSetPlayMode( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( !song ) { return engineUnavailable(); }
	Song::PlayMode mode;
	const Clip *clip = nullptr;
	int pattern = 0;
	CommandResult failure;
	if( !resolvePlaybackSelection( song, arguments, mode, clip, pattern, failure ) ) { return failure; }
	if( !arguments.value( "dryRun" ).toBool() )
	{
		if( !song->setPlayMode( mode, clip ) ) { return invalidArguments( "Could not select the requested playback mode." ); }
		if( mode == Song::PlayMode::Pattern ) { Engine::patternStore()->setCurrentPattern( pattern ); }
	}
	return CommandResult::success( QJsonObject{ { "mode", playModeName( mode ) }, { "pattern", pattern }, { "playing", false } } );
}


CommandResult executeTransportPlay( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( !song ) { return engineUnavailable(); }
	Song::PlayMode mode;
	const Clip *clip = nullptr;
	int pattern = 0;
	CommandResult failure;
	if( !resolvePlaybackSelection( song, arguments, mode, clip, pattern, failure ) ) { return failure; }
	if( mode == Song::PlayMode::None ) { return invalidArguments( "Playback requires a selected mode." ); }
	int ticks = song->getPlayPos( mode ).getTicks();
	QString error;
	if( arguments.contains( "fromBar" ) || arguments.contains( "ticks" ) )
	{
		if( !readTransportPosition( song, arguments, "fromBar", ticks, error ) )
		{
			return invalidArguments( error );
		}
	}
	if( !arguments.value( "dryRun" ).toBool() )
	{
		song->setPlayMode( mode, clip );
		if( mode == Song::PlayMode::Pattern ) { Engine::patternStore()->setCurrentPattern( pattern ); }
		song->setPlayPos( ticks, mode );
		if( mode == Song::PlayMode::Song ) { song->playSong(); }
		else if( mode == Song::PlayMode::Pattern ) { song->playPattern(); }
		else if( mode == Song::PlayMode::MidiClip ) { song->playMidiClip( static_cast<const MidiClip *>( clip ), arguments.value( "loop" ).toBool( true ) ); }
		else { song->playAutomationClip( static_cast<const AutomationClip *>( clip ), arguments.value( "loop" ).toBool( true ) ); }
	}
	auto data = transportPosition( song, mode );
	data.insert( "requestedTicks", ticks );
	data.insert( "wouldPlay", true );
	return CommandResult::success( data );
}




CommandResult executeTransportStop( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	if( !arguments.value( "dryRun" ).toBool() ) { song->stop(); }
	return CommandResult::success( transportPosition( song, song->playMode() ) );
}




CommandResult executeTransportTogglePause( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	if( !arguments.value( "dryRun" ).toBool() ) { song->togglePause(); }
	return CommandResult::success( transportPosition( song, song->playMode() ) );
}




CommandResult executeTransportSetPosition( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	int ticks = 0;
	QString error;
	if( !readTransportPosition( song, arguments, "bar", ticks, error ) )
	{
		return invalidArguments( error );
	}

	const auto mode = song->playMode() == Song::PlayMode::None ? Song::PlayMode::Song : song->playMode();
	if( !arguments.value( "dryRun" ).toBool() ) { song->setPlayPos( ticks, mode ); }
	auto data = transportPosition( song, mode );
	data.insert( "requestedTicks", ticks );
	return CommandResult::success( data );
}

CommandResult executeTransportSetLoopRange( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	QString error;
	int startBar = 0;
	int endBar = 0;
	if( !readNonNegativeInteger( arguments, "startBar", startBar, error ) ||
		!readNonNegativeInteger( arguments, "endBar", endBar, error ) )
	{
		return invalidArguments( error );
	}
	if( endBar <= startBar )
	{
		return invalidArguments( "'endBar' must be greater than 'startBar'." );
	}

	const int ticksPerBar = song->ticksPerBar();
	if( endBar > MaxSongLength / ticksPerBar )
	{
		return invalidArguments( "The loop range must fit within the maximum song length." );
	}

	auto &timeline = song->getTimeline( Song::PlayMode::Song );
	timeline.setLoopPoints( TimePos( startBar * ticksPerBar ), TimePos( endBar * ticksPerBar ) );
	timeline.setLoopEnabled( true );
	song->setModified();

	return CommandResult::success( QJsonObject{
		{ "startBar", startBar },
		{ "endBar", endBar },
		{ "start", timeline.loopBegin().getTicks() },
		{ "end", timeline.loopEnd().getTicks() },
		{ "enabled", timeline.loopEnabled() }
	} );
}




CommandResult executeTransportPreviewClip( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	MidiClip *midiClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveMidiClip( song, arguments, midiClip, address, failure ) )
	{
		return failure;
	}

	bool hasLoop = false;
	bool loop = true;
	QString error;
	if( !readOptionalBool( arguments, "loop", hasLoop, loop, error ) )
	{
		return invalidArguments( error );
	}

	if( !arguments.value( "dryRun" ).toBool() ) { song->playMidiClip( midiClip, loop ); }
	return CommandResult::success( QJsonObject{
		{ "path", clipPath( address.trackIndex, address.clipIndex, address.track ) },
		{ "loop", loop },
		{ "mode", playModeName( song->playMode() ) },
		{ "playing", song->isPlaying() }
	} );
}



CommandResult executeTransportGetPosition( const QJsonObject & )
{
	auto *song = Engine::getSong();
	return song == nullptr ? engineUnavailable() : CommandResult::success(
		transportPosition( song, song->playMode() ) );
}




bool trackTypeFromName( const QString &name, Track::Type &type )
{
	if( name.compare( "instrument", Qt::CaseInsensitive ) == 0 )
	{
		type = Track::Type::Instrument;
		return true;
	}
	if( name.compare( "sample", Qt::CaseInsensitive ) == 0 )
	{
		type = Track::Type::Sample;
		return true;
	}
	if( name.compare( "automation", Qt::CaseInsensitive ) == 0 )
	{
		type = Track::Type::Automation;
		return true;
	}
	return false;
}




CommandResult executeTrackCreate( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	Track::Type type = Track::Type::Instrument;
	QString error;
	if( arguments.contains( "type" ) )
	{
		QString typeName;
		if( !readRequiredString( arguments, "type", typeName, error ) )
		{
			return invalidArguments( error );
		}
		if( !trackTypeFromName( typeName, type ) )
		{
			return CommandResult::failure( "unsupported_track_type",
				"A1 supports instrument, sample, and automation tracks." );
		}
	}

	bool hasName = false;
	QString name;
	if( !readOptionalString( arguments, "name", hasName, name, error ) )
	{
		return invalidArguments( error );
	}

	CommandResult failure;
	auto *container = resolveParent( song, arguments, failure );
	if( !container ) { return failure; }
	int index = static_cast<int>( container->tracks().size() );
	if( arguments.contains( "index" ) && !readNonNegativeInteger( arguments, "index", index, error ) )
	{
		return invalidArguments( error );
	}
	if( index > static_cast<int>( container->tracks().size() ) )
	{
		return invalidArguments( "'index' must be an insertion position in the selected container." );
	}
	auto *track = Track::create( type, container );
	if( track == nullptr )
	{
		return CommandResult::failure( "track_creation_failed", "LMMS could not create the requested track." );
	}
	if( hasName )
	{
		track->setName( name );
	}
	container->moveTrack( track, index );
	song->setModified();

	const int trackIndex = indexOfTrack( song, track );
	return CommandResult::success( trackDetail( song, track, trackIndex ) );
}




CommandResult executeTrackRemove( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	Track *track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if( !resolveTrack( song, arguments, track, trackIndex, failure ) )
	{
		return failure;
	}

	const QString path = trackPath( trackIndex, track );
	delete track;
	return CommandResult::success( QJsonObject{ { "removed", path } } );
}

CommandResult executeTrackClone( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( !song ) { return engineUnavailable(); }
	Track *track = nullptr;
	int index = -1;
	CommandResult failure;
	if( !resolveTrack( song, arguments, track, index, failure ) ) { return failure; }
	auto guard = Engine::audioEngine()->requestChangesGuard();
	auto *clone = track->clone();
	if( !clone ) { return CommandResult::failure( "track_creation_failed", "Could not clone the track." ); }
	if( arguments.contains( "name" ) ) { clone->setName( arguments.value( "name" ).toString() ); }
	song->setModified();
	return CommandResult::success( trackDetail( song, clone, indexOfTrack( song, clone ) ) );
}

CommandResult executeTrackMove( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( !song ) { return engineUnavailable(); }
	Track *track = nullptr;
	int index = -1;
	CommandResult failure;
	if( !resolveTrack( song, arguments, track, index, failure ) ) { return failure; }
	const int destination = arguments.value( "newIndex" ).toInt( -1 );
	if( destination < 0 || destination >= static_cast<int>( track->trackContainer()->tracks().size() ) )
	{
		return invalidArguments( "'newIndex' must identify a position in the selected track container." );
	}
	auto guard = Engine::audioEngine()->requestChangesGuard();
	track->trackContainer()->moveTrack( track, destination );
	song->setModified();
	return CommandResult::success( trackDetail( song, track, destination ) );
}

bool readColor( const QJsonObject &arguments, std::optional<QColor> &color )
{
	const auto value = arguments.value( "value" );
	if( value.isNull() || ( value.isString() && value.toString().isEmpty() ) ) { return true; }
	if( !value.isString() ) { return false; }
	const QColor parsed( value.toString() );
	if( !parsed.isValid() ) { return false; }
	color = parsed;
	return true;
}

CommandResult executeTrackProperty( const QJsonObject &arguments, const QString &property )
{
	auto *song = Engine::getSong();
	if( !song ) { return engineUnavailable(); }
	Track *track = nullptr;
	int index = -1;
	CommandResult failure;
	if( !resolveTrack( song, arguments, track, index, failure ) ) { return failure; }
	if( property == "height" )
	{
		const int height = arguments.value( "value" ).toInt();
		if( height < MINIMAL_TRACK_HEIGHT || height > 4096 )
		{
			return invalidArguments( "Track height must be between the minimum track height and 4096 pixels." );
		}
		track->setHeight( height );
		emit track->dataChanged();
	}
	else if( property == "color" )
	{
		std::optional<QColor> color;
		if( !readColor( arguments, color ) ) { return invalidArguments( "'value' must be a valid color, empty string or null." ); }
		track->setColor( color );
	}
	else
	{
		const int channel = arguments.value( "channel" ).toInt( -1 );
		if( !Engine::mixer() || channel < 0 || channel >= Engine::mixer()->numChannels() )
		{
			return invalidArguments( "'channel' must identify an existing mixer channel." );
		}
		IntModel *model = nullptr;
		if( auto *instrument = dynamic_cast<InstrumentTrack *>( track ) ) { model = instrument->mixerChannelModel(); }
		if( auto *sample = dynamic_cast<SampleTrack *>( track ) ) { model = sample->mixerChannelModel(); }
		if( !model ) { return CommandResult::failure( "wrong_track_type", "Only instrument and sample tracks have mixer channels." ); }
		model->setRange( 0, Engine::mixer()->numChannels() - 1, 1 );
		model->setValue( channel );
	}
	song->setModified();
	return CommandResult::success( trackDetail( song, track, index ) );
}

CommandResult executeClipDuplicate( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( !song ) { return engineUnavailable(); }
	ClipAddress address;
	CommandResult failure;
	if( !resolveClip( song, arguments, address, failure ) ) { return failure; }
	int position = address.clip->endPosition().getTicks();
	QString error;
	if( arguments.contains( "position" ) && !readNonNegativeInteger( arguments, "position", position, error ) )
	{
		return invalidArguments( error );
	}
	if( position > MaxSongLength - address.clip->length().getTicks() ) { return invalidArguments( "The duplicate exceeds the maximum song length." ); }
	auto guard = Engine::audioEngine()->requestChangesGuard();
	auto *clone = address.clip->clone();
	if( !clone ) { return CommandResult::failure( "clip_creation_failed", "Could not duplicate the clip." ); }
	clone->movePosition( TimePos( position ) );
	address.clip = clone;
	address.clipIndex = indexOfClip( address.track, clone );
	song->setModified();
	return CommandResult::success( clipDetail( address ) );
}

CommandResult executeClipProperty( const QJsonObject &arguments, const QString &property )
{
	auto *song = Engine::getSong();
	if( !song ) { return engineUnavailable(); }
	ClipAddress address;
	CommandResult failure;
	if( !resolveClip( song, arguments, address, failure ) ) { return failure; }
	if( property == "color" )
	{
		std::optional<QColor> color;
		if( !readColor( arguments, color ) ) { return invalidArguments( "'value' must be a valid color, empty string or null." ); }
		address.clip->setColor( color );
	}
	else if( property == "autoResize" )
	{
		address.clip->setAutoResize( arguments.value( "value" ).toBool() );
		emit address.clip->dataChanged();
	}
	else
	{
		const int offset = arguments.value( "value" ).toInt();
		if( offset < -MaxSongLength || offset > MaxSongLength ) { return invalidArguments( "The clip offset is out of range." ); }
		address.clip->setStartTimeOffset( TimePos( offset ) );
	}
	song->setModified();
	return CommandResult::success( clipDetail( address ) );
}




CommandResult executeTrackSetName( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	Track *track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if( !resolveTrack( song, arguments, track, trackIndex, failure ) )
	{
		return failure;
	}

	QString name;
	QString error;
	if( !readRequiredString( arguments, "name", name, error, true ) )
	{
		return invalidArguments( error );
	}

	track->setName( name );
	return CommandResult::success( trackDetail( song, track, trackIndex ) );
}




CommandResult executeTrackSetMuted( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	Track *track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if( !resolveTrack( song, arguments, track, trackIndex, failure ) )
	{
		return failure;
	}

	bool muted = false;
	QString error;
	if( !readRequiredBool( arguments, "value", muted, error ) )
	{
		return invalidArguments( error );
	}

	track->setMuted( muted );
	return CommandResult::success( trackDetail( song, track, trackIndex ) );
}




CommandResult executeTrackSetSolo( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	Track *track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if( !resolveTrack( song, arguments, track, trackIndex, failure ) )
	{
		return failure;
	}

	bool solo = false;
	QString error;
	if( !readRequiredBool( arguments, "value", solo, error ) )
	{
		return invalidArguments( error );
	}

	if( track->isSolo() != solo )
	{
		track->toggleSolo();
	}
	return CommandResult::success( trackDetail( song, track, trackIndex ) );
}




bool readSubPluginKey( const Plugin::Descriptor *descriptor, const QJsonObject &arguments,
	std::optional<Plugin::Descriptor::SubPluginFeatures::Key> &key, CommandResult &failure )
{
	if( !arguments.contains( "subKey" ) )
	{
		if( descriptor->subPluginFeatures )
		{
			failure = invalidArguments( "This plugin requires subKey; select one from effect.listAvailable." );
			return false;
		}
		return true;
	}
	if( !descriptor->subPluginFeatures )
	{
		failure = invalidArguments( "This plugin does not support subKey." );
		return false;
	}
	const auto requested = arguments.value( "subKey" ).toObject();
	Plugin::Descriptor::SubPluginFeatures::Key::AttributeMap attributes;
	const auto values = requested.value( "attributes" ).toObject();
	for( auto it = values.begin(); it != values.end(); ++it )
	{
		if( !it.value().isString() ) { failure = invalidArguments( "subKey attributes must be strings." ); return false; }
		attributes.insert( it.key(), it.value().toString() );
	}
	Plugin::Descriptor::SubPluginFeatures::KeyList available;
	descriptor->subPluginFeatures->listSubPluginKeys( descriptor, available );
	for( const auto &candidate : available )
	{
		if( candidate.attributes == attributes &&
			( !requested.contains( "name" ) || candidate.name == requested.value( "name" ).toString() ) )
		{
			key = candidate;
			return true;
		}
	}
	failure = invalidArguments( "subKey must identify a key returned by effect.listAvailable." );
	return false;
}

CommandResult executeInstrumentLoad( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	InstrumentTrack *track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if( !resolveInstrumentTrack( song, arguments, track, trackIndex, failure ) )
	{
		return failure;
	}

	QString plugin;
	QString error;
	if( !readRequiredString( arguments, "plugin", plugin, error ) )
	{
		return invalidArguments( error );
	}

	auto *pluginFactory = PluginFactory::instance();
	if( pluginFactory == nullptr )
	{
		return CommandResult::failure( "plugin_factory_unavailable", "The plugin factory is unavailable." );
	}

	const QByteArray pluginName = plugin.toUtf8();
	const auto pluginInfo = pluginFactory->pluginInfo( pluginName.constData() );
	if( pluginInfo.isNull() || pluginInfo.descriptor == nullptr ||
		pluginInfo.descriptor->type != Plugin::Type::Instrument )
	{
		return CommandResult::failure( "instrument_not_found",
			QStringLiteral( "'%1' is not an available instrument plugin." ).arg( plugin ) );
	}

	std::optional<Plugin::Descriptor::SubPluginFeatures::Key> subKey;
	if( !readSubPluginKey( pluginInfo.descriptor, arguments, subKey, failure ) ) { return failure; }
	QString path;
	if( arguments.contains( "path" ) )
	{
		const QFileInfo file( arguments.value( "path" ).toString() );
		const auto suffix = file.suffix().toLower();
		const bool supported = pluginInfo.descriptor->supportsFileType( suffix ) ||
			( subKey && subKey->additionalFileExtensions().split( ',' ).contains( suffix ) );
		if( !file.isFile() || !file.isReadable() || !supported )
		{
			return invalidArguments( "'path' must be a readable local file supported by the instrument." );
		}
		path = file.absoluteFilePath();
	}
	auto *instrument = track->loadInstrument( plugin, subKey ? &*subKey : nullptr );
	if( instrument == nullptr || instrument->descriptor() != pluginInfo.descriptor )
	{
		return CommandResult::failure( "instrument_load_failed",
			QStringLiteral( "LMMS could not load instrument '%1'." ).arg( plugin ) );
	}

	if( !path.isEmpty() ) { instrument->loadFile( path ); }
	return CommandResult::success( QJsonObject{
		{ "path", trackPath( trackIndex, track ) },
		{ "plugin", plugin },
		{ "parameters", instrumentParameters( track ) }
	} );
}




bool validateInstrumentParameters( InstrumentTrack *track, bool hasVolume, double volume, bool hasPanning, double panning,
	bool hasPitch, double pitch, bool hasPitchRange, int pitchRange, bool hasBaseNote, int baseNote,
	QString &error )
{
	if( hasVolume && ( volume < MinVolume || volume > MaxVolume ) )
	{
		error = QStringLiteral( "'volume' must be between %1 and %2." ).arg( MinVolume ).arg( MaxVolume );
		return false;
	}
	if( hasPanning && ( panning < PanningLeft || panning > PanningRight ) )
	{
		error = QStringLiteral( "'panning' must be between %1 and %2." )
			.arg( PanningLeft ).arg( PanningRight );
		return false;
	}
	if( hasPitchRange && ( pitchRange < 1 || pitchRange > 60 ) )
	{
		error = "'pitchRange' must be between 1 and 60.";
		return false;
	}
	const int range = hasPitchRange ? pitchRange : track->pitchRangeModel()->value();
	if( hasPitch && ( pitch < MinPitchDefault * range || pitch > MaxPitchDefault * range ) )
	{
		error = QStringLiteral( "'pitch' is measured in cents and must be between %1 and %2 for this pitch range." )
			.arg( MinPitchDefault * range ).arg( MaxPitchDefault * range );
		return false;
	}
	if( hasBaseNote && ( baseNote < 0 || baseNote >= NumKeys ) )
	{
		error = QStringLiteral( "'baseNote' must be between 0 and %1." ).arg( NumKeys - 1 );
		return false;
	}
	return true;
}




CommandResult executeInstrumentSetParameters( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	InstrumentTrack *track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if( !resolveInstrumentTrack( song, arguments, track, trackIndex, failure ) )
	{
		return failure;
	}

	bool hasVolume = false;
	bool hasPanning = false;
	bool hasPitch = false;
	bool hasPitchRange = false;
	bool hasBaseNote = false;
	double volume = 0.0;
	double panning = 0.0;
	double pitch = 0.0;
	int pitchRange = 0;
	int baseNote = 0;
	QString error;
	if( !readOptionalNumber( arguments, "volume", hasVolume, volume, error ) ||
		!readOptionalNumber( arguments, "panning", hasPanning, panning, error ) ||
		!readOptionalNumber( arguments, "pitch", hasPitch, pitch, error ) ||
		!readOptionalInteger( arguments, "pitchRange", hasPitchRange, pitchRange, error ) ||
		!readOptionalInteger( arguments, "baseNote", hasBaseNote, baseNote, error ) ||
		!validateInstrumentParameters( track, hasVolume, volume, hasPanning, panning, hasPitch, pitch,
			hasPitchRange, pitchRange, hasBaseNote, baseNote, error ) )
	{
		return invalidArguments( error );
	}
	if( !hasVolume && !hasPanning && !hasPitch && !hasPitchRange && !hasBaseNote )
	{
		return invalidArguments( "Specify at least one instrument parameter." );
	}

	if( hasVolume )
	{
		track->volumeModel()->setValue( static_cast<float>( volume ) );
	}
	if( hasPanning )
	{
		track->panningModel()->setValue( static_cast<float>( panning ) );
	}
	if( hasPitchRange )
	{
		track->pitchRangeModel()->setValue( pitchRange );
	}
	if( hasPitch )
	{
		track->pitchModel()->setValue( static_cast<float>( pitch ) );
	}
	if( hasBaseNote )
	{
		track->baseNoteModel()->setValue( baseNote );
	}

	return CommandResult::success( QJsonObject{
		{ "path", trackPath( trackIndex, track ) },
		{ "parameters", instrumentParameters( track ) }
	} );
}




CommandResult executeInstrumentSetSingleParameter( const QJsonObject &arguments, const char *parameter )
{
	QJsonObject combined = arguments;
	combined.insert( argumentName( parameter ), combined.take( "value" ) );
	return executeInstrumentSetParameters( combined );
}




CommandResult executeInstrumentSetVolume( const QJsonObject &arguments )
{
	return executeInstrumentSetSingleParameter( arguments, "volume" );
}




CommandResult executeInstrumentSetPanning( const QJsonObject &arguments )
{
	return executeInstrumentSetSingleParameter( arguments, "panning" );
}




CommandResult executeInstrumentSetPitch( const QJsonObject &arguments )
{
	return executeInstrumentSetSingleParameter( arguments, "pitch" );
}




CommandResult executeInstrumentSetPitchRange( const QJsonObject &arguments )
{
	return executeInstrumentSetSingleParameter( arguments, "pitchRange" );
}




CommandResult executeInstrumentSetBaseNote( const QJsonObject &arguments )
{
	return executeInstrumentSetSingleParameter( arguments, "baseNote" );
}




CommandResult executeClipCreate( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	Track *track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if( !resolveTrack( song, arguments, track, trackIndex, failure ) )
	{
		return failure;
	}

	QString error;
	QString requestedType;
	bool hasRequestedType = false;
	if( arguments.contains( "type" ) )
	{
		if( !readRequiredString( arguments, "type", requestedType, error ) )
		{
			return invalidArguments( error );
		}
		hasRequestedType = true;
	}

	const bool hasStart = arguments.contains( "start" );
	const bool hasPosition = arguments.contains( "position" );
	if( hasStart && hasPosition )
	{
		return invalidArguments( "Specify only one of 'start' or 'position'." );
	}

	int start = 0;
	if( hasStart && !readNonNegativeInteger( arguments, "start", start, error ) )
	{
		return invalidArguments( error );
	}
	if( hasPosition && !readNonNegativeInteger( arguments, "position", start, error ) )
	{
		return invalidArguments( error );
	}

	int length = song->ticksPerBar();
	if( arguments.contains( "length" ) && !readRequiredInteger( arguments, "length", length, error ) )
	{
		return invalidArguments( error );
	}
	if( length <= 0 || start > MaxSongLength || length > MaxSongLength - start )
	{
		return invalidArguments( "The clip position and length must fit within the maximum song length." );
	}

	bool hasName = false;
	QString name;
	if( !readOptionalString( arguments, "name", hasName, name, error ) )
	{
		return invalidArguments( error );
	}

	Clip *clip = nullptr;
	if( track->type() == Track::Type::Instrument )
	{
		if( hasRequestedType && requestedType.compare( "midi", Qt::CaseInsensitive ) != 0 )
		{
			return CommandResult::failure( "unsupported_clip_type",
				"Instrument tracks support MIDI clips." );
		}
		clip = new MidiClip( static_cast<InstrumentTrack *>( track ) );
	}
	else if( track->type() == Track::Type::Sample )
	{
		if( hasRequestedType && requestedType.compare( "sample", Qt::CaseInsensitive ) != 0 )
		{
			return CommandResult::failure( "unsupported_clip_type",
				"Sample tracks support sample clips." );
		}
		clip = track->createClip( TimePos( start ) );
	}
	else if( track->type() == Track::Type::Automation )
	{
		if( hasRequestedType && requestedType.compare( "automation", Qt::CaseInsensitive ) != 0 )
		{
			return CommandResult::failure( "unsupported_clip_type", "Automation tracks support automation clips." );
		}
		clip = track->createClip( TimePos( start ) );
	}
	else
	{
		return CommandResult::failure( "unsupported_clip_type",
			"The selected track does not support agent clip creation." );
	}

	if( clip == nullptr )
	{
		return CommandResult::failure( "clip_creation_failed", "LMMS could not create the requested clip." );
	}
	clip->movePosition( TimePos( start ) );
	clip->changeLength( TimePos( length ) );
	if( hasName )
	{
		clip->setName( name );
	}

	ClipAddress address;
	address.track = track;
	address.clip = clip;
	address.trackIndex = trackIndex;
	address.clipIndex = indexOfClip( track, clip );
	return CommandResult::success( clipDetail( address ) );
}




CommandResult executeClipRemove( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	ClipAddress address;
	CommandResult failure;
	if( !resolveClip( song, arguments, address, failure ) )
	{
		return failure;
	}

	const QString path = clipPath( address.trackIndex, address.clipIndex, address.track );
	delete address.clip;
	return CommandResult::success( QJsonObject{ { "removed", path } } );
}




CommandResult executeClipSetPosition( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	ClipAddress address;
	CommandResult failure;
	if( !resolveClip( song, arguments, address, failure ) )
	{
		return failure;
	}

	int position = 0;
	QString error;
	if( arguments.contains( "bar" ) && arguments.contains( "position" ) )
	{
		return invalidArguments( "Specify only one of 'bar' or 'position'." );
	}
	const bool validPosition = arguments.contains( "bar" )
		? readTransportPosition( song, QJsonObject{ { "bar", arguments.value( "bar" ) } }, "bar", position, error )
		: readNonNegativeInteger( arguments, "position", position, error );
	if( !validPosition )
	{
		return invalidArguments( error );
	}
	if( position > MaxSongLength - address.clip->length().getTicks() )
	{
		return invalidArguments( "The clip position exceeds the maximum song length." );
	}

	address.clip->movePosition( TimePos( position ) );
	return CommandResult::success( clipDetail( address ) );
}




CommandResult executeClipSetLength( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	ClipAddress address;
	CommandResult failure;
	if( !resolveClip( song, arguments, address, failure ) )
	{
		return failure;
	}

	int length = 0;
	QString error;
	if( !readRequiredInteger( arguments, "length", length, error ) )
	{
		return invalidArguments( error );
	}
	if( length <= 0 || length > MaxSongLength - address.clip->startPosition().getTicks() )
	{
		return invalidArguments( "The clip length must be positive and fit within the maximum song length." );
	}

	address.clip->changeLength( TimePos( length ) );
	return CommandResult::success( clipDetail( address ) );
}




CommandResult executeClipSetMute( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	ClipAddress address;
	CommandResult failure;
	if( !resolveClip( song, arguments, address, failure ) )
	{
		return failure;
	}

	bool muted = false;
	QString error;
	if( !readRequiredBool( arguments, "value", muted, error ) )
	{
		return invalidArguments( error );
	}

	address.clip->setMuted( muted );
	return CommandResult::success( clipDetail( address ) );
}




struct PendingNote
{
	int position = 0;
	int length = 0;
	int key = DefaultKey;
	int volume = DefaultVolume;
	int panning = DefaultPanning;
};




bool parseNotes( const QJsonObject &arguments, std::vector<PendingNote> &notes, QString &error )
{
	const auto value = arguments.value( "notes" );
	if( !value.isArray() )
	{
		error = "'notes' must be an array.";
		return false;
	}

	const auto inputNotes = value.toArray();
	if( inputNotes.isEmpty() )
	{
		error = "'notes' must not be empty.";
		return false;
	}

	notes.reserve( inputNotes.size() );
	for( int index = 0; index < inputNotes.size(); ++index )
	{
		if( !inputNotes.at( index ).isObject() )
		{
			error = QStringLiteral( "notes[%1] must be an object." ).arg( index );
			return false;
		}

		const auto noteObject = inputNotes.at( index ).toObject();
		PendingNote note;
		QString noteError;
		bool hasVolume = false;
		bool hasPanning = false;
		if( !readRequiredInteger( noteObject, "position", note.position, noteError ) ||
			!readRequiredInteger( noteObject, "length", note.length, noteError ) ||
			!readRequiredInteger( noteObject, "key", note.key, noteError ) ||
			!readOptionalInteger( noteObject, "volume", hasVolume, note.volume, noteError ) ||
			!readOptionalInteger( noteObject, "panning", hasPanning, note.panning, noteError ) )
		{
			error = QStringLiteral( "notes[%1]: %2" ).arg( index ).arg( noteError );
			return false;
		}
		if( note.position < 0 || note.length <= 0 || note.position > MaxSongLength - note.length ||
			note.key < 0 || note.key >= NumKeys ||
			note.volume < MinVolume || note.volume > MaxVolume ||
			note.panning < PanningLeft || note.panning > PanningRight )
		{
			error = QStringLiteral( "notes[%1] contains an out-of-range position, length, key, volume, or panning value." )
				.arg( index );
			return false;
		}
		notes.push_back( note );
	}
	return true;
}




struct NoteFilter
{
	int start = 0;
	int end = MaxSongLength;
	QJsonArray keys;
	bool matches( const Note *note ) const
	{
		return note->pos().getTicks() >= start && note->pos().getTicks() < end &&
			( keys.isEmpty() || keys.contains( note->key() ) );
	}
};

bool readNoteFilter( const QJsonObject &arguments, NoteFilter &filter, QString &error )
{
	if( arguments.contains( "range" ) )
	{
		const auto range = arguments.value( "range" ).toObject();
		if( !readNonNegativeInteger( range, "start", filter.start, error ) ||
			!readNonNegativeInteger( range, "end", filter.end, error ) ) { return false; }
		if( filter.end < filter.start || filter.end > MaxSongLength )
		{
			error = "The note range must have 0 <= start <= end <= maximum song length.";
			return false;
		}
	}
	filter.keys = arguments.value( "keys" ).toArray();
	for( const auto &key : filter.keys )
	{
		if( !key.isDouble() || std::floor( key.toDouble() ) != key.toDouble() || key.toDouble() < 0 || key.toDouble() >= NumKeys )
		{
			error = "Every selected key must be an integer in the LMMS key range.";
			return false;
		}
	}
	return true;
}

QJsonObject noteDetail( const Note *note, int index )
{
	return {
		{ "index", index }, { "position", note->pos().getTicks() }, { "length", note->length().getTicks() },
		{ "key", note->key() }, { "volume", note->getVolume() }, { "panning", note->getPanning() }
	};
}

CommandResult executeMidiUpdateNote( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( !song ) { return engineUnavailable(); }
	MidiClip *clip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveMidiClip( song, arguments, clip, address, failure ) ) { return failure; }
	const int index = arguments.value( "note" ).toInt( -1 );
	if( index < 0 || index >= static_cast<int>( clip->notes().size() ) )
	{
		return CommandResult::failure( "note_not_found", "The note index does not exist in this clip." );
	}
	auto *note = clip->notes()[index];
	const auto fields = arguments.value( "fields" ).toObject();
	if( fields.isEmpty() ) { return invalidArguments( "'fields' must contain at least one note property." ); }
	auto candidate = noteDetail( note, index );
	candidate.remove( "index" );
	// Step notes retain their native length unless the caller explicitly changes it.
	candidate.insert( "length", std::max( 1, note->length().getTicks() ) );
	for( auto it = fields.begin(); it != fields.end(); ++it ) { candidate.insert( it.key(), it.value() ); }
	std::vector<PendingNote> parsed;
	QString error;
	if( !parseNotes( QJsonObject{ { "notes", QJsonArray{ candidate } } }, parsed, error ) ) { return invalidArguments( error ); }
	const auto &value = parsed.front();
	auto guard = Engine::audioEngine()->requestChangesGuard();
	note->setPos( TimePos( value.position ) );
	if( fields.contains( "length" ) ) { note->setLength( TimePos( value.length ) ); }
	note->setKey( value.key );
	note->setVolume( static_cast<volume_t>( value.volume ) );
	note->setPanning( static_cast<panning_t>( value.panning ) );
	clip->updateNotes();
	song->setModified();
	const auto updatedIndex = static_cast<int>( std::distance( clip->notes().begin(), std::find( clip->notes().begin(), clip->notes().end(), note ) ) );
	return CommandResult::success( noteDetail( note, updatedIndex ) );
}

CommandResult executeMidiEditNotes( const QJsonObject &arguments, const QString &operation )
{
	auto *song = Engine::getSong();
	if( !song ) { return engineUnavailable(); }
	MidiClip *clip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveMidiClip( song, arguments, clip, address, failure ) ) { return failure; }
	NoteFilter filter;
	QString error;
	if( !readNoteFilter( arguments, filter, error ) ) { return invalidArguments( error ); }
	std::vector<Note *> selected;
	for( auto *note : clip->notes() ) { if( filter.matches( note ) ) { selected.push_back( note ); } }
	int semitones = 0;
	int step = 0;
	double strength = 1.0;
	if( operation == "transpose" )
	{
		if( !readRequiredInteger( arguments, "semitones", semitones, error ) ) { return invalidArguments( error ); }
		for( const auto *note : selected )
		{
			const auto key = static_cast<long long>( note->key() ) + semitones;
			if( key < 0 || key >= NumKeys ) { return invalidArguments( "Transposition would move a note outside the LMMS key range." ); }
		}
	}
	else if( operation == "quantize" )
	{
		int grid = 0;
		if( !readRequiredInteger( arguments, "grid", grid, error ) ) { return invalidArguments( error ); }
		if( std::find( std::begin( gui::Quantizations ), std::end( gui::Quantizations ), grid ) == std::end( gui::Quantizations ) )
		{
			return invalidArguments( "'grid' must be a supported piano-roll note division, such as 16 or 24." );
		}
		step = DefaultTicksPerBar / grid;
		if( arguments.contains( "strength" ) && !readNumber( arguments, "strength", strength, error ) ) { return invalidArguments( error ); }
		if( strength < 0 || strength > 1 ) { return invalidArguments( "'strength' must be between 0 and 1." ); }
		for( const auto *note : selected )
		{
			const double target = std::round( static_cast<double>( note->pos().getTicks() ) / step ) * step;
			const double position = std::round( note->pos().getTicks() + ( target - note->pos().getTicks() ) * strength );
			if( position > MaxSongLength - std::max( 0, note->length().getTicks() ) ) { return invalidArguments( "Quantization would exceed the maximum song length." ); }
		}
	}
	auto guard = Engine::audioEngine()->requestChangesGuard();
	for( auto *note : selected )
	{
		if( operation == "remove" ) { clip->removeNote( note ); }
		else if( operation == "transpose" ) { note->setKey( note->key() + semitones ); }
		else
		{
			const double target = std::round( static_cast<double>( note->pos().getTicks() ) / step ) * step;
			note->setPos( TimePos( static_cast<int>( std::round( note->pos().getTicks() + ( target - note->pos().getTicks() ) * strength ) ) ) );
		}
	}
	clip->updateNotes();
	song->setModified();
	return CommandResult::success( QJsonObject{
		{ "path", clipPath( address.trackIndex, address.clipIndex, address.track ) },
		{ operation == "remove" ? "removed" : "changed", static_cast<int>( selected.size() ) },
		{ "noteCount", static_cast<int>( clip->notes().size() ) }
	} );
}

CommandResult executeMidiAddNotes( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	MidiClip *midiClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveMidiClip( song, arguments, midiClip, address, failure ) )
	{
		return failure;
	}

	std::vector<PendingNote> notes;
	QString error;
	if( !parseNotes( arguments, notes, error ) )
	{
		return invalidArguments( error );
	}

	std::vector<Note *> addedNotes;
	for( const auto &note : notes )
	{
		addedNotes.push_back( midiClip->addNote( Note( TimePos( note.length ), TimePos( note.position ), note.key,
			static_cast<volume_t>( note.volume ), static_cast<panning_t>( note.panning ) ), false ) );
	}
	QJsonArray indices;
	for( const auto *note : addedNotes )
	{
		indices.append( static_cast<int>( std::distance( midiClip->notes().begin(), std::find( midiClip->notes().begin(), midiClip->notes().end(), note ) ) ) );
	}

	return CommandResult::success( QJsonObject{
		{ "path", clipPath( address.trackIndex, address.clipIndex, address.track ) },
		{ "added", static_cast<int>( notes.size() ) },
		{ "indices", indices },
		{ "noteCount", static_cast<int>( midiClip->notes().size() ) }
	} );
}




CommandResult executeMidiClearNotes( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	MidiClip *midiClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveMidiClip( song, arguments, midiClip, address, failure ) )
	{
		return failure;
	}

	const auto removed = static_cast<int>( midiClip->notes().size() );
	midiClip->clearNotes();
	return CommandResult::success( QJsonObject{
		{ "path", clipPath( address.trackIndex, address.clipIndex, address.track ) },
		{ "removed", removed },
		{ "noteCount", 0 }
	} );
}




CommandResult executeMidiSetSteps( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	MidiClip *midiClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveMidiClip( song, arguments, midiClip, address, failure ) )
	{
		return failure;
	}

	int steps = 0;
	QString error;
	if( !readRequiredInteger( arguments, "steps", steps, error ) )
	{
		return invalidArguments( error );
	}
	const int maxSteps = MaxSongLength * TimePos::stepsPerBar() / song->ticksPerBar();
	if( steps < 1 || steps > maxSteps )
	{
		return invalidArguments( QStringLiteral( "'steps' must be between 1 and %1." ).arg( maxSteps ) );
	}

	midiClip->setSteps( steps );
	song->setModified();
	return CommandResult::success( QJsonObject{
		{ "path", clipPath( address.trackIndex, address.clipIndex, address.track ) },
		{ "steps", midiClip->steps() },
		{ "type", midiClipTypeName( midiClip->type() ) }
	} );
}



CommandResult executeMidiSetClipType( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	MidiClip *midiClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveMidiClip( song, arguments, midiClip, address, failure ) )
	{
		return failure;
	}

	QString type;
	QString error;
	if( !readRequiredString( arguments, "type", type, error ) )
	{
		return invalidArguments( error );
	}

	MidiClip::Type clipType;
	if( type.compare( "beat", Qt::CaseInsensitive ) == 0 )
	{
		clipType = MidiClip::Type::BeatClip;
	}
	else if( type.compare( "melody", Qt::CaseInsensitive ) == 0 )
	{
		clipType = MidiClip::Type::MelodyClip;
	}
	else
	{
		return invalidArguments( "'type' must be 'beat' or 'melody'." );
	}

	midiClip->setClipType( clipType );
	song->setModified();
	return CommandResult::success( QJsonObject{
		{ "path", clipPath( address.trackIndex, address.clipIndex, address.track ) },
		{ "steps", midiClip->steps() },
		{ "type", midiClipTypeName( midiClip->type() ) }
	} );
}



CommandResult executeMidiHumanize( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	MidiClip *midiClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveMidiClip( song, arguments, midiClip, address, failure ) )
	{
		return failure;
	}

	bool hasTiming = false;
	bool hasVelocity = false;
	bool hasDetune = false;
	int timing = 0;
	int velocity = 0;
	double detune = 0.0;
	QString error;
	if( !readOptionalInteger( arguments, "timing", hasTiming, timing, error ) ||
		!readOptionalInteger( arguments, "velocity", hasVelocity, velocity, error ) ||
		!readOptionalNumber( arguments, "detune", hasDetune, detune, error ) )
	{
		return invalidArguments( error );
	}
	if( !hasTiming && !hasVelocity && !hasDetune )
	{
		return invalidArguments( "Specify at least one of 'timing', 'velocity', or 'detune'." );
	}
	if( timing < 0 || timing > song->ticksPerBar() || velocity < 0 || velocity > MaxVolume ||
		detune < 0.0 || detune > MaxDetuning )
	{
		return invalidArguments( "'timing' must fit within one bar, 'velocity' within note volume bounds, and 'detune' within note detuning bounds." );
	}

	int seed = 0;
	if( arguments.contains( "seed" ) && !readRequiredInteger( arguments, "seed", seed, error ) )
	{
		return invalidArguments( error );
	}

	std::uint32_t state = static_cast<std::uint32_t>( seed );
	const auto nextRandom = [&state]()
	{
		state = state * 1664525u + 1013904223u;
		return state;
	};
	const auto randomInteger = [&nextRandom]( int amount )
	{
		return amount == 0 ? 0 : static_cast<int>( nextRandom() % ( 2 * amount + 1 ) ) - amount;
	};
	const auto randomUnit = [&nextRandom]()
	{
		return static_cast<double>( nextRandom() ) / static_cast<double>( std::numeric_limits<std::uint32_t>::max() );
	};

	for( auto *note : midiClip->notes() )
	{
		if( hasTiming )
		{
			const int maxPosition = MaxSongLength - note->length().getTicks();
			const int position = std::clamp( note->pos().getTicks() + randomInteger( timing ), 0, maxPosition );
			note->setPos( TimePos( position ) );
		}
		if( hasVelocity )
		{
			const int volume = std::clamp( static_cast<int>( note->getVolume() ) + randomInteger( velocity ),
				static_cast<int>( MinVolume ), static_cast<int>( MaxVolume ) );
			note->setVolume( static_cast<volume_t>( volume ) );
		}
		if( hasDetune )
		{
			note->createDetuning();
			auto *detuning = note->detuning().get();
			const auto value = static_cast<float>( ( 2.0 * randomUnit() - 1.0 ) * detune );
			detuning->automationClip()->putValue( TimePos( 0 ), detuning->inverseScaledValue( value ), false, true );
		}
	}

	midiClip->updateNotes();
	song->setModified();
	return CommandResult::success( QJsonObject{
		{ "path", clipPath( address.trackIndex, address.clipIndex, address.track ) },
		{ "notes", static_cast<int>( midiClip->notes().size() ) },
		{ "seed", seed }
	} );
}



CommandResult executeSampleSetFile( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	SampleClip *sampleClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveSampleClip( song, arguments, sampleClip, address, failure ) )
	{
		return failure;
	}

	QString path;
	QString error;
	if( !readRequiredString( arguments, "path", path, error ) )
	{
		return invalidArguments( error );
	}
	const QFileInfo sourceFile( path );
	if( !sourceFile.exists() || !sourceFile.isFile() )
	{
		return CommandResult::failure( "sample_file_not_found", "The requested sample file does not exist." );
	}

	sampleClip->setSampleFile( sourceFile.absoluteFilePath() );
	if( sampleClip->sample().sampleSize() == 0 )
	{
		return CommandResult::failure( "sample_load_failed", "LMMS could not decode the requested sample file." );
	}

	song->setModified();
	return CommandResult::success( sampleInfo( address, sampleClip ) );
}



CommandResult executeSampleSetReversed( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	SampleClip *sampleClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveSampleClip( song, arguments, sampleClip, address, failure ) )
	{
		return failure;
	}

	bool reversed = false;
	QString error;
	if( !readRequiredBool( arguments, "value", reversed, error ) )
	{
		return invalidArguments( error );
	}

	sampleClip->setReversed( reversed );
	song->setModified();
	return CommandResult::success( sampleInfo( address, sampleClip ) );
}



CommandResult executeSampleSetOffset( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	SampleClip *sampleClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveSampleClip( song, arguments, sampleClip, address, failure ) )
	{
		return failure;
	}

	int offset = 0;
	QString error;
	if( !readNonNegativeInteger( arguments, "value", offset, error ) )
	{
		return invalidArguments( error );
	}
	if( offset >= sampleClip->length().getTicks() )
	{
		return invalidArguments( "'value' must be smaller than the sample clip length in ticks." );
	}

	sampleClip->setStartTimeOffset( TimePos( offset ) );
	song->setModified();
	return CommandResult::success( sampleInfo( address, sampleClip ) );
}



CommandResult executeSampleGetInfo( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	SampleClip *sampleClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveSampleClip( song, arguments, sampleClip, address, failure ) )
	{
		return failure;
	}

	return CommandResult::success( sampleInfo( address, sampleClip ) );
}



CommandResult executeQuerySongSummary( const QJsonObject &arguments )
{
	auto result = executeSongGetInfo( arguments );
	if( result.ok && arguments.value( "detail" ).toString( "compact" ) == "full" )
	{
		QJsonArray tracks;
		auto *song = Engine::getSong();
		for( std::size_t index = 0; index < song->tracks().size(); ++index )
		{
			tracks.append( trackDetail( song, song->tracks()[index], static_cast<int>( index ) ) );
		}
		result.data.insert( "tracks", tracks );
	}
	return result;
}




CommandResult executeQueryTrackDetail( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	Track *track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if( !resolveTrack( song, arguments, track, trackIndex, failure ) )
	{
		return failure;
	}
	return CommandResult::success( trackDetail( song, track, trackIndex ) );
}




CommandResult executeQueryTrackList( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	QJsonArray tracks;
	CommandResult failure;
	auto *container = resolveParent( song, arguments, failure );
	if( !container ) { return failure; }
	const auto &songTracks = container->tracks();
	for( std::size_t index = 0; index < songTracks.size(); ++index )
	{
		tracks.append( trackDetail( song, songTracks[index], static_cast<int>( index ) ) );
	}
	return CommandResult::success( QJsonObject{ { "tracks", tracks } } );
}




CommandResult executeQueryClipDetail( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	ClipAddress address;
	CommandResult failure;
	if( !resolveClip( song, arguments, address, failure ) )
	{
		return failure;
	}
	if( auto *automation = dynamic_cast<AutomationClip *>( address.clip ) )
	{
		return CommandResult::success( automationClipDetail( song, address, automation ) );
	}
	return CommandResult::success( clipDetail( address ) );
}




CommandResult executeQueryClipList( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	Track *track = nullptr;
	int trackIndex = -1;
	CommandResult failure;
	if( !resolveTrack( song, arguments, track, trackIndex, failure ) )
	{
		return failure;
	}

	QJsonArray clips;
	const auto &trackClips = track->getClips();
	for( std::size_t index = 0; index < trackClips.size(); ++index )
	{
		clips.append( clipDetail( ClipAddress{ track, trackClips[index], trackIndex,
			static_cast<int>( index ) } ) );
	}
	return CommandResult::success( QJsonObject{
		{ "path", trackPath( trackIndex, track ) },
		{ "clips", clips }
	} );
}




CommandResult executeQueryNotes( const QJsonObject &arguments )
{
	auto *song = Engine::getSong();
	if( song == nullptr )
	{
		return engineUnavailable();
	}

	MidiClip *midiClip = nullptr;
	ClipAddress address;
	CommandResult failure;
	if( !resolveMidiClip( song, arguments, midiClip, address, failure ) )
	{
		return failure;
	}

	NoteFilter filter;
	QString error;
	if( !readNoteFilter( arguments, filter, error ) ) { return invalidArguments( error ); }
	int page = 0;
	int pageSize = 256;
	if( arguments.contains( "page" ) && !readNonNegativeInteger( arguments, "page", page, error ) ) { return invalidArguments( error ); }
	if( arguments.contains( "pageSize" ) && !readRequiredInteger( arguments, "pageSize", pageSize, error ) ) { return invalidArguments( error ); }
	if( pageSize < 1 || pageSize > 4096 ) { return invalidArguments( "'pageSize' must be between 1 and 4096." ); }
	const auto first = static_cast<long long>( page ) * pageSize;
	int total = 0;
	QJsonArray notes;
	const auto &clipNotes = midiClip->notes();
	for( std::size_t index = 0; index < clipNotes.size(); ++index )
	{
		const auto *note = clipNotes[index];
		if( !filter.matches( note ) ) { continue; }
		if( total >= first && total < first + pageSize ) { notes.append( noteDetail( note, static_cast<int>( index ) ) ); }
		++total;
	}

	return CommandResult::success( QJsonObject{
		{ "path", clipPath( address.trackIndex, address.clipIndex, address.track ) },
		{ "total", total }, { "page", page }, { "pageSize", pageSize },
		{ "noteCount", static_cast<int>( clipNotes.size() ) },
		{ "notes", notes }
	} );
}




void registerDescriptor( CommandBus &commandBus, const QString &name, const QString &summary,
	const QJsonObject &schema, Mutability mutability, TxScope scope, CommandHandler handler )
{
	CommandDescriptor descriptor;
	descriptor.name = name;
	descriptor.summary = summary;
	descriptor.argsSchema = schema;
	auto properties = descriptor.argsSchema.value( "properties" ).toObject();
	if( properties.contains( "track" ) )
	{
		properties.insert( "track", QJsonObject{
			{ "anyOf", QJsonArray{ integerSchema(), stringSchema() } },
			{ "description", "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order." }
		} );
		properties.insert( "parent", QJsonObject{
			{ "type", "string" }, { "enum", QJsonArray{ "song", "pattern" } },
			{ "description", "Track container; defaults to song. A track path carries its own container." }
		} );
		descriptor.argsSchema.insert( "properties", properties );
	}
	if( name == "track.setName" || name == "mixer.setName" )
	{
		properties.insert( "value", stringSchema() );
		descriptor.argsSchema.insert( "properties", properties );
		descriptor.argsSchema.insert( "required", QJsonArray{ name == "track.setName" ? "track" : "channel" } );
		descriptor.argsSchema.insert( "oneOf", QJsonArray{
			objectSchema( {}, QJsonArray{ "name" } ), objectSchema( {}, QJsonArray{ "value" } )
		} );
		const auto setName = std::move( handler );
		handler = [setName]( QJsonObject args )
		{
			if( args.contains( "value" ) ) { args.insert( "name", args.take( "value" ) ); }
			return setName( args );
		};
	}
	descriptor.mutability = mutability;
	descriptor.scope = scope;
	descriptor.handler = std::move( handler );
	commandBus.registerCommand( descriptor );
}

} // namespace

void registerCoreCommands( CommandBus &commandBus )
{
	const auto noArguments = objectSchema();
	const auto trackArguments = objectSchema( QJsonObject{ { "track", integerSchema() } }, QJsonArray{ "track" } );
	const auto clipArguments = objectSchema( QJsonObject{
		{ "track", integerSchema() },
		{ "clip", integerSchema() }
	}, QJsonArray{ "track", "clip" } );
	const auto valueArguments = objectSchema( QJsonObject{
		{ "track", integerSchema() },
		{ "value", numberSchema() }
	}, QJsonArray{ "track", "value" } );
	const auto integerValueArguments = objectSchema( QJsonObject{
		{ "track", integerSchema() },
		{ "value", integerSchema() }
	}, QJsonArray{ "track", "value" } );
	const auto noteProperties = QJsonObject{
		{ "position", QJsonObject{ { "type", "integer" }, { "minimum", 0 }, { "maximum", MaxSongLength }, { "description", "Clip-local start in ticks." } } },
		{ "length", QJsonObject{ { "type", "integer" }, { "minimum", 1 }, { "maximum", MaxSongLength }, { "description", "Note duration in ticks." } } },
		{ "key", QJsonObject{ { "type", "integer" }, { "minimum", 0 }, { "maximum", NumKeys - 1 }, { "description", "LMMS key number." } } },
		{ "volume", QJsonObject{ { "type", "integer" }, { "minimum", MinVolume }, { "maximum", MaxVolume }, { "description", "Note volume in LMMS percent units." } } },
		{ "panning", QJsonObject{ { "type", "integer" }, { "minimum", PanningLeft }, { "maximum", PanningRight } } }
	};
	auto noteSchema = objectSchema( noteProperties, QJsonArray{ "position", "length", "key" } );
	noteSchema.insert( "additionalProperties", false );
	auto noteFieldsSchema = objectSchema( noteProperties );
	noteFieldsSchema.insert( "additionalProperties", false );
	auto rangeSchema = objectSchema( QJsonObject{ { "start", integerSchema() }, { "end", integerSchema() } }, QJsonArray{ "start", "end" } );
	rangeSchema.insert( "additionalProperties", false );
	rangeSchema.insert( "description", "Clip-local tick range: start inclusive, end exclusive; selects note starts." );
	const auto noteFilterProperties = QJsonObject{
		{ "track", integerSchema() }, { "clip", integerSchema() }, { "range", rangeSchema },
		{ "keys", QJsonObject{ { "type", "array" }, { "items", noteProperties.value( "key" ) },
			{ "description", "Optional key filter; omitted or empty selects all keys." } } }
	};
	auto noteQueryProperties = noteFilterProperties;
	noteQueryProperties.insert( "page", QJsonObject{ { "type", "integer" }, { "minimum", 0 }, { "description", "Zero-based page; defaults to zero." } } );
	noteQueryProperties.insert( "pageSize", QJsonObject{ { "type", "integer" }, { "minimum", 1 }, { "maximum", 4096 }, { "description", "Notes per page; defaults to 256." } } );
	const auto noteQueryArguments = objectSchema( noteQueryProperties, QJsonArray{ "track", "clip" } );

	registerDescriptor( commandBus, "song.getInfo", "Return a compact summary of the current song.",
		noArguments, Mutability::ReadOnly, TxScope::None, executeSongGetInfo );
	registerDescriptor( commandBus, "song.setTempo", "Set the song tempo in BPM.", objectSchema(
		QJsonObject{ { "bpm", integerSchema() } }, QJsonArray{ "bpm" } ),
		Mutability::Mutating, TxScope::Single, executeSongSetTempo );
	registerDescriptor( commandBus, "song.setTimeSignature", "Set the song time signature.", objectSchema(
		QJsonObject{ { "numerator", integerSchema() }, { "denominator", integerSchema() } },
		QJsonArray{ "numerator", "denominator" } ),
		Mutability::Mutating, TxScope::Single, executeSongSetTimeSignature );
	registerDescriptor( commandBus, "song.setMasterVolume", "Set the master volume.", objectSchema(
		QJsonObject{ { "value", integerSchema() } }, QJsonArray{ "value" } ),
		Mutability::Mutating, TxScope::Single, executeSongSetMasterVolume );
	registerDescriptor( commandBus, "song.setMasterPitch", "Set the master pitch in semitones.", objectSchema(
		QJsonObject{ { "semitones", integerSchema() } }, QJsonArray{ "semitones" } ),
		Mutability::Mutating, TxScope::Single, executeSongSetMasterPitch );
	registerDescriptor( commandBus, "song.clearProject", "Clear every track and controller from the current project.",
		noArguments, Mutability::Destructive, TxScope::Single, executeSongClearProject );
	registerDescriptor( commandBus, "song.load", "Load a validated local LMMS project file.", objectSchema(
		QJsonObject{ { "path", stringSchema() }, { "promptSave", booleanSchema() } }, QJsonArray{ "path" } ),
		Mutability::Destructive, TxScope::Single, executeSongLoad );
	registerDescriptor( commandBus, "song.save", "Save the current project, optionally as a resource bundle.", objectSchema(
		QJsonObject{ { "path", stringSchema() }, { "asBundle", booleanSchema() } } ),
		Mutability::Mutating, TxScope::None, executeSongSave );

	const QJsonObject playbackProperties{
		{ "mode", stringSchema() }, { "track", integerSchema() }, { "clip", integerSchema() },
		{ "pattern", integerSchema() }, { "loop", booleanSchema() },
		{ "fromBar", integerSchema() }, { "ticks", integerSchema() }
	};
	registerDescriptor( commandBus, "song.setPlayMode", "Select Song, Pattern, MidiClip or AutomationClip playback.",
		objectSchema( playbackProperties, QJsonArray{ "mode" } ), Mutability::Mutating, TxScope::None, executeSongSetPlayMode );
	registerDescriptor( commandBus, "transport.play", "Start playback in the selected mode.", objectSchema( playbackProperties ),
		Mutability::Mutating, TxScope::None, executeTransportPlay );
	registerDescriptor( commandBus, "transport.playSong", "Start song playback.", noArguments,
		Mutability::Mutating, TxScope::None, []( QJsonObject args ) { args.insert( "mode", "Song" ); return executeTransportPlay( args ); } );
	registerDescriptor( commandBus, "transport.stop", "Stop playback.", noArguments,
		Mutability::Mutating, TxScope::None, executeTransportStop );
	registerDescriptor( commandBus, "transport.togglePause", "Toggle playback pause state.", noArguments,
		Mutability::Mutating, TxScope::None, executeTransportTogglePause );
	registerDescriptor( commandBus, "transport.setPosition", "Set the song playhead position.", objectSchema(
		QJsonObject{ { "bar", integerSchema() }, { "ticks", integerSchema() } } ),
		Mutability::Mutating, TxScope::None, executeTransportSetPosition );
	registerDescriptor( commandBus, "transport.seek", "Set the song playhead position in ticks.", objectSchema(
		QJsonObject{ { "ticks", integerSchema() } }, QJsonArray{ "ticks" } ),
		Mutability::Mutating, TxScope::None, executeTransportSetPosition );
	registerDescriptor( commandBus, "transport.getPosition", "Return the current playback position.", noArguments,
		Mutability::ReadOnly, TxScope::None, executeTransportGetPosition );
	registerDescriptor( commandBus, "transport.setLoopRange", "Set and enable the song loop range in bars.", objectSchema(
		QJsonObject{ { "startBar", integerSchema() }, { "endBar", integerSchema() } },
		QJsonArray{ "startBar", "endBar" } ),
		Mutability::Mutating, TxScope::Single, executeTransportSetLoopRange );

	registerDescriptor( commandBus, "transport.previewClip", "Preview one MIDI clip without changing the project.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() }, { "loop", booleanSchema() } },
		QJsonArray{ "track", "clip" } ),
		Mutability::Mutating, TxScope::None, executeTransportPreviewClip );

	registerDescriptor( commandBus, "track.create", "Create an instrument, sample, or automation track.", objectSchema(
		QJsonObject{ { "type", stringSchema() }, { "name", stringSchema() },
			{ "parent", QJsonObject{ { "type", "string" }, { "enum", QJsonArray{ "song", "pattern" } } } },
			{ "index", integerSchema() } } ),
		Mutability::Mutating, TxScope::Single, executeTrackCreate );
	registerDescriptor( commandBus, "track.clone", "Clone a track with its clips and plugin settings.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "name", stringSchema() } }, QJsonArray{ "track" } ),
		Mutability::Mutating, TxScope::Single, executeTrackClone );
	registerDescriptor( commandBus, "track.move", "Move a track to a zero-based position in its container.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "newIndex", integerSchema() } }, QJsonArray{ "track", "newIndex" } ),
		Mutability::Mutating, TxScope::Single, executeTrackMove );
	const auto colorSchema = QJsonObject{ { "anyOf", QJsonArray{ stringSchema(), QJsonObject{ { "type", "null" } } } } };
	for( const auto &property : { QString( "Height" ), QString( "Color" ), QString( "MixerChannel" ) } )
	{
		const auto field = property == "MixerChannel" ? QString( "channel" ) : QString( "value" );
		registerDescriptor( commandBus, "track.set" + property, "Set the track " + property.toLower() + ".", objectSchema(
			QJsonObject{ { "track", integerSchema() }, { field, property == "Color" ? colorSchema : integerSchema() } },
			QJsonArray{ "track", field } ), Mutability::Mutating, TxScope::Single,
			[property]( const QJsonObject &args ) { return executeTrackProperty( args, property == "MixerChannel" ? "mixerChannel" : property.toLower() ); } );
	}
	registerDescriptor( commandBus, "track.remove", "Remove a track.", trackArguments,
		Mutability::Destructive, TxScope::Single, executeTrackRemove );
	registerDescriptor( commandBus, "track.delete", "Remove a track.", trackArguments,
		Mutability::Destructive, TxScope::Single, executeTrackRemove );
	registerDescriptor( commandBus, "track.setName", "Set a track name.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "name", stringSchema() } },
		QJsonArray{ "track", "name" } ), Mutability::Mutating, TxScope::Single, executeTrackSetName );
	registerDescriptor( commandBus, "track.setMuted", "Set a track mute state.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "value", booleanSchema() } },
		QJsonArray{ "track", "value" } ), Mutability::Mutating, TxScope::Single, executeTrackSetMuted );
	registerDescriptor( commandBus, "track.setMute", "Set a track mute state.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "value", booleanSchema() } },
		QJsonArray{ "track", "value" } ), Mutability::Mutating, TxScope::Single, executeTrackSetMuted );
	registerDescriptor( commandBus, "track.setSolo", "Set a track solo state.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "value", booleanSchema() } },
		QJsonArray{ "track", "value" } ), Mutability::Mutating, TxScope::Single, executeTrackSetSolo );
	registerDescriptor( commandBus, "track.list", "List tracks in the song or pattern container.", objectSchema(
		QJsonObject{ { "parent", QJsonObject{ { "type", "string" }, { "enum", QJsonArray{ "song", "pattern" } } } } } ),
		Mutability::ReadOnly, TxScope::None, executeQueryTrackList );
	registerDescriptor( commandBus, "track.get", "Return a track detail object.", trackArguments,
		Mutability::ReadOnly, TxScope::None, executeQueryTrackDetail );

	registerDescriptor( commandBus, "instrument.load", "Load an instrument plugin onto an instrument track.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "plugin", stringSchema() }, { "path", stringSchema() },
			{ "subKey", objectSchema( QJsonObject{ { "name", stringSchema() },
				{ "attributes", QJsonObject{ { "type", "object" }, { "additionalProperties", stringSchema() } } } }, QJsonArray{ "attributes" } ) } },
		QJsonArray{ "track", "plugin" } ), Mutability::Mutating, TxScope::Single, executeInstrumentLoad );
	registerDescriptor( commandBus, "instrument.setParameters", "Set one or more instrument-track parameters.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "volume", numberSchema() },
			{ "panning", numberSchema() }, { "pitch", numberSchema() },
			{ "pitchRange", integerSchema() }, { "baseNote", integerSchema() } }, QJsonArray{ "track" } ),
		Mutability::Mutating, TxScope::Single, executeInstrumentSetParameters );
	registerDescriptor( commandBus, "instrument.setVolume", "Set an instrument-track volume.", valueArguments,
		Mutability::Mutating, TxScope::Single, executeInstrumentSetVolume );
	registerDescriptor( commandBus, "instrument.setPanning", "Set an instrument-track panning value.", valueArguments,
		Mutability::Mutating, TxScope::Single, executeInstrumentSetPanning );
	registerDescriptor( commandBus, "instrument.setPitch", "Set instrument-track pitch in cents within its current pitch range.", valueArguments,
		Mutability::Mutating, TxScope::Single, executeInstrumentSetPitch );
	registerDescriptor( commandBus, "instrument.setPitchRange", "Set an instrument-track pitch range.", integerValueArguments,
		Mutability::Mutating, TxScope::Single, executeInstrumentSetPitchRange );
	registerDescriptor( commandBus, "instrument.setBaseNote", "Set an instrument-track base note.", integerValueArguments,
		Mutability::Mutating, TxScope::Single, executeInstrumentSetBaseNote );
	registerDescriptor( commandBus, "instrument.getParams", "List L1 serialized parameters for an instrument plugin.",
		trackArguments, Mutability::ReadOnly, TxScope::None, executeInstrumentGetParams );
	registerDescriptor( commandBus, "instrument.setParam", "Set one L1 serialized instrument parameter.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "name", stringSchema() }, { "value", QJsonObject{} } },
		QJsonArray{ "track", "name", "value" } ),
		Mutability::Mutating, TxScope::Single, executeInstrumentSetParam );

	const auto mixerChannelArguments = objectSchema( QJsonObject{ { "channel", integerSchema() } },
		QJsonArray{ "channel" } );
	const auto mixerRouteArguments = objectSchema( QJsonObject{
		{ "from", integerSchema() }, { "to", integerSchema() }
	}, QJsonArray{ "from", "to" } );
	registerDescriptor( commandBus, "mixer.getChannel", "Return a mixer channel detail object.",
		mixerChannelArguments, Mutability::ReadOnly, TxScope::None, executeMixerGetChannel );
	registerDescriptor( commandBus, "mixer.getMaster", "Return the master mixer channel detail object.",
		noArguments, Mutability::ReadOnly, TxScope::None, executeMixerGetMaster );
	registerDescriptor( commandBus, "mixer.listChannels", "List all mixer channels.",
		noArguments, Mutability::ReadOnly, TxScope::None, executeMixerListChannels );
	registerDescriptor( commandBus, "mixer.setName", "Set a mixer channel name.", objectSchema(
		QJsonObject{ { "channel", integerSchema() }, { "name", stringSchema() } },
		QJsonArray{ "channel", "name" } ), Mutability::Mutating, TxScope::Single, executeMixerSetName );
	registerDescriptor( commandBus, "mixer.setVolume", "Set a mixer channel volume.", objectSchema(
		QJsonObject{ { "channel", integerSchema() }, { "value", numberSchema() } },
		QJsonArray{ "channel", "value" } ), Mutability::Mutating, TxScope::Single, executeMixerSetVolume );
	registerDescriptor( commandBus, "mixer.setMute", "Set a mixer channel mute state.", objectSchema(
		QJsonObject{ { "channel", integerSchema() }, { "value", booleanSchema() } },
		QJsonArray{ "channel", "value" } ), Mutability::Mutating, TxScope::Single, executeMixerSetMute );
	registerDescriptor( commandBus, "mixer.setSolo", "Set a mixer channel solo state.", objectSchema(
		QJsonObject{ { "channel", integerSchema() }, { "value", booleanSchema() } },
		QJsonArray{ "channel", "value" } ), Mutability::Mutating, TxScope::Single, executeMixerSetSolo );
	registerDescriptor( commandBus, "mixer.setColor", "Set or clear a mixer channel color.", objectSchema(
		QJsonObject{ { "channel", integerSchema() }, { "value", stringSchema() } },
		QJsonArray{ "channel", "value" } ), Mutability::Mutating, TxScope::Single, executeMixerSetColor );
	registerDescriptor( commandBus, "mixer.addSend", "Create a mixer send.", objectSchema(
		QJsonObject{ { "from", integerSchema() }, { "to", integerSchema() }, { "amount", numberSchema() } },
		QJsonArray{ "from", "to" } ), Mutability::Mutating, TxScope::Single, executeMixerAddSend );
	registerDescriptor( commandBus, "mixer.setSendAmount", "Set a mixer send amount.", objectSchema(
		QJsonObject{ { "from", integerSchema() }, { "to", integerSchema() }, { "amount", numberSchema() } },
		QJsonArray{ "from", "to", "amount" } ),
		Mutability::Mutating, TxScope::Single, executeMixerSetSendAmount );
	registerDescriptor( commandBus, "mixer.removeSend", "Remove a mixer send.", mixerRouteArguments,
		Mutability::Destructive, TxScope::Single, executeMixerRemoveSend );
	registerDescriptor( commandBus, "mixer.clearChannel", "Clear all routes and effects from a mixer channel.",
		mixerChannelArguments, Mutability::Destructive, TxScope::Single, executeMixerClearChannel );

	const auto effectArguments = objectSchema( QJsonObject{
		{ "owner", stringSchema() }, { "slot", integerSchema() }
	}, QJsonArray{ "owner", "slot" } );
	registerDescriptor( commandBus, "effect.listAvailable", "List available effect plugins.", objectSchema(
		QJsonObject{ { "kind", stringSchema() } } ), Mutability::ReadOnly, TxScope::None, executeEffectListAvailable );
	registerDescriptor( commandBus, "effect.add", "Add an effect plugin to an effect chain.", objectSchema(
		QJsonObject{ { "owner", stringSchema() }, { "plugin", stringSchema() }, { "index", integerSchema() },
			{ "subKey", objectSchema( QJsonObject{ { "name", stringSchema() },
				{ "attributes", QJsonObject{ { "type", "object" }, { "additionalProperties", stringSchema() } } } }, QJsonArray{ "attributes" } ) } },
		QJsonArray{ "owner", "plugin" } ), Mutability::Mutating, TxScope::Single, executeEffectAdd );
	registerDescriptor( commandBus, "effect.remove", "Remove an effect from an effect chain.", effectArguments,
		Mutability::Destructive, TxScope::Single, executeEffectRemove );
	registerDescriptor( commandBus, "effect.move", "Move an effect to another slot.", objectSchema(
		QJsonObject{ { "owner", stringSchema() }, { "slot", integerSchema() }, { "index", integerSchema() } },
		QJsonArray{ "owner", "slot", "index" } ), Mutability::Mutating, TxScope::Single, executeEffectMove );
	registerDescriptor( commandBus, "effect.setEnabled", "Set an effect enabled state.", objectSchema(
		QJsonObject{ { "owner", stringSchema() }, { "slot", integerSchema() }, { "value", booleanSchema() } },
		QJsonArray{ "owner", "slot", "value" } ), Mutability::Mutating, TxScope::Single, executeEffectSetEnabled );
	registerDescriptor( commandBus, "effect.setWetDry", "Set an effect wet/dry value.", objectSchema(
		QJsonObject{ { "owner", stringSchema() }, { "slot", integerSchema() }, { "value", numberSchema() } },
		QJsonArray{ "owner", "slot", "value" } ), Mutability::Mutating, TxScope::Single, executeEffectSetWetDry );
	registerDescriptor( commandBus, "effect.getParams", "List L1 serialized effect parameters.", effectArguments,
		Mutability::ReadOnly, TxScope::None, executeEffectGetParams );
	registerDescriptor( commandBus, "effect.setParam", "Set one L1 serialized effect parameter.", objectSchema(
		QJsonObject{ { "owner", stringSchema() }, { "slot", integerSchema() },
			{ "name", stringSchema() }, { "value", QJsonObject{} } },
		QJsonArray{ "owner", "slot", "name", "value" } ),
		Mutability::Mutating, TxScope::Single, executeEffectSetParam );

	const auto modelPathArguments = objectSchema( QJsonObject{ { "path", stringSchema() } },
		QJsonArray{ "path" } );
	registerDescriptor( commandBus, "model.getValue", "Return an addressable model value.",
		modelPathArguments, Mutability::ReadOnly, TxScope::None, executeModelGetValue );
	registerDescriptor( commandBus, "model.setValue", "Set an addressable model value.", objectSchema(
		QJsonObject{ { "path", stringSchema() }, { "value", QJsonObject{ { "anyOf", QJsonArray{ numberSchema(), booleanSchema() } } } } }, QJsonArray{ "path", "value" } ),
		Mutability::Mutating, TxScope::Single, executeModelSetValue );
	registerDescriptor( commandBus, "model.list", "List addressable models, optionally under a prefix.", objectSchema(
		QJsonObject{ { "prefix", stringSchema() } } ), Mutability::ReadOnly, TxScope::None, executeModelList );
	registerDescriptor( commandBus, "model.search", "Search addressable models by path or name.", objectSchema(
		QJsonObject{ { "keyword", stringSchema() } } ), Mutability::ReadOnly, TxScope::None, executeModelSearch );

	const auto automationClipArguments = objectSchema( QJsonObject{
		{ "track", integerSchema() }, { "clip", integerSchema() }
	}, QJsonArray{ "track", "clip" } );
	registerDescriptor( commandBus, "automation.createTrack", "Create an automation track.", objectSchema(
		QJsonObject{ { "name", stringSchema() }, { "index", integerSchema() },
			{ "parent", QJsonObject{ { "type", "string" }, { "enum", QJsonArray{ "song", "pattern" } } } } } ),
		Mutability::Mutating, TxScope::Single, executeAutomationCreateTrack );
	registerDescriptor( commandBus, "automation.addClip", "Create an automation clip on an automation track.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "position", integerSchema() },
			{ "length", integerSchema() }, { "name", stringSchema() } },
		QJsonArray{ "track", "position", "length" } ), Mutability::Mutating, TxScope::Single, executeAutomationAddClip );
	registerDescriptor( commandBus, "automation.addTarget", "Add an addressable model as an automation target.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() },
			{ "target", stringSchema() }, { "nodes", QJsonObject{ { "type", "array" } } } },
		QJsonArray{ "track", "clip", "target" } ), Mutability::Mutating, TxScope::Single, executeAutomationAddTarget );
	registerDescriptor( commandBus, "automation.putValue", "Write one model value at a tick position.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() },
			{ "pos", integerSchema() }, { "value", numberSchema() } },
		QJsonArray{ "track", "clip", "pos", "value" } ), Mutability::Mutating, TxScope::Single, executeAutomationPutValue );
	registerDescriptor( commandBus, "automation.putValues", "Write one or more model-value automation nodes.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() },
			{ "nodes", QJsonObject{ { "type", "array" } } } }, QJsonArray{ "track", "clip", "nodes" } ),
		Mutability::Mutating, TxScope::Single, executeAutomationPutValues );
	registerDescriptor( commandBus, "automation.removeNode", "Remove the automation node at a tick position.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() }, { "pos", integerSchema() } },
		QJsonArray{ "track", "clip", "pos" } ), Mutability::Destructive, TxScope::Single, executeAutomationRemoveNode );
	auto automationRangeSchema = rangeSchema;
	automationRangeSchema.insert( "description", "Clip-local tick endpoints, both inclusive; reversed endpoints are normalized by LMMS." );
	auto removeNodesArguments = objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() }, { "range", automationRangeSchema },
			{ "start", integerSchema() }, { "end", integerSchema() } }, QJsonArray{ "track", "clip" } );
	removeNodesArguments.insert( "oneOf", QJsonArray{
		objectSchema( {}, QJsonArray{ "range" } ), objectSchema( {}, QJsonArray{ "start", "end" } )
	} );
	registerDescriptor( commandBus, "automation.removeNodes", "Remove automation nodes between inclusive tick endpoints.",
		removeNodesArguments, Mutability::Destructive, TxScope::Single, executeAutomationRemoveNodes );
	registerDescriptor( commandBus, "automation.setProgression", "Set an automation curve progression type.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() }, { "type", stringSchema() } },
		QJsonArray{ "track", "clip", "type" } ), Mutability::Mutating, TxScope::Single, executeAutomationSetProgression );
	registerDescriptor( commandBus, "automation.setTension", "Set a cubic automation curve tension.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() }, { "value", numberSchema() } },
		QJsonArray{ "track", "clip", "value" } ), Mutability::Mutating, TxScope::Single, executeAutomationSetTension );
	registerDescriptor( commandBus, "automation.listTargets", "List addressable automation targets under an optional scope.",
		objectSchema( QJsonObject{ { "scope", QJsonObject{ { "type", "string" }, { "enum", QJsonArray{ "track", "mixer", "effect", "song" } },
			{ "description", "Model domain; effect includes slots on tracks and mixer channels. Omitted selects all domains." } } } } ),
		Mutability::ReadOnly, TxScope::None, executeAutomationListTargets );

	registerDescriptor( commandBus, "clip.create", "Create a MIDI, sample or automation clip matching the track type.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "type", stringSchema() },
			{ "start", integerSchema() }, { "position", integerSchema() },
			{ "length", integerSchema() }, { "name", stringSchema() } }, QJsonArray{ "track" } ),
		Mutability::Mutating, TxScope::Single, executeClipCreate );
	registerDescriptor( commandBus, "clip.duplicate", "Duplicate a clip, by default immediately after the original.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() }, { "position", integerSchema() } },
		QJsonArray{ "track", "clip" } ), Mutability::Mutating, TxScope::Single, executeClipDuplicate );
	for( const auto &property : { QString( "Color" ), QString( "AutoResize" ), QString( "StartTimeOffset" ) } )
	{
		const auto schema = property == "Color" ? colorSchema : property == "AutoResize" ? booleanSchema() : integerSchema();
		registerDescriptor( commandBus, "clip.set" + property, "Set the clip " + property.toLower() + ".", objectSchema(
			QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() }, { "value", schema } },
			QJsonArray{ "track", "clip", "value" } ), Mutability::Mutating, TxScope::Single,
			[property]( const QJsonObject &args ) { return executeClipProperty( args, property == "AutoResize" ? "autoResize" : property == "Color" ? "color" : "startTimeOffset" ); } );
	}
	registerDescriptor( commandBus, "clip.remove", "Remove a clip.", clipArguments,
		Mutability::Destructive, TxScope::Single, executeClipRemove );
	registerDescriptor( commandBus, "clip.delete", "Remove a clip.", clipArguments,
		Mutability::Destructive, TxScope::Single, executeClipRemove );
	registerDescriptor( commandBus, "clip.move", "Move a clip in ticks.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() },
			{ "position", integerSchema() }, { "bar", integerSchema() } }, QJsonArray{ "track", "clip" } ),
		Mutability::Mutating, TxScope::Single, executeClipSetPosition );
	registerDescriptor( commandBus, "clip.setPosition", "Move a clip in ticks.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() },
			{ "position", integerSchema() } }, QJsonArray{ "track", "clip", "position" } ),
		Mutability::Mutating, TxScope::Single, executeClipSetPosition );
	registerDescriptor( commandBus, "clip.resize", "Set a clip length in ticks.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() },
			{ "length", integerSchema() } }, QJsonArray{ "track", "clip", "length" } ),
		Mutability::Mutating, TxScope::Single, executeClipSetLength );
	registerDescriptor( commandBus, "clip.setLength", "Set a clip length in ticks.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() },
			{ "length", integerSchema() } }, QJsonArray{ "track", "clip", "length" } ),
		Mutability::Mutating, TxScope::Single, executeClipSetLength );
	registerDescriptor( commandBus, "clip.setMute", "Set a clip mute state.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() },
			{ "value", booleanSchema() } }, QJsonArray{ "track", "clip", "value" } ),
		Mutability::Mutating, TxScope::Single, executeClipSetMute );
	registerDescriptor( commandBus, "clip.get", "Return a clip detail object.", clipArguments,
		Mutability::ReadOnly, TxScope::None, executeQueryClipDetail );
	registerDescriptor( commandBus, "clip.list", "List clips on a track.", trackArguments,
		Mutability::ReadOnly, TxScope::None, executeQueryClipList );

	registerDescriptor( commandBus, "midi.addNotes", "Add MIDI notes to a MIDI clip.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() },
			{ "notes", QJsonObject{ { "type", "array" }, { "items", noteSchema }, { "maxItems", 4096 } } } }, QJsonArray{ "track", "clip", "notes" } ),
		Mutability::Mutating, TxScope::Single, executeMidiAddNotes );
	registerDescriptor( commandBus, "midi.updateNote", "Update a note by its current index and return its index after sorting.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() }, { "note", integerSchema() }, { "fields", noteFieldsSchema } },
		QJsonArray{ "track", "clip", "note", "fields" } ), Mutability::Mutating, TxScope::Single, executeMidiUpdateNote );
	registerDescriptor( commandBus, "midi.removeNotes", "Remove notes matching optional start range and key filters; no filters removes all notes.",
		objectSchema( noteFilterProperties, QJsonArray{ "track", "clip" } ), Mutability::Destructive, TxScope::Single,
		[]( const QJsonObject &args ) { return executeMidiEditNotes( args, "remove" ); } );
	auto quantizeProperties = noteFilterProperties;
	quantizeProperties.insert( "grid", QJsonObject{ { "type", "integer" },
		{ "enum", QJsonArray{ 1, 2, 4, 8, 16, 32, 64, 3, 6, 12, 24, 48, 96, 192 } },
		{ "description", "Piano-roll note division, e.g. 16 for sixteenth notes, 24 for sixteenth-note triplets." } } );
	quantizeProperties.insert( "strength", QJsonObject{ { "type", "number" }, { "minimum", 0 }, { "maximum", 1 }, { "description", "Fraction of the distance to the grid; defaults to 1." } } );
	registerDescriptor( commandBus, "midi.quantize", "Quantize matching note starts using the piano-roll grid.",
		objectSchema( quantizeProperties, QJsonArray{ "track", "clip", "grid" } ), Mutability::Mutating, TxScope::Single,
		[]( const QJsonObject &args ) { return executeMidiEditNotes( args, "quantize" ); } );
	auto transposeProperties = noteFilterProperties;
	transposeProperties.insert( "semitones", integerSchema() );
	registerDescriptor( commandBus, "midi.transpose", "Transpose matching notes; fail atomically if any result is outside the LMMS key range.",
		objectSchema( transposeProperties, QJsonArray{ "track", "clip", "semitones" } ), Mutability::Mutating, TxScope::Single,
		[]( const QJsonObject &args ) { return executeMidiEditNotes( args, "transpose" ); } );
	registerDescriptor( commandBus, "midi.clearNotes", "Remove all MIDI notes from a clip.", clipArguments,
		Mutability::Destructive, TxScope::Single, executeMidiClearNotes );
	registerDescriptor( commandBus, "midi.setSteps", "Set a MIDI clip's number of step positions.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() }, { "steps", integerSchema() } },
		QJsonArray{ "track", "clip", "steps" } ),
		Mutability::Mutating, TxScope::Single, executeMidiSetSteps );
	registerDescriptor( commandBus, "midi.setClipType", "Set a MIDI clip to beat or melody mode.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() }, { "type", stringSchema() } },
		QJsonArray{ "track", "clip", "type" } ),
		Mutability::Mutating, TxScope::Single, executeMidiSetClipType );
	registerDescriptor( commandBus, "midi.humanize", "Apply deterministic timing, velocity, and detune variation to MIDI notes.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() },
			{ "timing", integerSchema() }, { "velocity", integerSchema() },
			{ "detune", numberSchema() }, { "seed", integerSchema() } },
		QJsonArray{ "track", "clip" } ),
		Mutability::Mutating, TxScope::Single, executeMidiHumanize );
	registerDescriptor( commandBus, "midi.getNotes", "Return a filtered page of MIDI notes with their current indices.", noteQueryArguments,
		Mutability::ReadOnly, TxScope::None, executeQueryNotes );

	registerDescriptor( commandBus, "sample.setFile", "Load a local audio file into a sample clip.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() }, { "path", stringSchema() } },
		QJsonArray{ "track", "clip", "path" } ),
		Mutability::Mutating, TxScope::Single, executeSampleSetFile );
	registerDescriptor( commandBus, "sample.setReversed", "Set whether a sample clip plays in reverse.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() }, { "value", booleanSchema() } },
		QJsonArray{ "track", "clip", "value" } ),
		Mutability::Mutating, TxScope::Single, executeSampleSetReversed );
	registerDescriptor( commandBus, "sample.setOffset", "Set a sample clip's timeline offset in ticks.", objectSchema(
		QJsonObject{ { "track", integerSchema() }, { "clip", integerSchema() }, { "value", integerSchema() } },
		QJsonArray{ "track", "clip", "value" } ),
		Mutability::Mutating, TxScope::Single, executeSampleSetOffset );
	registerDescriptor( commandBus, "sample.getInfo", "Return metadata and peak information for a sample clip.", clipArguments,
		Mutability::ReadOnly, TxScope::None, executeSampleGetInfo );

	registerDescriptor( commandBus, "query.songSummary", "Return a compact summary or full track details.", objectSchema(
		QJsonObject{ { "detail", QJsonObject{ { "type", "string" }, { "enum", QJsonArray{ "compact", "full" } } } } } ),
		Mutability::ReadOnly, TxScope::None, executeQuerySongSummary );
	registerDescriptor( commandBus, "query.trackDetail", "Return a track detail object.", trackArguments,
		Mutability::ReadOnly, TxScope::None, executeQueryTrackDetail );
	registerDescriptor( commandBus, "query.clipDetail", "Return a clip detail object.", clipArguments,
		Mutability::ReadOnly, TxScope::None, executeQueryClipDetail );
	registerDescriptor( commandBus, "query.notes", "Return a filtered page of MIDI notes with their current indices.", noteQueryArguments,
		Mutability::ReadOnly, TxScope::None, executeQueryNotes );
	registerDescriptor( commandBus, "query.mixerState", "Return the current mixer channel state.", noArguments,
		Mutability::ReadOnly, TxScope::None, executeMixerListChannels );
	registerDescriptor( commandBus, "query.modelSearch", "Search addressable models by path or name.", objectSchema(
		QJsonObject{ { "keyword", stringSchema() } } ),
		Mutability::ReadOnly, TxScope::None, executeModelSearch );
}

} // namespace lmms::agent
