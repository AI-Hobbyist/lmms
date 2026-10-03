#include <QtTest>

#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>

#include "agent/CommandBus.h"
#include "Engine.h"
#include "MidiClip.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "Timeline.h"

namespace
{

QString writeTestWave( QTemporaryDir &directory )
{
	const QString path = directory.filePath( "test.wav" );
	QFile file( path );
	if( !file.open( QIODevice::WriteOnly ) )
	{
		return {};
	}

	constexpr quint32 sampleRate = 44100;
	constexpr quint32 frames = sampleRate;
	constexpr quint16 channels = 1;
	constexpr quint16 bitsPerSample = 16;
	constexpr quint32 dataBytes = frames * channels * bitsPerSample / 8;
	QDataStream stream( &file );
	stream.setByteOrder( QDataStream::LittleEndian );
	file.write( "RIFF", 4 );
	stream << quint32( 36 + dataBytes );
	file.write( "WAVEfmt ", 8 );
	stream << quint32( 16 ) << quint16( 1 ) << channels << sampleRate;
	stream << quint32( sampleRate * channels * bitsPerSample / 8 );
	stream << quint16( channels * bitsPerSample / 8 ) << bitsPerSample;
	file.write( "data", 4 );
	stream << dataBytes;
	file.write( QByteArray( static_cast<int>( dataBytes ), '\0' ) );
	return path;
}

} // namespace

class A3CommandsTest : public QObject
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

	void persistsSongLoopRangeWithoutGui()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		auto *song = lmms::Engine::getSong();
		const int ticksPerBar = song->ticksPerBar();
		auto &timeline = song->getTimeline( lmms::Song::PlayMode::Song );
		timeline.setLoopPoints( lmms::TimePos( 0 ), lmms::TimePos( 0 ) );
		timeline.setLoopEnabled( false );

		const auto dryRun = bus.execute( "transport.setLoopRange", QJsonObject{
			{ "startBar", 2 }, { "endBar", 4 }, { "dryRun", true }
		} );
		QVERIFY( dryRun.ok );
		QCOMPARE( dryRun.data.value( "start" ).toInt(), 2 * ticksPerBar );
		QVERIFY( !timeline.loopEnabled() );

		const auto range = bus.execute( "transport.setLoopRange", QJsonObject{
			{ "startBar", 1 }, { "endBar", 3 }
		} );
		QVERIFY( range.ok );
		QCOMPARE( range.data.value( "start" ).toInt(), ticksPerBar );
		QCOMPARE( range.data.value( "end" ).toInt(), 3 * ticksPerBar );
		QVERIFY( range.data.value( "enabled" ).toBool() );

		QCOMPARE( timeline.loopBegin().getTicks(), ticksPerBar );
		QCOMPARE( timeline.loopEnd().getTicks(), 3 * ticksPerBar );
		QVERIFY( timeline.loopEnabled() );

		QVERIFY( bus.execute( "history.undo" ).ok );
		QVERIFY( !timeline.loopEnabled() );
		QVERIFY( bus.execute( "history.redo" ).ok );
		QCOMPARE( timeline.loopBegin().getTicks(), ticksPerBar );
		QCOMPARE( timeline.loopEnd().getTicks(), 3 * ticksPerBar );
		QVERIFY( timeline.loopEnabled() );

		QTemporaryDir temporaryDirectory;
		QVERIFY( temporaryDirectory.isValid() );
		const QString projectPath = temporaryDirectory.filePath( "loop-range.mmp" );
		QVERIFY( song->saveProjectFile( projectPath, false ) );

		timeline.setLoopPoints( lmms::TimePos( 0 ), lmms::TimePos( 0 ) );
		timeline.setLoopEnabled( false );
		song->loadProject( projectPath );

		QCOMPARE( timeline.loopBegin().getTicks(), ticksPerBar );
		QCOMPARE( timeline.loopEnd().getTicks(), 3 * ticksPerBar );
		QVERIFY( timeline.loopEnabled() );
	}

	void supportsMidiSampleAndPreviewCommands()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		auto *song = lmms::Engine::getSong();

		QVERIFY( bus.execute( "track.create", QJsonObject{ { "type", "instrument" } } ).ok );
		QVERIFY( bus.execute( "clip.create", QJsonObject{
			{ "track", 0 }, { "type", "midi" }, { "length", song->ticksPerBar() }
		} ).ok );
		QVERIFY( bus.execute( "midi.addNotes", QJsonObject{
			{ "track", 0 }, { "clip", 0 }, { "notes", QJsonArray{ QJsonObject{
				{ "position", 24 }, { "length", 48 }, { "key", 60 }, { "volume", 100 }
			} } }
		} ).ok );

		const auto setSteps = bus.execute( "midi.setSteps", QJsonObject{
			{ "track", 0 }, { "clip", 0 }, { "steps", 32 }
		} );
		QVERIFY( setSteps.ok );
		QCOMPARE( setSteps.data.value( "steps" ).toInt(), 32 );

		const auto setBeat = bus.execute( "midi.setClipType", QJsonObject{
			{ "track", 0 }, { "clip", 0 }, { "type", "beat" }
		} );
		QVERIFY( setBeat.ok );
		QCOMPARE( setBeat.data.value( "type" ).toString(), QString( "beat" ) );

		auto *midiClip = dynamic_cast<lmms::MidiClip *>( song->tracks().at( 0 )->getClips().at( 0 ) );
		QVERIFY( midiClip != nullptr );
		QCOMPARE( midiClip->type(), lmms::MidiClip::Type::BeatClip );
		QCOMPARE( midiClip->steps(), 32 );

		QVERIFY( bus.execute( "midi.setClipType", QJsonObject{
			{ "track", 0 }, { "clip", 0 }, { "type", "melody" }
		} ).ok );
		const auto humanize = bus.execute( "midi.humanize", QJsonObject{
			{ "track", 0 }, { "clip", 0 }, { "timing", 6 }, { "velocity", 8 },
			{ "detune", 0.5 }, { "seed", 17 }
		} );
		QVERIFY( humanize.ok );
		QCOMPARE( humanize.data.value( "seed" ).toInt(), 17 );
		QVERIFY( midiClip->notes().front()->hasDetuningInfo() );

		const auto preview = bus.execute( "transport.previewClip", QJsonObject{
			{ "track", 0 }, { "clip", 0 }, { "loop", false }
		} );
		QVERIFY( preview.ok );
		QCOMPARE( preview.data.value( "mode" ).toString(), QString( "midiClip" ) );
		QVERIFY( !preview.data.value( "loop" ).toBool() );
		song->stop();

		QVERIFY( bus.execute( "track.create", QJsonObject{ { "type", "sample" } } ).ok );
		const auto sampleClip = bus.execute( "clip.create", QJsonObject{
			{ "track", 1 }, { "type", "sample" }, { "length", song->ticksPerBar() }
		} );
		QVERIFY( sampleClip.ok );
		QCOMPARE( sampleClip.data.value( "type" ).toString(), QString( "sample" ) );

		QTemporaryDir temporaryDirectory;
		QVERIFY( temporaryDirectory.isValid() );
		const QString samplePath = writeTestWave( temporaryDirectory );
		QVERIFY( !samplePath.isEmpty() );
		const auto loaded = bus.execute( "sample.setFile", QJsonObject{
			{ "track", 1 }, { "clip", 0 }, { "path", samplePath }
		} );
		QVERIFY( loaded.ok );
		QCOMPARE( loaded.data.value( "sampleRate" ).toInt(), 44100 );
		QVERIFY( loaded.data.value( "frames" ).toDouble() > 0.0 );

		const auto reversed = bus.execute( "sample.setReversed", QJsonObject{
			{ "track", 1 }, { "clip", 0 }, { "value", true }
		} );
		QVERIFY( reversed.ok );
		QVERIFY( reversed.data.value( "reversed" ).toBool() );
		const auto offset = bus.execute( "sample.setOffset", QJsonObject{
			{ "track", 1 }, { "clip", 0 }, { "value", 1 }
		} );
		QVERIFY( offset.ok );
		QCOMPARE( offset.data.value( "offsetTicks" ).toInt(), 1 );

		const auto info = bus.execute( "sample.getInfo", QJsonObject{ { "track", 1 }, { "clip", 0 } } );
		QVERIFY( info.ok );
		QVERIFY( info.data.value( "reversed" ).toBool() );
		QCOMPARE( info.data.value( "offsetTicks" ).toInt(), 1 );
	}

	void supportsSongLifecycleCommands()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		auto *song = lmms::Engine::getSong();

		QVERIFY( bus.execute( "track.create", QJsonObject{ { "type", "instrument" } } ).ok );
		QVERIFY( bus.execute( "song.setTempo", QJsonObject{ { "bpm", 133 } } ).ok );

		QTemporaryDir temporaryDirectory;
		QVERIFY( temporaryDirectory.isValid() );
		const QString projectPath = temporaryDirectory.filePath( "lifecycle.mmp" );
		const auto save = bus.execute( "song.save", QJsonObject{ { "path", projectPath } } );
		QVERIFY( save.ok );
		QVERIFY( QFile::exists( projectPath ) );
		QCOMPARE( song->projectFileName(), projectPath );

		QVERIFY( bus.execute( "song.setTempo", QJsonObject{ { "bpm", 144 } } ).ok );
		const auto dryRunLoad = bus.execute( "song.load", QJsonObject{
			{ "path", projectPath }, { "dryRun", true }
		} );
		QVERIFY( dryRunLoad.ok );
		QCOMPARE( song->getTempo(), 144 );

		const auto load = bus.execute( "song.load", QJsonObject{ { "path", projectPath } } );
		QVERIFY( load.ok );
		QCOMPARE( song->getTempo(), 133 );
		QCOMPARE( static_cast<int>( song->tracks().size() ), 1 );
		QVERIFY( bus.execute( "history.undo" ).ok );
		QCOMPARE( song->getTempo(), 144 );
		QVERIFY( bus.execute( "history.redo" ).ok );
		QCOMPARE( song->getTempo(), 133 );

		const auto clear = bus.execute( "song.clearProject" );
		QVERIFY( clear.ok );
		QCOMPARE( clear.data.value( "trackCount" ).toInt(), 0 );
		QVERIFY( bus.execute( "history.undo" ).ok );
		QCOMPARE( static_cast<int>( song->tracks().size() ), 1 );
	}
};

QTEST_GUILESS_MAIN( A3CommandsTest )
#include "A3CommandsTest.moc"