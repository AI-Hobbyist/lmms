#include <QtTest>

#include <QJsonArray>

#include "agent/CommandBus.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"

namespace
{

constexpr int DrumTempo = 128;
constexpr int KickKey = 36;

QJsonArray fourOnTheFloorNotes()
{
	QJsonArray notes;
	const int ticksPerBar = lmms::TimePos::ticksPerBar();
	const int beatLength = ticksPerBar / 4;
	const int noteLength = ticksPerBar / 8;

	for( int bar = 0; bar < 8; ++bar )
	{
		for( int beat = 0; beat < 4; ++beat )
		{
			notes.append( QJsonObject{
				{ "position", bar * ticksPerBar + beat * beatLength },
				{ "length", noteLength },
				{ "key", KickKey },
				{ "volume", 100 },
				{ "panning", 0 }
			} );
		}
	}

	return notes;
}

} // namespace

class CoreCommandsTest : public QObject
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

	void createsEightBarDrumPatternAndQueriesIt()
	{
		auto &bus = lmms::agent::CommandBus::instance();
		auto *song = lmms::Engine::getSong();
		auto *journal = lmms::Engine::projectJournal();
		const int ticksPerBar = lmms::TimePos::ticksPerBar();

		QVERIFY( song->tracks().empty() );
		QVERIFY( !journal->canUndo() );
		QVERIFY( bus.beginBatch( "eight-bar drum pattern" ) );

		QVERIFY( bus.execute( "song.setTempo", QJsonObject{ { "bpm", DrumTempo } } ).ok );

		const auto createdTrack = bus.execute( "track.create", QJsonObject{
			{ "type", "instrument" },
			{ "name", "Drums" }
		} );
		QVERIFY( createdTrack.ok );
		QCOMPARE( createdTrack.data.value( "path" ).toString(), QString( "song/track:0" ) );
		QCOMPARE( createdTrack.data.value( "name" ).toString(), QString( "Drums" ) );

		const auto parameters = bus.execute( "instrument.setParameters", QJsonObject{
			{ "track", 0 },
			{ "volume", 90.0 },
			{ "panning", 0.0 },
			{ "baseNote", KickKey }
		} );
		QVERIFY( parameters.ok );
		QCOMPARE( parameters.data.value( "parameters" ).toObject().value( "baseNote" ).toInt(), KickKey );

		const auto createdClip = bus.execute( "clip.create", QJsonObject{
			{ "track", 0 },
			{ "type", "midi" },
			{ "start", 0 },
			{ "length", 8 * ticksPerBar },
			{ "name", "Kick Pattern" }
		} );
		QVERIFY( createdClip.ok );
		QCOMPARE( createdClip.data.value( "path" ).toString(), QString( "song/track:0/clip:0" ) );
		QCOMPARE( createdClip.data.value( "length" ).toInt(), 8 * ticksPerBar );

		const auto addedNotes = bus.execute( "midi.addNotes", QJsonObject{
			{ "track", 0 },
			{ "clip", 0 },
			{ "notes", fourOnTheFloorNotes() }
		} );
		QVERIFY( addedNotes.ok );
		QCOMPARE( addedNotes.data.value( "added" ).toInt(), 32 );
		QCOMPARE( addedNotes.data.value( "noteCount" ).toInt(), 32 );
		QVERIFY( bus.endBatch( true ).ok );
		QVERIFY( journal->canUndo() );

		const auto summary = bus.execute( "query.songSummary" );
		QVERIFY( summary.ok );
		QCOMPARE( summary.data.value( "tempo" ).toInt(), DrumTempo );
		QCOMPARE( summary.data.value( "trackCount" ).toInt(), 1 );
		QCOMPARE( summary.data.value( "lengthBars" ).toInt(), 8 );

		const auto track = bus.execute( "query.trackDetail", QJsonObject{ { "track", 0 } } );
		QVERIFY( track.ok );
		QCOMPARE( track.data.value( "path" ).toString(), QString( "song/track:0" ) );
		QCOMPARE( track.data.value( "name" ).toString(), QString( "Drums" ) );
		QCOMPARE( track.data.value( "clipCount" ).toInt(), 1 );
		QCOMPARE( track.data.value( "instrumentParameters" ).toObject().value( "volume" ).toDouble(), 90.0 );

		const auto clip = bus.execute( "query.clipDetail", QJsonObject{
			{ "track", 0 },
			{ "clip", 0 }
		} );
		QVERIFY( clip.ok );
		QCOMPARE( clip.data.value( "path" ).toString(), QString( "song/track:0/clip:0" ) );
		QCOMPARE( clip.data.value( "length" ).toInt(), 8 * ticksPerBar );
		QCOMPARE( clip.data.value( "noteCount" ).toInt(), 32 );

		const auto notes = bus.execute( "query.notes", QJsonObject{
			{ "track", 0 },
			{ "clip", 0 }
		} );
		QVERIFY( notes.ok );
		const auto noteList = notes.data.value( "notes" ).toArray();
		QCOMPARE( noteList.size(), 32 );
		QCOMPARE( noteList.first().toObject().value( "position" ).toInt(), 0 );
		QCOMPARE( noteList.last().toObject().value( "position" ).toInt(), 7 * ticksPerBar + 3 * ticksPerBar / 4 );
		QCOMPARE( noteList.last().toObject().value( "key" ).toInt(), KickKey );

		const int clipsBeforeFailedQuery = static_cast<int>( song->tracks().at( 0 )->getClips().size() );
		const auto missingClip = bus.execute( "query.clipDetail", QJsonObject{
			{ "track", 0 },
			{ "clip", 1 }
		} );
		QVERIFY( !missingClip.ok );
		QCOMPARE( missingClip.errorCode, QString( "clip_not_found" ) );
		QCOMPARE( static_cast<int>( song->tracks().at( 0 )->getClips().size() ), clipsBeforeFailedQuery );

		const auto invalidNote = bus.execute( "midi.addNotes", QJsonObject{
			{ "track", 0 },
			{ "clip", 0 },
			{ "notes", QJsonArray{ QJsonObject{
				{ "position", -1 },
				{ "length", ticksPerBar / 8 },
				{ "key", KickKey }
			} } }
		} );
		QVERIFY( !invalidNote.ok );
		QCOMPARE( invalidNote.errorCode, QString( "invalid_arguments" ) );
		QCOMPARE( bus.execute( "query.notes", QJsonObject{ { "track", 0 }, { "clip", 0 } } )
			.data.value( "notes" ).toArray().size(), 32 );

		const auto seek = bus.execute( "transport.seek", QJsonObject{ { "ticks", ticksPerBar } } );
		QVERIFY( seek.ok );
		QCOMPARE( seek.data.value( "mode" ).toString(), QString( "song" ) );
		QCOMPARE( seek.data.value( "ticks" ).toInt(), ticksPerBar );
		QVERIFY( journal->canUndo() );

		QVERIFY( bus.execute( "history.undo" ).ok );
		const auto afterUndo = bus.execute( "query.songSummary" );
		QVERIFY( afterUndo.ok );
		QCOMPARE( afterUndo.data.value( "trackCount" ).toInt(), 0 );

		QVERIFY( bus.execute( "history.redo" ).ok );
		const auto afterRedo = bus.execute( "query.songSummary" );
		QVERIFY( afterRedo.ok );
		QCOMPARE( afterRedo.data.value( "tempo" ).toInt(), DrumTempo );
		QCOMPARE( afterRedo.data.value( "trackCount" ).toInt(), 1 );
		QCOMPARE( bus.execute( "query.notes", QJsonObject{ { "track", 0 }, { "clip", 0 } } )
			.data.value( "notes" ).toArray().size(), 32 );
	}
};

QTEST_GUILESS_MAIN( CoreCommandsTest )
#include "CoreCommandsTest.moc"