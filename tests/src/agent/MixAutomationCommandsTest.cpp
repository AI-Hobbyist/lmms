#include <QtTest>

#include <QJsonArray>
#include <QDir>
#include <QtMath>

#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "AutomatableModel.h"
#include "Engine.h"
#include "Effect.h"
#include "EffectControls.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "PatternStore.h"
#include "PluginFactory.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"
#include "agent/CommandBus.h"

namespace
{

class AgentTestEffectControls : public lmms::EffectControls
{
public:
	explicit AgentTestEffectControls( lmms::Effect *effect ) :
		EffectControls( effect ),
		m_volume( 100.0f, 0.0f, 200.0f, 0.1f, this, "Volume" )
	{
	}

	int controlCount() override
	{
		return 1;
	}

	void saveSettings( QDomDocument &document, QDomElement &element ) override
	{
		m_volume.saveSettings( document, element, "volume" );
	}

	void loadSettings( const QDomElement &element ) override
	{
		m_volume.loadSettings( element, "volume" );
	}

	QString nodeName() const override
	{
		return "agenttesteffectcontrols";
	}

	lmms::gui::EffectControlDialog * createView() override
	{
		return nullptr;
	}

private:
	lmms::FloatModel m_volume;
};




class AgentTestEffect : public lmms::Effect
{
public:
	explicit AgentTestEffect( lmms::Model *parent ) :
		Effect( &Descriptor, parent, nullptr ),
		m_controls( this )
	{
	}

	lmms::EffectControls * controls() override
	{
		return &m_controls;
	}

protected:
	ProcessStatus processImpl( lmms::SampleFrame *, lmms::f_cnt_t ) override
	{
		return ProcessStatus::Sleep;
	}

private:
	inline static const lmms::Plugin::Descriptor Descriptor{
		"agent-test-effect", "Agent Test Effect", "", "", 1,
		lmms::Plugin::Type::Effect, nullptr, nullptr
	};
	AgentTestEffectControls m_controls;
};



QJsonValue parameterValue( const QJsonArray &parameters, const QString &name )
{
	for( const auto &parameter : parameters )
	{
		const auto object = parameter.toObject();
		if( object.value( "name" ).toString() == name )
		{
			return object.value( "value" );
		}
	}
	return {};
}

} // namespace

class MixAutomationCommandsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		lmms::Engine::init( true );
	}

	void cleanupTestCase()
	{
		lmms::Engine::destroy();
	}

	void init()
	{
		lmms::Engine::getSong()->clearProject();
		lmms::Engine::projectJournal()->clearJournal();
	}

	void insertsAutomationTracksAndWritesTypedModels()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		QVERIFY( bus.execute( "track.create", { { "name", "Instrument" } } ).ok );
		QVERIFY( bus.execute( "automation.createTrack", { { "index", 0 }, { "name", "Inserted" } } ).ok );
		QCOMPARE( bus.execute( "track.get", { { "track", 0 } } ).data.value( "name" ).toString(), QString( "Inserted" ) );
		QVERIFY( bus.execute( "history.undo" ).ok );
		QCOMPARE( lmms::Engine::getSong()->tracks().size(), std::size_t{ 1 } );
		QVERIFY( bus.execute( "history.redo" ).ok );
		QCOMPARE( bus.execute( "track.get", { { "track", 0 } } ).data.value( "name" ).toString(), QString( "Inserted" ) );
		QVERIFY( !bus.execute( "automation.createTrack", { { "index", 3 } } ).ok );
		QCOMPARE( lmms::Engine::getSong()->tracks().size(), std::size_t{ 2 } );
		const auto mute = bus.execute( "model.setValue", { { "path", "song/channel:0/mute" }, { "value", true } } );
		QVERIFY( mute.ok );
		QVERIFY( mute.data.value( "value" ).isBool() );
		QCOMPARE( mute.data.value( "value" ).toBool(), true );
		QVERIFY( bus.execute( "model.setValue", { { "path", "song/channel:0/mute" }, { "value", false }, { "dryRun", true } } ).ok );
		QCOMPARE( bus.execute( "model.getValue", { { "path", "song/channel:0/mute" } } ).data.value( "value" ).toBool(), true );
		QVERIFY( !bus.execute( "model.setValue", { { "path", "song/channel:0/mute" }, { "value", 0.5 } } ).ok );
		QVERIFY( !bus.execute( "model.setValue", { { "path", "song/tempo" }, { "value", true } } ).ok );
		QVERIFY( bus.execute( "model.setValue", { { "path", "song/channel:0/mute" }, { "value", 0 } } ).ok );
		QCOMPARE( bus.execute( "model.getValue", { { "path", "song/channel:0/mute" } } ).data.value( "value" ).toBool(), false );
	}

	void removesAutomationNodesByInclusiveRange()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		QVERIFY( bus.execute( "automation.createTrack" ).ok );
		QVERIFY( bus.execute( "automation.addClip", { { "track", 0 }, { "position", 0 }, { "length", 192 } } ).ok );
		QVERIFY( bus.execute( "automation.addTarget", { { "track", 0 }, { "clip", 0 }, { "target", "song/masterVolume" } } ).ok );
		QVERIFY( bus.execute( "automation.putValues", { { "track", 0 }, { "clip", 0 }, { "nodes", QJsonArray{
			QJsonObject{ { "pos", 0 }, { "inValue", 90 } }, QJsonObject{ { "pos", 12 }, { "inValue", 80 } },
			QJsonObject{ { "pos", 24 }, { "inValue", 100 } }, QJsonObject{ { "pos", 36 }, { "inValue", 120 } }
		} } } ).ok );
		const auto removed = bus.execute( "automation.removeNodes", { { "track", 0 }, { "clip", 0 },
			{ "range", QJsonObject{ { "start", 12 }, { "end", 24 } } } } );
		QVERIFY( removed.ok );
		QCOMPARE( removed.data.value( "nodes" ).toArray().size(), 2 );
		QVERIFY( bus.execute( "history.undo" ).ok );
		const auto preview = bus.execute( "automation.removeNodes", { { "track", 0 }, { "clip", 0 },
			{ "start", 12 }, { "end", 24 }, { "dryRun", true } } );
		QVERIFY( preview.ok );
		QCOMPARE( preview.data.value( "nodes" ).toArray().size(), 2 );
		QVERIFY( !bus.execute( "automation.removeNodes", { { "track", 0 }, { "clip", 0 }, { "start", 12 },
			{ "range", QJsonObject{ { "start", 12 }, { "end", 24 } } } } ).ok );
		QCOMPARE( bus.execute( "clip.get", { { "track", 0 }, { "clip", 0 } } ).data.value( "nodes" ).toArray().size(), 4 );
	}

	void assignsMixerChannelsWithoutGui()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		for( const auto &parent : { QString( "song" ), QString( "pattern" ) } )
		{
			QVERIFY( bus.execute( "track.create", { { "parent", parent }, { "type", "instrument" } } ).ok );
			QVERIFY( bus.execute( "track.create", { { "parent", parent }, { "type", "sample" } } ).ok );
		}
		const int channel = lmms::Engine::mixer()->createChannel();
		QVERIFY( channel > 0 );
		QVERIFY( bus.execute( "mixer.setName", { { "channel", channel }, { "value", "Bus" } } ).ok );
		QCOMPARE( bus.execute( "mixer.getChannel", { { "channel", channel } } ).data.value( "name" ).toString(), QString( "Bus" ) );
		for( const auto &parent : { QString( "song" ), QString( "pattern" ) } )
		{
			for( int index = 0; index < 2; ++index )
			{
				const auto path = QStringLiteral( "%1/track:%2" ).arg( parent ).arg( index );
				const auto assigned = bus.execute( "track.setMixerChannel", { { "track", path }, { "channel", channel } } );
				QVERIFY( assigned.ok );
				QCOMPARE( assigned.data.value( "mixerChannel" ).toInt(), channel );
				QVERIFY( bus.execute( "history.undo" ).ok );
				QCOMPARE( bus.execute( "track.get", { { "track", path } } ).data.value( "mixerChannel" ).toInt(), 0 );
				QVERIFY( bus.execute( "history.redo" ).ok );
				QCOMPARE( bus.execute( "track.get", { { "track", path } } ).data.value( "mixerChannel" ).toInt(), channel );
				const auto modelPath = path + ( index == 0 ? "/instrument/mixerChannel" : "/mixerChannel" );
				const auto model = bus.execute( "model.getValue", { { "path", modelPath } } );
				QVERIFY( model.ok );
				QCOMPARE( model.data.value( "max" ).toInt(), lmms::Engine::mixer()->numChannels() - 1 );
				QVERIFY( bus.execute( "model.setValue", { { "path", modelPath }, { "value", 0 }, { "dryRun", true } } ).ok );
				QCOMPARE( bus.execute( "track.get", { { "track", path } } ).data.value( "mixerChannel" ).toInt(), channel );
				QVERIFY( bus.execute( "model.setValue", { { "path", modelPath }, { "value", 0 } } ).ok );
				QVERIFY( bus.execute( "model.setValue", { { "path", modelPath }, { "value", channel } } ).ok );
				QCOMPARE( bus.execute( "track.get", { { "track", path } } ).data.value( "mixerChannel" ).toInt(), channel );
			}
		}
	}

	void filtersTargetDomainsAndIncludesPatternTracks()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		QVERIFY( bus.execute( "track.create", { { "parent", "pattern" }, { "name", "Pattern instrument" } } ).ok );
		QVERIFY( bus.execute( "model.setValue", { { "path", "pattern/track:0/instrument/volume" }, { "value", 123 } } ).ok );
		QCOMPARE( bus.execute( "model.getValue", { { "path", "pattern/track:0/instrument/volume" } } ).data.value( "value" ).toDouble(), 123.0 );
		QVERIFY( bus.execute( "automation.createTrack", { { "parent", "pattern" }, { "index", 0 } } ).ok );
		QCOMPARE( bus.execute( "track.get", { { "track", "pattern/track:0" } } ).data.value( "type" ).toString(), QString( "automation" ) );
		auto *instrument = dynamic_cast<lmms::InstrumentTrack *>(lmms::Engine::patternStore()->tracks()[1]);
		auto *trackChain = instrument->audioBusHandle()->effects();
		trackChain->appendEffect( new AgentTestEffect( trackChain ) );
		auto *mixerChain = &lmms::Engine::mixer()->mixerChannel( 0 )->m_fxChain;
		mixerChain->appendEffect( new AgentTestEffect( mixerChain ) );
		QVERIFY( bus.execute( "effect.getParams", { { "owner", "pattern/track:1" }, { "slot", 0 } } ).ok );
		const auto all = bus.execute( "automation.listTargets" ).data.value( "models" ).toArray();
		int count = 0;
		for( const auto &scope : { QString( "song" ), QString( "track" ), QString( "mixer" ), QString( "effect" ) } )
		{
			const auto listed = bus.execute( "automation.listTargets", { { "scope", scope } } );
			QVERIFY( listed.ok );
			const auto models = listed.data.value( "models" ).toArray();
			QVERIFY( !models.isEmpty() );
			count += models.size();
			for( const auto &model : models )
			{
				const auto path = model.toObject().value( "path" ).toString();
				if( scope == "effect" ) { QVERIFY( path.contains( "/fx:" ) ); }
				else { QVERIFY( !path.contains( "/fx:" ) ); }
				if( scope == "song" ) { QVERIFY( !path.contains( "/track:" ) && !path.contains( "/channel:" ) ); }
				if( scope == "mixer" ) { QVERIFY( path.startsWith( "song/channel:" ) ); }
				if( scope == "track" ) { QVERIFY( path.contains( "/track:" ) ); }
			}
		}
		QCOMPARE( count, all.size() );
		QVERIFY( !bus.execute( "automation.listTargets", { { "scope", "invalid" } } ).ok );
	}

	void loadsNativeEffectsAndSubPlugins()
	{
		const auto pluginDirectory = qEnvironmentVariable( "LMMS_AGENT_PLUGIN_TEST_PATH" );
		if( pluginDirectory.isEmpty() ) { QSKIP( "Set LMMS_AGENT_PLUGIN_TEST_PATH and LADSPA_PATH for native integration." ); }
		QDir::setSearchPaths( "plugins", { pluginDirectory } );
		lmms::PluginFactory::instance()->discoverPlugins();
		auto &bus = lmms::agent::CommandBus::instance();
		QJsonObject subKey;
		const auto available = bus.execute( "effect.listAvailable" );
		QVERIFY( available.ok );
		for( const auto &plugin : available.data.value( "plugins" ).toArray() )
		{
			const auto object = plugin.toObject();
			if( object.value( "plugin" ).toString() != "ladspaeffect" ) { continue; }
			for( const auto &key : object.value( "subKeys" ).toArray() )
			{
				if( key.toObject().value( "name" ).toString().contains( "Amplifier", Qt::CaseInsensitive ) )
				{
					subKey = key.toObject();
					break;
				}
			}
		}
		QVERIFY2( !subKey.isEmpty(), "The CMT LADSPA amplifier must be discovered." );
		QVERIFY( bus.execute( "track.create", { { "parent", "pattern" } } ).ok );
		QVERIFY( bus.execute( "effect.add", { { "owner", "pattern/track:0" }, { "plugin", "amplifier" } } ).ok );
		const auto selected = bus.execute( "effect.add", { { "owner", "pattern/track:0" }, { "plugin", "ladspaeffect" }, { "subKey", subKey }, { "index", 0 } } );
		QVERIFY2( selected.ok, qPrintable( selected.errorMessage ) );
		auto *instrument = dynamic_cast<lmms::InstrumentTrack *>(lmms::Engine::patternStore()->tracks()[0]);
		const auto attributes = subKey.value( "attributes" ).toObject();
		for( auto it = attributes.begin(); it != attributes.end(); ++it )
		{
			QCOMPARE( instrument->audioBusHandle()->effects()->effectAt( 0 )->key().attributes.value( it.key() ), it.value().toString() );
		}
		QVERIFY( bus.execute( "history.undo" ).ok );
		QVERIFY( bus.execute( "history.redo" ).ok );
		instrument = dynamic_cast<lmms::InstrumentTrack *>(lmms::Engine::patternStore()->tracks()[0]);
		QCOMPARE( QString( instrument->audioBusHandle()->effects()->effectAt( 0 )->descriptor()->name ), QString( "ladspaeffect" ) );
		for( auto it = attributes.begin(); it != attributes.end(); ++it )
		{
			QCOMPARE( instrument->audioBusHandle()->effects()->effectAt( 0 )->key().attributes.value( it.key() ), it.value().toString() );
		}
		QVERIFY( !bus.execute( "effect.add", { { "owner", "pattern/track:0" }, { "plugin", "ladspaeffect" },
			{ "subKey", QJsonObject{ { "attributes", QJsonObject{ { "file", "missing" } } } } } } ).ok );
		QVERIFY( !bus.execute( "effect.add", { { "owner", "pattern/track:0" }, { "plugin", "ladspaeffect" } } ).ok );
		QCOMPARE( dynamic_cast<lmms::InstrumentTrack *>(lmms::Engine::patternStore()->tracks()[0])->audioBusHandle()->effects()->effectCount(), 2 );
		QVERIFY( bus.execute( "effect.setParam", { { "owner", "pattern/track:0" }, { "slot", 1 }, { "name", "volume" }, { "value", 125 } } ).ok );
		const auto params = bus.execute( "effect.getParams", { { "owner", "pattern/track:0" }, { "slot", 1 } } );
		QCOMPARE( parameterValue( params.data.value( "parameters" ).toArray(), "volume" ).toDouble(), 125.0 );
		QVERIFY( bus.execute( "model.setValue", { { "path", "pattern/track:0/fx:0/enabled" }, { "value", false } } ).ok );
		QVERIFY( bus.execute( "effect.move", { { "owner", "pattern/track:0" }, { "slot", 0 }, { "index", 1 } } ).ok );
		QVERIFY( bus.execute( "effect.remove", { { "owner", "pattern/track:0" }, { "slot", 1 }, { "dryRun", true } } ).ok );
		QCOMPARE( dynamic_cast<lmms::InstrumentTrack *>(lmms::Engine::patternStore()->tracks()[0])->audioBusHandle()->effects()->effectCount(), 2 );
	}

	void executesMixerEffectModelAndAutomationCommands()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		auto *song = lmms::Engine::getSong();
		auto *mixer = lmms::Engine::mixer();
		const int ticksPerBar = lmms::TimePos::ticksPerBar();

		const auto instrumentTrack = bus.execute( "track.create", QJsonObject{
			{ "type", "instrument" },
			{ "name", "Automation source" }
		} );
		QVERIFY( instrumentTrack.ok );
		QCOMPARE( instrumentTrack.data.value( "index" ).toInt(), 0 );
		const auto unloadedInstrumentParameters = bus.execute( "instrument.getParams", QJsonObject{
			{ "track", 0 }
		} );
		QVERIFY( !unloadedInstrumentParameters.ok );
		QCOMPARE( unloadedInstrumentParameters.errorCode, QString( "instrument_not_loaded" ) );

		const int mixerChannel = mixer->createChannel();
		QVERIFY( mixerChannel > 0 );
		QVERIFY( bus.execute( "mixer.setName", QJsonObject{
			{ "channel", mixerChannel }, { "name", "Aux" }
		} ).ok );
		QVERIFY( bus.execute( "mixer.setVolume", QJsonObject{
			{ "channel", mixerChannel }, { "value", 0.75 }
		} ).ok );
		QVERIFY( bus.execute( "mixer.setColor", QJsonObject{
			{ "channel", mixerChannel }, { "value", "#335577" }
		} ).ok );
		QVERIFY( bus.execute( "mixer.setSolo", QJsonObject{
			{ "channel", mixerChannel }, { "value", true }
		} ).ok );
		QVERIFY( bus.execute( "mixer.setSolo", QJsonObject{
			{ "channel", mixerChannel }, { "value", false }
		} ).ok );
		QVERIFY( bus.execute( "mixer.setSendAmount", QJsonObject{
			{ "from", mixerChannel }, { "to", 0 }, { "amount", 0.25 }
		} ).ok );

		const auto channel = bus.execute( "mixer.getChannel", QJsonObject{ { "channel", mixerChannel } } );
		QVERIFY( channel.ok );
		QCOMPARE( channel.data.value( "name" ).toString(), QString( "Aux" ) );
		QVERIFY( qAbs( channel.data.value( "volume" ).toDouble() - 0.75 ) < 0.001 );
		QCOMPARE( channel.data.value( "color" ).toString(), QString( "#335577" ) );
		const auto sends = channel.data.value( "sends" ).toArray();
		QCOMPARE( sends.size(), 1 );
		QCOMPARE( sends.first().toObject().value( "to" ).toInt(), 0 );
		QVERIFY( qAbs( sends.first().toObject().value( "amount" ).toDouble() - 0.25 ) < 0.001 );

		const auto mixerState = bus.execute( "query.mixerState" );
		QVERIFY( mixerState.ok );
		QVERIFY( mixerState.data.value( "channels" ).toArray().size() > mixerChannel );
		const auto master = bus.execute( "mixer.getMaster" );
		QVERIFY( master.ok );
		QVERIFY( master.data.value( "master" ).toBool() );

		const auto model = bus.execute( "model.setValue", QJsonObject{
			{ "path", "song/track:0/instrument/volume" }, { "value", 80.0 }
		} );
		QVERIFY( model.ok );
		QVERIFY( qAbs( model.data.value( "value" ).toDouble() - 80.0 ) < 0.001 );
		const auto modelSearch = bus.execute( "query.modelSearch", QJsonObject{
			{ "keyword", "instrument/volume" }
		} );
		QVERIFY( modelSearch.ok );
		QVERIFY( !modelSearch.data.value( "models" ).toArray().isEmpty() );

		const auto automationTrack = bus.execute( "automation.createTrack", QJsonObject{
			{ "name", "Volume automation" }
		} );
		QVERIFY( automationTrack.ok );
		QCOMPARE( automationTrack.data.value( "index" ).toInt(), 1 );
		const auto automationClip = bus.execute( "automation.addClip", QJsonObject{
			{ "track", 1 }, { "position", 0 }, { "length", ticksPerBar }, { "name", "Fade" }
		} );
		QVERIFY( automationClip.ok );
		QCOMPARE( automationClip.data.value( "index" ).toInt(), 0 );
		QVERIFY( bus.execute( "automation.addTarget", QJsonObject{
			{ "track", 1 }, { "clip", 0 }, { "target", "song/track:0/instrument/volume" }
		} ).ok );
		const auto duplicateTarget = bus.execute( "automation.addTarget", QJsonObject{
			{ "track", 1 }, { "clip", 0 }, { "target", "song/track:0/instrument/volume" }
		} );
		QVERIFY( !duplicateTarget.ok );
		QCOMPARE( duplicateTarget.errorCode, QString( "automation_target_exists" ) );

		const int automationPosition = ticksPerBar / 2;
		constexpr double automationValue = 70.0;
		const auto putValue = bus.execute( "automation.putValue", QJsonObject{
			{ "track", 1 }, { "clip", 0 }, { "pos", automationPosition }, { "value", automationValue }
		} );
		QVERIFY( putValue.ok );
		QVERIFY( bus.execute( "automation.setProgression", QJsonObject{
			{ "track", 1 }, { "clip", 0 }, { "type", "CubicHermite" }
		} ).ok );
		QVERIFY( bus.execute( "automation.setTension", QJsonObject{
			{ "track", 1 }, { "clip", 0 }, { "value", 0.6 }
		} ).ok );

		auto *sourceTrack = dynamic_cast<lmms::InstrumentTrack *>( song->tracks().at( 0 ) );
		auto *targetTrack = dynamic_cast<lmms::AutomationTrack *>( song->tracks().at( 1 ) );
		QVERIFY( sourceTrack != nullptr );
		QVERIFY( targetTrack != nullptr );
		auto *storedClip = dynamic_cast<lmms::AutomationClip *>( targetTrack->getClips().at( 0 ) );
		QVERIFY( storedClip != nullptr );
		const float expectedStoredValue = sourceTrack->volumeModel()->inverseScaledValue( automationValue );
		QVERIFY( qAbs( storedClip->valueAt( lmms::TimePos( automationPosition ) ) - expectedStoredValue ) < 0.001f );
		QVERIFY( qAbs( putValue.data.value( "valueAt" ).toDouble() - expectedStoredValue ) < 0.001 );
		QVERIFY( bus.execute( "automation.putValues", QJsonObject{
			{ "track", 1 }, { "clip", 0 }, { "nodes", QJsonArray{ QJsonObject{
				{ "pos", automationPosition + 1 }, { "inValue", 60.0 }, { "outValue", 65.0 }
			} } }
		} ).ok );
		QVERIFY( bus.execute( "automation.removeNode", QJsonObject{
			{ "track", 1 }, { "clip", 0 }, { "pos", automationPosition + 1 }
		} ).ok );
		const auto targets = bus.execute( "automation.listTargets", QJsonObject{
			{ "scope", "track" }
		} );
		QVERIFY( targets.ok );
		QVERIFY( !targets.data.value( "models" ).toArray().isEmpty() );

		QVERIFY( bus.execute( "mixer.removeSend", QJsonObject{
			{ "from", mixerChannel }, { "to", 0 }
		} ).ok );
		QVERIFY( bus.execute( "mixer.addSend", QJsonObject{
			{ "from", mixerChannel }, { "to", 0 }, { "amount", 0.5 }
		} ).ok );
		QVERIFY( bus.execute( "mixer.clearChannel", QJsonObject{ { "channel", mixerChannel } } ).ok );
		QVERIFY( lmms::Engine::projectJournal()->canUndo() );
		QVERIFY( bus.execute( "history.undo" ).ok );
		QVERIFY( bus.execute( "history.redo" ).ok );

		const auto availableEffects = bus.execute( "effect.listAvailable" );
		QVERIFY( availableEffects.ok );
		auto *effectChain = &mixer->mixerChannel( 0 )->m_fxChain;
		effectChain->appendEffect( new AgentTestEffect( effectChain ) );
		QVERIFY( bus.execute( "effect.setEnabled", QJsonObject{
			{ "owner", "channel:0" }, { "slot", 0 }, { "value", false }
		} ).ok );
		QVERIFY( bus.execute( "effect.setWetDry", QJsonObject{
			{ "owner", "channel:0" }, { "slot", 0 }, { "value", -0.25 }
		} ).ok );
		const auto effectParameters = bus.execute( "effect.getParams", QJsonObject{
			{ "owner", "channel:0" }, { "slot", 0 }
		} );
		QVERIFY( effectParameters.ok );
		QVERIFY( !parameterValue( effectParameters.data.value( "parameters" ).toArray(), "volume" ).isUndefined() );
		const auto setEffectParameter = bus.execute( "effect.setParam", QJsonObject{
			{ "owner", "channel:0" }, { "slot", 0 }, { "name", "volume" }, { "value", 125.0 }
		} );
		QVERIFY( setEffectParameter.ok );
		const auto reloadedEffectParameters = bus.execute( "effect.getParams", QJsonObject{
			{ "owner", "channel:0" }, { "slot", 0 }
		} );
		QVERIFY( reloadedEffectParameters.ok );
		QVERIFY( qAbs( parameterValue( reloadedEffectParameters.data.value( "parameters" ).toArray(), "volume" )
			.toDouble() - 125.0 ) < 0.001 );
	}
};

QTEST_GUILESS_MAIN( MixAutomationCommandsTest )
#include "MixAutomationCommandsTest.moc"
