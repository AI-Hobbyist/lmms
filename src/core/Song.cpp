/*
 * Song.cpp - root of the model tree
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "Song.h"
#ifdef LMMS_BUILD_WIN32
#include "WindowsCrashReport.h"
#endif
#include "AudioBusHandle.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Controller.h"
#include "vsthost/RealtimeMidiQueue.h"
#include "vsthost/VstNodePorts.h"
#include "NodeWorkflowSourceGraph.h"
#include "NodeWorkflowInputs.h"
#ifdef WANT_AGENT
#include "agent/CommandBus.h"
#endif
#include <QTextStream>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QEvent>
#include <QMessageBox>
#include <QPointer>
#include <QScopedValueRollback>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "AutomationTrack.h"
#include "AutomationEditor.h"
#include "ConfigManager.h"
#include "ControllerRackView.h"
#include "ControllerConnection.h"
#include "EnvelopeAndLfoParameters.h"
#include "MidiController.h"
#include "Mixer.h"
#include "MidiEvent.h"
#include "MixerView.h"
#include "GuiApplication.h"
#include "ExportFilter.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Keymap.h"
#include "NotePlayHandle.h"
#include "MidiClip.h"
#include "SVSClip.h"
#include "PatternEditor.h"
#include "PatternStore.h"
#include "SampleTrack.h"
#include "SVCTrack.h"
#include "SVSTrack.h"
#include "PatternTrack.h"
#include "PianoRoll.h"
#include "ProjectJournal.h"
#include "ProjectNotes.h"
#include "Scale.h"
#include "SongEditor.h"
#include "PeakController.h"


namespace lmms
{
namespace
{
void setProjectWorkspace(bool& isNodeProject, bool node)
{
	isNodeProject = node;
#ifdef LMMS_BUILD_WIN32
	windowsCrashReport::setNodeWorkspace(node);
#endif
}
}

tick_t TimePos::s_ticksPerBar = DefaultTicksPerBar;



Song::Song() :
	TrackContainer(),
	m_globalAutomationTrack( dynamic_cast<AutomationTrack *>(
				Track::create( Track::Type::HiddenAutomation,
								this ) ) ),
	m_tempoModel( DefaultTempo, MinTempo, MaxTempo, this, tr( "Tempo" ) ),
	m_timeSigModel( this ),
	m_oldTicksPerBar( DefaultTicksPerBar ),
	m_masterVolumeModel( 100, 0, 200, this, tr( "Master volume" ) ),
	m_masterPitchModel( 0, -12, 12, this, tr( "Master pitch" ) ),
	m_nLoadingTrack( 0 ),
	m_fileName(),
	m_oldFileName(),
	m_modified( false ),
	m_loadOnLaunch( true ),
	m_recording( false ),
	m_exporting( false ),
	m_exportLoop( false ),
	m_renderBetweenMarkers( false ),
	m_playing( false ),
	m_paused( false ),
	m_savingProject( false ),
	m_loadingProject( false ),
	m_isCancelled( false ),
	m_playMode( PlayMode::None ),
	m_length( 0 ),
	m_noteClipToPlay( nullptr ),
	m_loopMidiClip( false ),
	m_loopRenderCount(1),
	m_loopRenderRemaining(1),
	m_oldAutomatedValues()
{
	connect( &m_tempoModel, SIGNAL(dataChanged()),
			this, SLOT(setTempo()), Qt::DirectConnection );
	connect( &m_tempoModel, SIGNAL(dataUnchanged()),
			this, SLOT(setTempo()), Qt::DirectConnection );
	connect( &m_timeSigModel, SIGNAL(dataChanged()),
			this, SLOT(setTimeSignature()), Qt::DirectConnection );


	connect( Engine::audioEngine(), SIGNAL(sampleRateChanged()), this,
						SLOT(updateFramesPerTick()));

	connect( &m_masterVolumeModel, SIGNAL(dataChanged()),
			this, SLOT(masterVolumeChanged()), Qt::DirectConnection );
/*	connect( &m_masterPitchModel, SIGNAL(dataChanged()),
			this, SLOT(masterPitchChanged()));*/

	qRegisterMetaType<lmms::Note>( "lmms::Note" );
	setType( Type::Song );

	for (auto& scale : m_scales) {scale = std::make_shared<Scale>();}
	for (auto& keymap : m_keymaps) {keymap = std::make_shared<Keymap>();}

	// Aggregate the `positionJumped` signals from all the timelines into a single `playbackPositionJumped` signal for
	// other objects (sample tracks, LFOs, etc) to use.
	for (auto i = std::size_t{0}; i < static_cast<std::size_t>(PlayMode::Count); ++i)
	{
		const auto onPositionJumped = [this, playMode = static_cast<PlayMode>(i)] {
			// Only emit the signal when the song is actually playing and the active timeline jumps
			// This prevents LFOs from changing phase when the user drags the timeline while paused
			if (m_playing && m_playMode == playMode) { emit playbackPositionJumped(); }
		};

		connect(&m_timelines[i], &Timeline::positionJumped, this, onPositionJumped);
	}

	// Inform VST plugins if the user moved the play head
	connect(this, &Song::playbackPositionJumped, this, [this](){ m_vstSyncController.setPlaybackJumped(true); }, Qt::DirectConnection);
}




Song::~Song()
{
	m_playing = false;
	for (auto owner = m_nodeInstrumentLayers.cbegin(); owner != m_nodeInstrumentLayers.cend(); ++owner)
	{
		disconnect(owner.key(), nullptr, this, nullptr);
	}
	m_nodeInstrumentLayers.clear();
	m_nodeCustomInstruments.clear();
	m_nodeInstrumentTracks.reset();
	delete m_globalAutomationTrack;
}




auto Song::getTimeline(PlayMode mode) -> Timeline&
{
	return m_timelines[static_cast<std::size_t>(mode)];
}


auto Song::getTimeline(PlayMode mode) const -> const Timeline&
{
	return m_timelines[static_cast<std::size_t>(mode)];
}


void Song::masterVolumeChanged()
{
	Engine::audioEngine()->setMasterGain( m_masterVolumeModel.value() / 100.0f );
}




void Song::setTempo()
{
	Engine::audioEngine()->requestChangeInModel();
	const auto tempo = (bpm_t)m_tempoModel.value();
	PlayHandleList & playHandles = Engine::audioEngine()->playHandles();
	for (const auto& playHandle : playHandles)
	{
		auto nph = dynamic_cast<NotePlayHandle*>(playHandle);
		if( nph && !nph->isReleased() )
		{
			nph->lock();
			nph->resize( tempo );
			nph->unlock();
		}
	}
	Engine::audioEngine()->doneChangeInModel();

	Engine::updateFramesPerTick();

	m_vstSyncController.setTempo( tempo );

	emit tempoChanged( tempo );
}




void Song::setTimeSignature()
{
	TimePos::setTicksPerBar( ticksPerBar() );
	emit timeSignatureChanged( m_oldTicksPerBar, ticksPerBar() );
	emit dataChanged();
	m_oldTicksPerBar = ticksPerBar();

	m_vstSyncController.setTimeSignature(
		getTimeSigModel().getNumerator(), getTimeSigModel().getDenominator() );
}




void Song::savePlayStartPosition()
{
	getTimeline().setPlayStartPosition(getPlayPos());
}




void Song::processNextBuffer()
{
	// If nothing is playing, there is nothing to do
	if (!m_playing) { return; }

	// At the beginning of the song, we have to reset the LFOs
	if (m_playMode == PlayMode::Song && getPlayPos() == 0)
	{
		EnvelopeAndLfoParameters::instances()->reset();
	}

	TrackList trackList;
	int clipNum = -1; // The number of the clip that will be played

	// Determine the list of tracks to play and the clip number
	switch (m_playMode)
	{
		case PlayMode::Song:
			trackList = tracks();
			break;

		case PlayMode::Pattern:
			if (Engine::patternStore()->numOfPatterns() > 0)
			{
				clipNum = Engine::patternStore()->currentPattern();
				trackList.push_back(PatternTrack::findPatternTrack(clipNum));
			}
			break;

		case PlayMode::MidiClip:
			if (m_noteClipToPlay)
			{
				clipNum = m_noteClipToPlay->getTrack()->getClipNum(m_noteClipToPlay);
				trackList.push_back(m_noteClipToPlay->getTrack());
			}
			break;
		case PlayMode::AutomationClip:
			if (m_automationClipToPlay)
			{
				clipNum = m_automationClipToPlay->getTrack()->getClipNum(m_automationClipToPlay);
				trackList.push_back(m_automationClipToPlay->getTrack());
			}
			break;

		default:
			return;
	}

	auto& timeline = getTimeline();

	// If the playback position is outside of the range [begin, end), move it to
	// begin and inform interested parties.
	// Returns true if the playback position was moved, else false.
	const auto enforceLoop = [this](const TimePos& begin, const TimePos& end)
	{
		if (getPlayPos() < begin || getPlayPos() >= end)
		{
			getTimeline().setTicks(begin.getTicks());
			return true;
		}
		return false;
	};

	const auto loopEnabled = !m_exporting && timeline.loopEnabled();

	// Ensure playback begins within the loop if it is enabled
	if (loopEnabled) { enforceLoop(timeline.loopBegin(), timeline.loopEnd()); }

	const auto framesPerTick = Engine::framesPerTick();
	const auto framesPerPeriod = Engine::audioEngine()->framesPerPeriod();

	f_cnt_t frameOffsetInPeriod = 0;

	while (frameOffsetInPeriod < framesPerPeriod)
	{
		auto frameOffsetInTick = timeline.frameOffset();

		// If a whole tick has elapsed, update the frame and tick count, and check any loops
		if (frameOffsetInTick >= framesPerTick)
		{
			// Transfer any whole ticks from the frame count to the tick count
			const auto elapsedTicks = static_cast<int>(frameOffsetInTick / framesPerTick);
			frameOffsetInTick -= elapsedTicks * framesPerTick;
			timeline.incrementTicks(elapsedTicks);
			timeline.setFrameOffset(frameOffsetInTick);

			// If we are playing a pattern track, or a MIDI clip with no loop enabled,
			// loop back to the beginning when we reach the end
			if (m_playMode == PlayMode::Pattern)
			{
				enforceLoop(TimePos{0}, TimePos{Engine::patternStore()->lengthOfCurrentPattern(), 0});
			}
			else if ((m_playMode == PlayMode::MidiClip || m_playMode == PlayMode::AutomationClip) && previewClip()
				&& m_loopMidiClip && !loopEnabled)
			{
				enforceLoop(
					-previewClip()->startTimeOffset(), previewClip()->length() - previewClip()->startTimeOffset());
			}

			// Handle loop points, and inform VST plugins of the loop status
			if (loopEnabled || (m_loopRenderRemaining > 1 && getPlayPos() >= timeline.loopBegin()))
			{
				m_vstSyncController.startCycle(
					timeline.loopBegin().getTicks(), timeline.loopEnd().getTicks());

				// Loop if necessary, and decrement the remaining loops if we did
				if (enforceLoop(timeline.loopBegin(), timeline.loopEnd())
					&& m_loopRenderRemaining > 1)
				{
					m_loopRenderRemaining--;
				}
			}
			else
			{
				m_vstSyncController.stopCycle();
			}
		}

		const f_cnt_t framesUntilNextPeriod = framesPerPeriod - frameOffsetInPeriod;
		const auto framesUntilNextTick = static_cast<f_cnt_t>(std::ceil(framesPerTick - frameOffsetInTick));

		// We want to proceed to the next buffer or tick, whichever is closer
		const auto framesToPlay = std::min(framesUntilNextPeriod, framesUntilNextTick);

		if (frameOffsetInPeriod == 0)
		{
			// First frame of buffer: update VST sync position.
			// This must be done after we've corrected the frame/tick count,
			// but before actually playing any frames.
			m_vstSyncController.setAbsolutePosition(getPlayPos().getTicks()
				+ timeline.frameOffset() / static_cast<double>(framesPerTick));
			m_vstSyncController.update();
		}

		if (static_cast<f_cnt_t>(frameOffsetInTick) == 0)
		{
			// First frame of tick: process automation and play tracks
			processAutomations(trackList, getPlayPos(), framesToPlay);
			processMetronome(frameOffsetInPeriod);

			for (const auto track : trackList)
			{
				track->play(getPlayPos(), framesToPlay, frameOffsetInPeriod, clipNum);
			}
		}

		// Update frame counters
		frameOffsetInPeriod += framesToPlay;
		frameOffsetInTick += framesToPlay;
		timeline.setFrameOffset(frameOffsetInTick);
	}

	// Reset the jumped state after processing this buffer, since presumably it has now been handled.
	m_vstSyncController.setPlaybackJumped(false);
}


void Song::processAutomations(const TrackList &tracklist, TimePos timeStart, f_cnt_t)
{
	AutomatedValueMap values;

	QSet<const AutomatableModel*> recordedModels;

	TrackContainer* container = this;
	int clipNum = -1;

	switch (m_playMode)
	{
	case PlayMode::Song:
		break;
	case PlayMode::Pattern:
	{
		if (tracklist.empty()) { return; }
		Q_ASSERT(tracklist.at(0)->type() == Track::Type::Pattern);
		auto patternTrack = dynamic_cast<PatternTrack*>(tracklist.at(0));
		container = Engine::patternStore();
		clipNum = patternTrack->patternIndex();
	}
		break;
	case PlayMode::AutomationClip:
			if (!m_automationClipToPlay)
			{
				return;
			}
			break;
	default:
		return;
	}

	if (m_playMode == PlayMode::AutomationClip)
		{
			values = automatedValuesFromTracks(tracklist, timeStart + m_automationClipToPlay->startPosition(),
				m_automationClipToPlay->getTrack()->getClipNum(m_automationClipToPlay));
		}
		else
		{
			values = container->automatedValuesAt(timeStart, clipNum);
		}
	const TrackList& tracks = container->tracks();

	Track::clipVector clips;
	TimePos recordingTime = timeStart;
	if (m_playMode == PlayMode::AutomationClip)
	{
		clips.push_back(const_cast<AutomationClip*>(m_automationClipToPlay));
		recordingTime += m_automationClipToPlay->startPosition();
	}
	else
		for (Track* track : tracks)
	{
		if (track->type() == Track::Type::Automation) {
			track->getClipsInRange(clips, 0, timeStart);
		}
	}

	// Process recording
	for (Clip* clip : clips)
	{
		auto p = dynamic_cast<AutomationClip *>(clip);
		TimePos relTime = recordingTime - p->startPosition();
		if (p->isRecording() && relTime >= 0 && relTime < p->length())
		{
			const AutomatableModel* recordedModel = p->firstObject();
			// The automation system really needs to be reworked.
			// For whatever reason, the values in an automation clip are stored in un-un-scaled format, so if you
			// are automating a log knob, when you draw an curve, the values being stored are not the actual values the
			// knob will take, but instead the unscaled version of the unscaled numbers. The tooltip shows the number you expect, but if you double-click,
			// you can see that the true values are stored by their inverse scaled value....which is wrong, since they weren't scaled in the first place...?
			// Anyhow, in the meantime before we redo the automation system, when recording automations, we have to get the inverseScaledValue
			// and store that so that when playing it back, it scales the value correctly.
			p->recordValue(relTime, recordedModel->inverseScaledValue(recordedModel->value<float>()));

			recordedModels << recordedModel;
		}
	}

	// Checks if an automated model stopped being automated by automation clip
	// so we can move the control back to any connected controller again
	for (auto it = m_oldAutomatedValues.begin(); it != m_oldAutomatedValues.end(); it++)
	{
		AutomatableModel * am = it.key();
		if (!values.contains(am))
		{
			am->setUseControllerValue(true);
		}
	}
	m_oldAutomatedValues = values;

	// Apply values
	for (auto it = values.begin(); it != values.end(); it++)
	{
		AutomatableModel* model = it.key();
		bool isRecording = recordedModels.contains(model);
		model->setUseControllerValue(isRecording);

		if (!isRecording)
		{
			/* TODO
			 * Remove scaleValue() from here when automation editor's
			 * Y axis can be set to logarithmic, and automation clips store
			 * the actual values, and not the invertedScaledValue.
			 */
			model->setValue(model->scaledValue(it.value()), true);
		}
	}
}

void Song::processMetronome(size_t bufferOffset)
{
	const auto currentPlayMode = playMode();
	const auto supported = currentPlayMode == PlayMode::MidiClip
		|| currentPlayMode == PlayMode::Song
		|| currentPlayMode == PlayMode::Pattern;

	if (!supported || m_exporting)
	{
		return;
	}
	m_metronome.processTick(currentTick(), ticksPerBar(), m_timeSigModel.getNumerator(), bufferOffset);
}

void Song::setModified(bool value)
{
	if( !m_loadingProject && m_modified != value)
	{
		m_modified = value;
		emit modified();
	}
}

bool Song::isExportDone() const
{
	return !isExporting() || getPlayPos() >= m_exportSongEnd;
}

int Song::getExportProgress() const
{
	TimePos pos = getPlayPos();

	if (pos >= m_exportSongEnd)
	{
		return 100;
	}
	else if (pos <= m_exportSongBegin)
	{
		return 0;
	}
	else if (pos >= m_exportLoopEnd)
	{
		pos = (m_exportLoopBegin-m_exportSongBegin) + (m_exportLoopEnd - m_exportLoopBegin) *
			m_loopRenderCount + (pos - m_exportLoopEnd);
	}
	else if ( pos >= m_exportLoopBegin )
	{
		pos = (m_exportLoopBegin-m_exportSongBegin) + ((m_exportLoopEnd - m_exportLoopBegin) *
			(m_loopRenderCount - m_loopRenderRemaining)) + (pos - m_exportLoopBegin);
	}
	else
	{
		pos = (pos - m_exportSongBegin);
	}

	return (float)pos/(float)m_exportEffectiveLength*100.0f;
}

bool Song::setPlayMode(PlayMode mode, const Clip* clip)
{
	if (static_cast<int>(mode) < 0 || mode >= PlayMode::Count)
	{
		return false;
	}
	const bool noteClip = dynamic_cast<const MidiClip*>(clip) || dynamic_cast<const SVSClip*>(clip);
	const auto* automation = dynamic_cast<const AutomationClip*>(clip);
	if ((mode == PlayMode::MidiClip && !noteClip) || (mode == PlayMode::AutomationClip && !automation))
	{
		return false;
	}
	auto guard = Engine::audioEngine()->requestChangesGuard();
	if (!isStopped())
	{
		stop();
	}
	m_playMode = mode;
	m_noteClipToPlay = mode == PlayMode::MidiClip ? clip : nullptr;
	m_automationClipToPlay = mode == PlayMode::AutomationClip ? automation : nullptr;
	m_recording = false;
	emit playbackStateChanged();
	return true;
}

const Clip* Song::previewClip() const
{
	if (m_playMode == PlayMode::MidiClip) { return m_noteClipToPlay; }
	if (m_playMode == PlayMode::AutomationClip)
	{
		return m_automationClipToPlay;
	}
	return nullptr;
}

Song::PlaybackState Song::capturePlaybackState() const
{
	return {m_playMode, m_playing, m_paused, m_recording, m_loopMidiClip, previewClip()};
}

void Song::restorePlaybackState(const PlaybackState& state)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	m_playMode = state.mode;
	m_playing = state.playing;
	m_paused = state.paused;
	m_recording = state.recording;
	m_loopMidiClip = state.loopPreview;
	m_noteClipToPlay = state.mode == PlayMode::MidiClip
			&& (dynamic_cast<const MidiClip*>(state.clip) || dynamic_cast<const SVSClip*>(state.clip))
		? state.clip
		: nullptr;
	m_automationClipToPlay
		= state.mode == PlayMode::AutomationClip ? dynamic_cast<const AutomationClip*>(state.clip) : nullptr;
	m_vstSyncController.setPlaybackState(m_playing && !m_paused);
	emit playbackStateChanged();
}

void Song::stopPreviewOf(const Clip* clip)
{
	if (clip && previewClip() == clip)
	{
		stop();
	}
}

void Song::playSVSClip(const SVSClip* clip, bool loop)
{
	if (!clip) { return; }
	// Reuse the note-clip preview timeline while preserving the Song marker on mode switches.
	auto& main = getTimeline(PlayMode::Song);
	const auto position = main.ticks();
	const auto frame = main.frameOffset();
	const auto elapsed = main.getElapsedSeconds();
	const auto playStart = main.playStartPosition();
	const bool wasSong = m_playMode == PlayMode::Song;
	if (!setPlayMode(PlayMode::MidiClip, clip)) { return; }
	if (wasSong) { main.restorePlaybackPosition(position, frame, elapsed, playStart); }
	auto& timeline = getTimeline(PlayMode::MidiClip);
	timeline.setTicks(std::clamp(timeline.ticks(), std::max(0, -int(clip->startTimeOffset())),
		int(clip->length()) - int(clip->startTimeOffset())));
	m_loopMidiClip = loop;
	m_playing = true;
	m_paused = false;
	m_vstSyncController.setPlaybackState(true);
	savePlayStartPosition();
	emit playbackStateChanged();
}

void Song::playAutomationClip(const AutomationClip* clip, bool loop)
{
	if (!setPlayMode(PlayMode::AutomationClip, clip))
	{
		return;
	}
	m_loopMidiClip = loop;
	m_playing = true;
	m_paused = false;
	m_vstSyncController.setPlaybackState(true);
	savePlayStartPosition();
	emit playbackStateChanged();
}

void Song::playSong()
{
	m_recording = false;

	if( isStopped() == false )
	{
		stop();
	}

	m_playMode = PlayMode::Song;
	m_playing = true;
	m_paused = false;

	m_vstSyncController.setPlaybackState( true );

	savePlayStartPosition();

	emit playbackStateChanged();
}




void Song::record()
{
	m_recording = true;
	// TODO: Implement
}




void Song::playAndRecord()
{
	playSong();
	m_recording = true;
}




void Song::playPattern()
{
	if( isStopped() == false )
	{
		stop();
	}

	m_playMode = PlayMode::Pattern;
	m_playing = true;
	m_paused = false;

	m_vstSyncController.setPlaybackState( true );

	savePlayStartPosition();

	emit playbackStateChanged();
}




void Song::playMidiClip( const MidiClip* midiClipToPlay, bool loop )
{
	if( isStopped() == false )
	{
		stop();
	}

	m_noteClipToPlay = midiClipToPlay;
	m_loopMidiClip = loop;

	if (m_noteClipToPlay != nullptr)
	{
		m_playMode = PlayMode::MidiClip;
		m_playing = true;
		m_paused = false;
	}

	savePlayStartPosition();

	emit playbackStateChanged();
}




void Song::updateLength()
{
	if (m_loadingProject) { return; }

	m_length = 0;
	m_tracksMutex.lockForRead();
	for (auto track : tracks())
	{
		if (m_exporting && track->isMuted())
		{
			continue;
		}

		const bar_t cur = track->length();
		if( cur > m_length )
		{
			m_length = cur;
		}
		for (const auto* source : nodePianoRollSources(*track))
		{
			const auto end = source->startPosition() + source->length();
			m_length = std::max(m_length, TimePos(end).nextFullBar());
		}
	}
	m_tracksMutex.unlock();

	emit lengthChanged( m_length );
}




void Song::togglePause()
{
	// Pause/unpause only works when something is actually playing
	if (m_playMode == PlayMode::None)
	{
		return;
	}

	if( m_paused == true )
	{
		m_playing = true;
		m_paused = false;
	}
	else
	{
		m_playing = false;
		m_paused = true;
		Engine::audioEngine()->clear();
	}

	m_vstSyncController.setPlaybackState( m_playing );

	emit playbackStateChanged();
}




void Song::stop()
{
	// do not stop/reset things again if we're stopped already
	if( m_playMode == PlayMode::None )
	{
		return;
	}

	using gui::TimeLineWidget;

	// To avoid race conditions with the processing threads
	Engine::audioEngine()->requestChangeInModel();

	auto& timeline = getTimeline();
	m_paused = false;
	m_recording = true;
	m_playing = false;

	switch (timeline.stopBehaviour())
	{
		case Timeline::StopBehaviour::BackToZero:
			if ((m_playMode == PlayMode::MidiClip || m_playMode == PlayMode::AutomationClip) && previewClip())
			{
				timeline.setTicks(std::max(0, -previewClip()->startTimeOffset()));
			}
			else
			{
				timeline.setTicks(0);
			}
			break;

		case Timeline::StopBehaviour::BackToStart:
			if (timeline.playStartPosition() >= 0)
			{
				timeline.setTicks(timeline.playStartPosition().getTicks());

				timeline.setPlayStartPosition(-1);
			}
			break;

		case Timeline::StopBehaviour::KeepPosition:
			break;
	}

	getTimeline(PlayMode::None).setTicks(getPlayPos().getTicks());

	m_vstSyncController.setPlaybackState( m_exporting );
	m_vstSyncController.setAbsolutePosition(
		getPlayPos().getTicks()
		+ timeline.frameOffset()
		/ (double) Engine::framesPerTick() );

	// remove all note-play-handles that are active
	Engine::audioEngine()->clear();

	// Moves the control of the models that were processed on the last frame
	// back to their controllers.
	for (auto it = m_oldAutomatedValues.begin(); it != m_oldAutomatedValues.end(); it++)
	{
		AutomatableModel * am = it.key();
		am->setUseControllerValue(true);
	}
	m_oldAutomatedValues.clear();

	m_playMode = PlayMode::None;

	Engine::audioEngine()->doneChangeInModel();

	emit stopped();
	emit playbackStateChanged();
}




void Song::startExport()
{
	stop();

	m_exporting = true;
	updateLength();

	auto& timeline = getTimeline(PlayMode::Song);

	if (m_renderBetweenMarkers)
	{
		m_exportSongBegin = m_exportLoopBegin = timeline.loopBegin();
		m_exportSongEnd = m_exportLoopEnd = timeline.loopEnd();

		timeline.setTicks(timeline.loopBegin().getTicks());
	}
	else
	{
		m_exportSongEnd = TimePos(m_length, 0);

		// Handle potentially ridiculous loop points gracefully.
		if (m_loopRenderCount > 1 && timeline.loopEnd() > m_exportSongEnd)
		{
			m_exportSongEnd = timeline.loopEnd();
		}

		if (!m_exportLoop)
			m_exportSongEnd += TimePos(1,0);

		m_exportSongBegin = TimePos(0,0);
		m_exportLoopBegin = timeline.loopBegin() < m_exportSongEnd && timeline.loopEnd() <= m_exportSongEnd
			? timeline.loopBegin()
			: TimePos{0};
		m_exportLoopEnd = timeline.loopBegin() < m_exportSongEnd && timeline.loopEnd() <= m_exportSongEnd
			? timeline.loopEnd()
			: TimePos{0};

		getTimeline(PlayMode::Song).setTicks(0);
	}

	m_exportEffectiveLength = (m_exportLoopBegin - m_exportSongBegin)
		+ (m_exportLoopEnd - m_exportLoopBegin)
		* m_loopRenderCount + (m_exportSongEnd - m_exportLoopEnd);
	m_loopRenderRemaining = m_loopRenderCount;

	playSong();

	m_vstSyncController.setPlaybackState( true );
}




void Song::stopExport()
{
	stop();
	m_exporting = false;

	m_vstSyncController.setPlaybackState( m_playing );
}




void Song::insertBar()
{
	m_tracksMutex.lockForRead();
	for (Track* track: tracks())
	{
		// FIXME journal batch of tracks instead of each track individually
		if (track->numOfClips() > 0) { track->addJournalCheckPoint(); }
		track->insertBar(getPlayPos(PlayMode::Song));
	}
	m_tracksMutex.unlock();
}




void Song::removeBar()
{
	m_tracksMutex.lockForRead();
	for (Track* track: tracks())
	{
		// FIXME journal batch of tracks instead of each track individually
		if (track->numOfClips() > 0) { track->addJournalCheckPoint(); }
		track->removeBar(getPlayPos(PlayMode::Song));
	}
	m_tracksMutex.unlock();
}




void Song::addPatternTrack()
{
	Track * t = Track::create(Track::Type::Pattern, this);
	Engine::patternStore()->setCurrentPattern(dynamic_cast<PatternTrack*>(t)->patternIndex());
}




void Song::addSampleTrack()
{
	( void )Track::create( Track::Type::Sample, this );
}




void Song::addAutomationTrack()
{
	( void )Track::create( Track::Type::Automation, this );
}




bpm_t Song::getTempo()
{
	return ( bpm_t )m_tempoModel.value();
}


AutomatedValueMap Song::automatedValuesAt(TimePos time, int clipNum) const
{
	auto trackList = TrackList{m_globalAutomationTrack};
	trackList.insert(trackList.end(), tracks().begin(), tracks().end());
	return TrackContainer::automatedValuesFromTracks(trackList, time, clipNum);
}




void Song::clearProject()
{
#ifdef WANT_AGENT
	if (agent::CommandBus::instance().agentExecutionActive())
	{
		throw agent::AgentProjectResetError();
	}
#endif
	using gui::getGUI;
	setProjectWorkspace(m_nodeProject, false);
	m_nodeWorkflow = {};
	m_deferredVstOutputs = {};
	m_vstTrackAudio.clear();
	Engine::mixer()->setNodeMasterOutputMask(3);
	const bool wasLoading = m_loadingProject;
	const QScopedValueRollback<bool> loadingGuard(m_loadingProject, true);
	emit projectAboutToReset();

	Engine::projectJournal()->setJournalling( false );

	if( m_playing )
	{
		stop();
	}

	for (auto i = std::size_t{0}; i < PlayModeCount; i++)
	{
		setPlayPos( 0, ( PlayMode )i );
	}


	Engine::audioEngine()->requestChangeInModel();

	// Materialize queued track views while their models still exist, so the
	// following view teardown also removes them before deleting the tracks.
	QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
	m_nodePianoRollClips.clear();
	m_nodePianoRollTracks.reset();
	m_nodeInstrumentLayers.clear();
	m_nodeCustomInstruments.clear();
	m_nodeInstrumentTracks.reset();
	if( getGUI() != nullptr && getGUI()->patternEditor() )
	{
		getGUI()->patternEditor()->m_editor->clearAllTracks();
	}
	if( getGUI() != nullptr && getGUI()->songEditor() )
	{
		getGUI()->songEditor()->m_editor->clearAllTracks();
	}
	if( getGUI() != nullptr && getGUI()->mixerView() )
	{
		getGUI()->mixerView()->clear();
	}
	Engine::patternStore()->clearAllTracks();
	clearAllTracks();

	Engine::mixer()->clear();

	if( getGUI() != nullptr && getGUI()->automationEditor() )
	{
		getGUI()->automationEditor()->setCurrentClip( nullptr );
	}

	if( getGUI() != nullptr && getGUI()->pianoRoll() )
	{
		getGUI()->pianoRoll()->reset();
	}

	m_tempoModel.reset();
	m_masterVolumeModel.reset();
	m_masterPitchModel.reset();
	m_timeSigModel.reset();

	// Clear the m_oldAutomatedValues AutomatedValueMap
	m_oldAutomatedValues.clear();

	AutomationClip::globalAutomationClip( &m_tempoModel )->clear();
	AutomationClip::globalAutomationClip( &m_masterVolumeModel )->
									clear();
	AutomationClip::globalAutomationClip( &m_masterPitchModel )->
									clear();

	Engine::audioEngine()->doneChangeInModel();

	if( getGUI() != nullptr && getGUI()->getProjectNotes() )
	{
		getGUI()->getProjectNotes()->clear();
	}

	removeAllControllers();

	emit dataChanged();

	Engine::projectJournal()->clearJournal();

	Engine::projectJournal()->setJournalling( true );
	m_loadingProject = wasLoading;
	updateLength();
}




// create new file
void Song::createNodeProject()
{
	createNewProject();
	setProjectWorkspace(m_nodeProject, true);
}

bool Song::convertToNodeProject(const QString& fileName)
{
	const QFileInfo target(fileName);
	const auto extension = target.suffix().toLower();
	const auto sourcePath = QFileInfo(m_fileName).canonicalFilePath();
	if (m_nodeProject || (extension != "mmpn" && extension != "mmpnz")
		|| (!sourcePath.isEmpty() && sourcePath == target.canonicalFilePath()))
	{
		return false;
	}
	if (!saveProjectFile(fileName, false, true))
	{
		return false;
	}
	setProjectWorkspace(m_nodeProject, true);
	setProjectFileName(fileName);
	setModified(false);
	return true;
}

void Song::createNewProject()
{
#ifdef WANT_AGENT
	if (agent::CommandBus::instance().agentExecutionActive())
	{
		throw agent::AgentProjectResetError();
	}
#endif

	QString defaultTemplate = ConfigManager::inst()->userTemplateDir()
						+ "default.mpt";


	if( QFile::exists( defaultTemplate ) )
	{
		createNewProjectFromTemplate( defaultTemplate );
		return;
	}

	defaultTemplate = ConfigManager::inst()->factoryProjectsDir()
						+ "templates/default.mpt";
	if( QFile::exists( defaultTemplate ) )
	{
		createNewProjectFromTemplate( defaultTemplate );
		return;
	}

	m_loadingProject = true;

	clearProject();

	Engine::projectJournal()->setJournalling( false );

	m_oldFileName = "";
	setProjectFileName("");

	auto tripleOscTrack = Track::create(Track::Type::Instrument, this);
	dynamic_cast<InstrumentTrack*>(tripleOscTrack)->loadInstrument("tripleoscillator");

	auto kickerTrack = Track::create(Track::Type::Instrument, Engine::patternStore());
	dynamic_cast<InstrumentTrack*>(kickerTrack)->loadInstrument("kicker");

	Track::create( Track::Type::Sample, this );
	Track::create( Track::Type::Pattern, this );
	Track::create( Track::Type::Automation, this );

	m_tempoModel.setInitValue( DefaultTempo );
	m_timeSigModel.reset();
	m_masterVolumeModel.setInitValue( 100 );
	m_masterPitchModel.setInitValue( 0 );

	QCoreApplication::instance()->processEvents();

	m_loadingProject = false;
	updateLength();
	Engine::patternStore()->updateAfterTrackAdd();

	Engine::projectJournal()->setJournalling( true );

	QCoreApplication::sendPostedEvents();

	setModified(false);
	m_loadOnLaunch = false;
}




void Song::createNewProjectFromTemplate( const QString & templ )
{
#ifdef WANT_AGENT
	if (agent::CommandBus::instance().agentExecutionActive())
	{
		throw agent::AgentProjectResetError();
	}
#endif
	loadProject( templ );
	// clear file-name so that user doesn't overwrite template when
	// saving...
	m_oldFileName = "";
	setProjectFileName("");
	// update window title
	m_loadOnLaunch = false;
}




std::optional<QPointF> Song::nodeTrackPosition(Track& track) const
{
	const auto id = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto item = workflow.firstChildElement("track-position"); !item.isNull();
		item = item.nextSiblingElement("track-position"))
	{
		if (item.attribute("track") == id)
		{
			bool validX = false;
			bool validY = false;
			const auto x = item.attribute("x").toDouble(&validX);
			const auto y = item.attribute("y").toDouble(&validY);
			if (validX && validY && std::isfinite(x) && std::isfinite(y))
			{
				return QPointF(x, y);
			}
			break;
		}
	}
	return std::nullopt;
}

void Song::setNodeTrackPosition(Track& track, const QPointF& position)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = ensureNodeWorkflow();
	const auto id = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	auto item = workflow.firstChildElement("track-position");
	while (!item.isNull() && item.attribute("track") != id)
	{
		item = item.nextSiblingElement("track-position");
	}
	if (item.isNull())
	{
		item = m_nodeWorkflow.createElement("track-position");
		item.setAttribute("track", id);
		workflow.appendChild(item);
	}
	item.setAttribute("x", position.x());
	item.setAttribute("y", position.y());
	setModified();
}

std::optional<QPointF> Song::nodeLayoutPosition(const QString& key) const
{
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto item = workflow.firstChildElement("node-position"); !item.isNull();
		item = item.nextSiblingElement("node-position"))
	{
		if (item.attribute("key") == key)
		{
			bool validX = false;
			bool validY = false;
			const auto x = item.attribute("x").toDouble(&validX);
			const auto y = item.attribute("y").toDouble(&validY);
			if (validX && validY && std::isfinite(x) && std::isfinite(y))
			{
				return QPointF(x, y);
			}
			break;
		}
	}
	return std::nullopt;
}

void Song::setNodeLayoutPosition(const QString& key, const QPointF& position)
{
	if (!m_nodeProject || key.isEmpty() || !std::isfinite(position.x()) || !std::isfinite(position.y()))
	{
		return;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = ensureNodeWorkflow();
	auto item = workflow.firstChildElement("node-position");
	while (!item.isNull() && item.attribute("key") != key)
	{
		item = item.nextSiblingElement("node-position");
	}
	if (item.isNull())
	{
		item = m_nodeWorkflow.createElement("node-position");
		item.setAttribute("key", key);
		workflow.appendChild(item);
	}
	item.setAttribute("x", position.x());
	item.setAttribute("y", position.y());
	setModified();
}

static IntModel* trackMixerChannelModel(Track& track)
{
	switch (track.type())
	{
		case Track::Type::Instrument:
			return static_cast<InstrumentTrack&>(track).mixerChannelModel();
		case Track::Type::Sample:
			return static_cast<SampleTrack&>(track).mixerChannelModel();
		case Track::Type::SVS:
			return static_cast<SVSTrack&>(track).mixerChannelModel();
		case Track::Type::SVC:
			return static_cast<SVCTrack&>(track).mixerChannelModel();
		default:
			return nullptr;
	}
}

QStringList Song::classicConversionChanges()
{
	QStringList changes;
	const auto workflow = m_nodeWorkflow.documentElement();
	if (!workflow.firstChildElement("track-position").isNull()
		|| !workflow.firstChildElement("node-position").isNull())
	{
		changes.append(tr("Custom node positions will be omitted."));
	}
	if (!nodeLogicalInputs().isEmpty()
		|| !m_nodeWorkflow.documentElement().firstChildElement("boundary-port").isNull())
	{
		changes.append(tr("Logical inputs and their connections will be omitted."));
	}
	if (!nodeStepPatterns().isEmpty())
	{
		changes.append(tr("Standalone step patterns will be omitted."));
	}
	if (!nodePianoRolls().isEmpty())
	{
		changes.append(tr("Standalone piano rolls will be omitted."));
	}
	auto* mixer = Engine::mixer();
	if (mixer->nodeMasterOutputMask() != 3)
	{
		changes.append(tr("Both Master output channels will be enabled."));
	}
	for (const auto* route : mixer->m_mixerRoutes)
	{
		if (route->channelPairs() != MixerRoute::StereoChannels)
		{
			changes.append(tr("Mixer send %1 → %2 will become stereo.")
				.arg(route->sender()->m_name, route->receiver()->m_name));
		}
	}
	for (mix_ch_t channel = 1; channel < mixer->numChannels(); ++channel)
	{
		const auto* target = mixer->mixerChannel(channel);
		if (target->m_nodeEffect)
		{
			changes.append(tr("Effect node %1 will be represented as a mixer channel.")
				.arg(target->m_name));
		}
	}
	for (auto* container : {static_cast<TrackContainer*>(this), static_cast<TrackContainer*>(Engine::patternStore())})
	{
		for (auto* track : container->tracks())
		{
			const auto* model = trackMixerChannelModel(*track);
			if (!model)
			{
				continue;
			}
			const auto left = nodeTrackMixerEndpoint(*track, 0);
			const auto right = nodeTrackMixerEndpoint(*track, 1);
			if (!left || !right || left->side != 0 || right->side != 1
				|| left->channel != model->value() || right->channel != model->value()
				|| !nodeTrackBoundaryConnected(*track, 0) || !nodeTrackBoundaryConnected(*track, 1))
			{
				changes.append(tr("Track %1 will use its classic stereo mixer assignment.").arg(track->name()));
			}
			if (track->type() == Track::Type::Instrument || track->type() == Track::Type::SVS
				|| track->type() == Track::Type::Sample)
			{
				const auto source = nodeSourceConnections(*track);
				const bool eventTrack = track->type() != Track::Type::Sample;
				if ((eventTrack && (!source.midiInput || !source.events || !source.stepEvents))
					|| source.audioOutputs[0] != 0u || source.audioOutputs[1] != 1u)
				{
					changes.append(tr("Track %1 will use its classic event and audio connections.").arg(track->name()));
				}
			}
		}
	}
	return changes;
}

bool Song::hasExtendedSourceGraphs() const
{
	if (hasExtendedSourceGraphs(m_nodeWorkflow.documentElement(), {}))
	{
		return true;
	}
	for (auto* container : {static_cast<const TrackContainer*>(this),
		static_cast<const TrackContainer*>(Engine::patternStore())})
	{
		for (auto* track : container->tracks())
		{
			if (!sourceGraphEffectSlots(*track).isEmpty())
			{
				return true;
			}
		}
	}
	return false;
}

bool Song::hasExtendedSourceGraphs(const QDomElement& workflow, const QDomElement& content)
{
	if (!workflow.firstChildElement("instrument-layer").isNull()
		|| !workflow.firstChildElement("custom-graph").isNull())
	{
		return true;
	}
	for (auto port = workflow.firstChildElement("boundary-port"); !port.isNull();
		port = port.nextSiblingElement("boundary-port"))
	{
		if (port.attribute("graph").startsWith("source/"))
		{
			return true;
		}
	}
	const auto effects = content.elementsByTagName("effect");
	for (qsizetype index = 0; index < effects.count(); ++index)
	{
		if (!effects.item(index).toElement().attribute("nodeWorkflowSourceEffect").isEmpty())
		{
			return true;
		}
	}
	return false;
}

static AudioBusHandle* sourceGraphAudioBus(Track& track)
{
	switch (track.type())
	{
	case Track::Type::Instrument: return static_cast<InstrumentTrack&>(track).audioBusHandle();
	case Track::Type::Sample: return static_cast<SampleTrack&>(track).audioBusHandle();
	case Track::Type::SVS: return static_cast<SVSTrack&>(track).audioBusHandle();
	case Track::Type::SVC: return static_cast<SVCTrack&>(track).audioBusHandle();
	default: return nullptr;
	}
}

QList<int> Song::sourceGraphEffectSlots(Track& track) const
{
	auto* bus = sourceGraphAudioBus(track);
	QList<int> indices;
	auto* chain = bus ? bus->effects() : nullptr;
	if (chain)
	{
		for (int index = 0; index < chain->effectCount(); ++index)
		{
			if (!chain->effectAt(index)->property("nodeWorkflowSourceEffect").toString().isEmpty())
			{
				indices.append(index);
			}
		}
	}
	return indices;
}

bool Song::convertToClassicProject(const QString& fileName)
{
	const QFileInfo target(fileName);
	const auto extension = target.suffix().toLower();
	const auto sourcePath = QFileInfo(m_fileName).canonicalFilePath();
	if (!m_nodeProject || hasExtendedSourceGraphs() || (extension != "mmp" && extension != "mmpz")
		|| (!sourcePath.isEmpty() && sourcePath == target.canonicalFilePath()))
	{
		return false;
	}
	if (!saveProjectFile(fileName, false, false))
	{
		return false;
	}
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		for (auto* track : tracks())
		{
			if (track->type() == Track::Type::SVS)
			{
				static_cast<SVSTrack*>(track)->clearNodeVoiceSources();
			}
		}
		m_nodePianoRollClips.clear();
		m_nodePianoRollTracks.reset();
		m_nodeWorkflow = QDomDocument{};
		restoreNodeAudioRoutes();
		auto* mixer = Engine::mixer();
		mixer->setNodeMasterOutputMask(3);
		for (auto* route : mixer->m_mixerRoutes)
		{
			route->setChannelPairs(MixerRoute::StereoChannels);
		}
		for (mix_ch_t channel = 1; channel < mixer->numChannels(); ++channel)
		{
			auto* targetChannel = mixer->mixerChannel(channel);
			targetChannel->m_nodeEffect = false;
			targetChannel->m_nodeEffectPosition.reset();
		}
		setProjectWorkspace(m_nodeProject, false);
	}
	emit nodeTrackRoutingChanged();
	setProjectFileName(fileName);
	setModified(false);
	return true;
}

std::optional<Song::NodeMixerEndpoint> Song::nodeTrackMixerEndpoint(Track& track, ch_cnt_t sourceSide) const
{
	const auto* model = trackMixerChannelModel(track);
	const auto* container = track.trackContainer();
	if (!m_nodeProject || !model || sourceSide >= 2
		|| (container != this && container != Engine::patternStore()))
	{
		return std::nullopt;
	}

	const auto trackId = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	const auto side = sourceSide == 0 ? QStringLiteral("L") : QStringLiteral("R");
	QDomElement match;
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto route = workflow.firstChildElement("track-audio-route"); !route.isNull();
		route = route.nextSiblingElement("track-audio-route"))
	{
		if (route.attribute("track") == trackId && route.attribute("side") == side)
		{
			match = route;
		}
	}

	auto* mixer = Engine::mixer();
	if (match.isNull())
	{
		const auto channel = static_cast<mix_ch_t>(model->value());
		if (channel < mixer->numChannels())
		{
			return NodeMixerEndpoint{channel, sourceSide};
		}
		return std::nullopt;
	}

	const auto target = match.attribute("mixer");
	const auto input = match.attribute("input");
	if (target.isEmpty() || (input != "L" && input != "R"))
	{
		return std::nullopt;
	}

	for (mix_ch_t channel = 0; channel < mixer->numChannels(); ++channel)
	{
		if (mixer->mixerChannel(channel)->nodeWorkflowId().toString(QUuid::WithoutBraces) == target)
		{
			return NodeMixerEndpoint{channel, input == "L" ? ch_cnt_t{0} : ch_cnt_t{1}};
		}
	}
	return std::nullopt;
}

bool Song::nodeTrackBoundaryConnected(Track& track, ch_cnt_t side) const
{
	const auto workflow = m_nodeWorkflow.documentElement();
	const auto id = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	const auto channel = side == 0 ? QStringLiteral("L") : QStringLiteral("R");
	for (auto item = workflow.firstChildElement("track-boundary-disconnect"); !item.isNull();
		item = item.nextSiblingElement("track-boundary-disconnect"))
	{
		if (item.attribute("track") == id && item.attribute("side") == channel)
		{
			return false;
		}
	}
	return true;
}

bool Song::setNodeTrackBoundaryConnected(Track& track, ch_cnt_t side, bool connected)
{
	const auto* container = track.trackContainer();
	if (!m_nodeProject || side >= 2 || !trackMixerChannelModel(track)
		|| (container != this && container != Engine::patternStore())
		|| nodeTrackBoundaryConnected(track, side) == connected)
	{
		return false;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = ensureNodeWorkflow();
	const auto id = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	const auto channel = side == 0 ? QStringLiteral("L") : QStringLiteral("R");
	if (connected)
	{
		auto item = workflow.firstChildElement("track-boundary-disconnect");
		while (!item.isNull())
		{
			const auto next = item.nextSiblingElement("track-boundary-disconnect");
			if (item.attribute("track") == id && item.attribute("side") == channel)
			{
				workflow.removeChild(item);
			}
			item = next;
		}
	}
	else
	{
		auto item = m_nodeWorkflow.createElement("track-boundary-disconnect");
		item.setAttribute("track", id);
		item.setAttribute("side", channel);
		workflow.appendChild(item);
	}
	restoreNodeAudioRoutes();
	setModified();
	emit nodeTrackRoutingChanged();
	return true;
}

bool Song::setNodeTrackAudioRoute(Track& track, ch_cnt_t sourceSide,
	std::optional<mix_ch_t> mixerChannel, ch_cnt_t destinationSide)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const auto* pattern = Engine::patternStore();
	const auto* container = track.trackContainer();
	auto* mixer = Engine::mixer();
	if (!m_nodeProject || (container != this && container != pattern)
		|| sourceSide >= 2 || (mixerChannel && (*mixerChannel >= mixer->numChannels() || destinationSide >= 2)))
	{
		return false;
	}

	auto* model = trackMixerChannelModel(track);
	if (!model)
	{
		return false;
	}

	auto workflow = ensureNodeWorkflow();
	const auto trackId = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	const auto source = sourceSide == 0 ? QStringLiteral("L") : QStringLiteral("R");
	auto route = workflow.firstChildElement("track-audio-route");
	while (!route.isNull())
	{
		const auto next = route.nextSiblingElement("track-audio-route");
		if (route.attribute("track") == trackId && route.attribute("side") == source)
		{
			workflow.removeChild(route);
		}
		route = next;
	}

	route = m_nodeWorkflow.createElement("track-audio-route");
	route.setAttribute("track", trackId);
	route.setAttribute("side", source);
	if (mixerChannel)
	{
		route.setAttribute("mixer", mixer->mixerChannel(*mixerChannel)->nodeWorkflowId().toString(QUuid::WithoutBraces));
		route.setAttribute("input", destinationSide == 0 ? "L" : "R");
	}
	workflow.appendChild(route);
	const auto left = nodeTrackMixerEndpoint(track, 0);
	const auto right = nodeTrackMixerEndpoint(track, 1);
	if (left && right && left->channel == right->channel && left->side == 0 && right->side == 1)
	{
		model->setValue(left->channel);
	}

	restoreNodeAudioRoutes();
	setModified();
	emit nodeTrackRoutingChanged();
	return true;
}

QList<Song::NodeBoundaryPort> Song::nodeBoundaryPorts(const QString& graph) const
{
	QList<NodeBoundaryPort> ports;
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto port = workflow.firstChildElement("boundary-port"); !port.isNull();
		port = port.nextSiblingElement("boundary-port"))
	{
		if (port.attribute("graph") == graph)
		{
			NodeBoundaryPort boundary{port.attribute("id"), port.attribute("signal"),
				port.attribute("direction") == "out", port.attribute("track"), port.attribute("channel").toUInt(), {}};
			boundary.stereoGroup = port.attribute("stereo");
			boundary.stereoSide = port.attribute("side", "-1").toInt();
			boundary.name = port.attribute("name");
			if (!boundary.output)
			{
				for (const auto& edge : nodeInputDestinations(workflow, boundary.id, boundary.signal))
				{
					boundary.destinations.append(qMakePair(edge.attribute("track"), edge.attribute("port")));
				}
			}
			ports.append(std::move(boundary));
		}
	}
	return ports;
}

void Song::addNodeBoundaryPort(const QString& graph, const QString& signal, bool output)
{
	createNodeBoundaryPort(graph, signal, output, false);
}

QString Song::createNodeBoundaryPort(const QString& graph, const QString& signal, bool output, bool stereo)
{
	static const QStringList signalTypes{"audio", "midi", "step", "voice"};
	if (!m_nodeProject || graph.isEmpty() || !signalTypes.contains(signal) || (stereo && signal != "audio"))
	{
		return {};
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = ensureNodeWorkflow();
	const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	for (int side = 0; side < (stereo ? 2 : 1); ++side)
	{
		auto port = m_nodeWorkflow.createElement("boundary-port");
		port.setAttribute("id", side == 0 ? id : QUuid::createUuid().toString(QUuid::WithoutBraces));
		port.setAttribute("graph", graph);
		port.setAttribute("signal", signal);
		port.setAttribute("direction", output ? "out" : "in");
		if (stereo)
		{
			port.setAttribute("stereo", id);
			port.setAttribute("side", side);
		}
		workflow.appendChild(port);
	}
	setModified();
	return id;
}

void Song::removeNodeBoundaryPort(const QString& graph, const QString& id)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = m_nodeWorkflow.documentElement();
	QStringList removed;
	for (auto port = workflow.firstChildElement("boundary-port"); !port.isNull();)
	{
		const auto next = port.nextSiblingElement("boundary-port");
		if (port.attribute("graph") == graph
			&& (port.attribute("id") == id || port.attribute("stereo") == id))
		{
			removed.append(port.attribute("id"));
			workflow.removeChild(port);
		}
		port = next;
	}
	if (removed.isEmpty())
	{
		return;
	}
	for (auto edge = workflow.firstChildElement("custom-edge"); !edge.isNull();)
	{
		const auto next = edge.nextSiblingElement("custom-edge");
		if (removed.contains(edge.attribute("source")) || removed.contains(edge.attribute("destination")))
		{
			workflow.removeChild(edge);
		}
		edge = next;
	}
	for (auto input = workflow.firstChildElement("aggregate-input"); !input.isNull();)
	{
		const auto next = input.nextSiblingElement("aggregate-input");
		if (removed.contains(input.attribute("id")))
		{
			workflow.removeChild(input);
		}
		else
		{
			for (auto destination = input.firstChildElement("destination"); !destination.isNull();)
			{
				const auto nextDestination = destination.nextSiblingElement("destination");
				const auto port = destination.attribute("port");
				if (port.startsWith("midi-in/") && removed.contains(port.mid(8)))
				{
					input.removeChild(destination);
				}
				destination = nextDestination;
			}
		}
		input = next;
	}
	for (const auto& tag : {QStringLiteral("piano-roll"), QStringLiteral("step-pattern")})
	{
		for (auto item = workflow.firstChildElement(tag); !item.isNull(); item = item.nextSiblingElement(tag))
		{
			for (auto target = item.firstChildElement("target"); !target.isNull();)
			{
				const auto next = target.nextSiblingElement("target");
				if (removed.contains(target.attribute("port")))
				{
					item.removeChild(target);
				}
				target = next;
			}
		}
	}
	restoreNodeAudioRoutes();
	setModified();
}

bool Song::renameNodeBoundaryPort(const QString& graph, const QString& id, const QString& name)
{
	const auto title = name.trimmed();
	if (!m_nodeProject || graph.isEmpty() || id.isEmpty() || title.isEmpty())
	{
		return false;
	}
	bool found = false;
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto port = workflow.firstChildElement("boundary-port"); !port.isNull();
		port = port.nextSiblingElement("boundary-port"))
	{
		if (port.attribute("graph") == graph && (port.attribute("id") == id || port.attribute("stereo") == id))
		{
			port.setAttribute("name", title);
			found = true;
		}
	}
	if (found)
	{
		setModified();
	}
	return found;
}

QList<Song::NodeCustomGraph> Song::nodeCustomGraphs(const QString& parent) const
{
	QList<NodeCustomGraph> result;
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto graph = workflow.firstChildElement("custom-graph"); !graph.isNull();
		graph = graph.nextSiblingElement("custom-graph"))
	{
		if (graph.attribute("parent") == parent)
		{
			result.append({graph.attribute("id"), graph.attribute("name")});
		}
	}
	return result;
}

QString Song::addNodeCustomGraph(const QString& parent, const QString& name)
{
	const auto title = name.trimmed();
	if (!m_nodeProject || parent.isEmpty() || title.isEmpty())
	{
		return {};
	}
	if (parent.startsWith("source/"))
	{
		for (auto* track : tracks())
		{
			if (track->type() == Track::Type::SVS
				&& parent == QStringLiteral("source/") + track->nodeWorkflowId().toString(QUuid::WithoutBraces) + '/')
			{
				return {};
			}
		}
	}
	auto workflow = ensureNodeWorkflow();
	auto graph = m_nodeWorkflow.createElement("custom-graph");
	const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	graph.setAttribute("id", id);
	graph.setAttribute("parent", parent);
	graph.setAttribute("name", title);
	workflow.appendChild(graph);
	setModified();
	return id;
}

bool Song::renameNodeCustomGraph(const QString& id, const QString& name)
{
	const auto title = name.trimmed();
	if (!m_nodeProject || id.isEmpty() || title.isEmpty())
	{
		return false;
	}
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto graph = workflow.firstChildElement("custom-graph"); !graph.isNull();
		graph = graph.nextSiblingElement("custom-graph"))
	{
		if (graph.attribute("id") == id)
		{
			graph.setAttribute("name", title);
			setModified();
			return true;
		}
	}
	return false;
}

bool Song::removeNodeCustomGraph(const QString& id)
{
	if (!m_nodeProject || QUuid(id).isNull())
	{
		return false;
	}
	const auto scopeFor = [](const QString& key) { return QStringLiteral("custom/") + key + '/'; };
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = m_nodeWorkflow.documentElement();
	QSet<QString> ids;
	QSet<QString> scopes;
	QStringList pending{id};
	while (!pending.isEmpty())
	{
		const auto key = pending.takeLast();
		if (ids.contains(key))
		{
			continue;
		}
		ids.insert(key);
		const auto scope = scopeFor(key);
		scopes.insert(scope);
		for (const auto& child : nodeCustomGraphs(scope))
		{
			pending.append(child.id);
		}
	}
	QSet<QString> boundaryIds;
	QSet<QUuid> effects;
	bool found = false;
	for (auto item = workflow.firstChildElement(); !item.isNull();)
	{
		const auto next = item.nextSiblingElement();
		const auto tag = item.tagName();
		const auto key = item.attribute("id");
		const bool inScope = scopes.contains(item.attribute("graph"));
		if (tag == "custom-graph" && ids.contains(key))
		{
			found = true;
			workflow.removeChild(item);
		}
		else if (inScope && tag == "boundary-port")
		{
			boundaryIds.insert(key);
			workflow.removeChild(item);
		}
		else if (inScope && tag == "custom-effect")
		{
			effects.insert(QUuid(key));
			workflow.removeChild(item);
		}
		else if (inScope && tag == "custom-instrument")
		{
			delete m_nodeCustomInstruments.take(key);
			workflow.removeChild(item);
		}
		else if (tag == "node-position")
		{
			const auto layoutKey = item.attribute("key");
			const auto parts = layoutKey.split('/');
			if (parts.size() >= 2 && parts.front() == "custom" && ids.contains(parts.at(1)))
			{
				workflow.removeChild(item);
			}
		}
		item = next;
	}
	if (!found)
	{
		return false;
	}
	for (auto edge = workflow.firstChildElement("custom-edge"); !edge.isNull();)
	{
		const auto next = edge.nextSiblingElement("custom-edge");
		if (scopes.contains(edge.attribute("graph")) || boundaryIds.contains(edge.attribute("source"))
			|| boundaryIds.contains(edge.attribute("destination")))
		{
			workflow.removeChild(edge);
		}
		edge = next;
	}
	auto* mixer = Engine::mixer();
	for (auto index = mixer->numChannels(); index > 1;)
	{
		--index;
		if (effects.contains(mixer->mixerChannel(index)->nodeWorkflowId()))
		{
			mixer->deleteChannel(index);
		}
	}
	restoreNodeAudioRoutes();
	setModified();
	return true;
}

QDomElement Song::copyNodeCustomGraph(const QString& id) const
{
	const auto workflow = m_nodeWorkflow.documentElement();
	QDomDocument document;
	auto snapshot = document.createElement("custom-graph-copy");
	snapshot.setAttribute("root", id);
	document.appendChild(snapshot);
	QHash<QString, QDomElement> graphs;
	for (auto item = workflow.firstChildElement("custom-graph"); !item.isNull();
		item = item.nextSiblingElement("custom-graph"))
	{
		graphs.insert(item.attribute("id"), item);
	}
	if (!graphs.contains(id))
	{
		return {};
	}
	QSet<QString> scopes;
	QStringList pending{id};
	while (!pending.isEmpty())
	{
		const auto key = pending.takeFirst();
		const auto scope = QStringLiteral("custom/") + key + '/';
		if (scopes.contains(scope))
		{
			continue;
		}
		scopes.insert(scope);
		snapshot.appendChild(document.importNode(graphs.value(key), true));
		for (const auto& child : nodeCustomGraphs(scope))
		{
			pending.append(child.id);
		}
	}
	for (auto item = workflow.firstChildElement(); !item.isNull(); item = item.nextSiblingElement())
	{
		const auto tag = item.tagName();
		const auto key = item.attribute("id");
		const auto scope = item.attribute("graph");
		if (tag == "node-position")
		{
			const auto parts = item.attribute("key").split('/');
			if (parts.size() >= 2 && scopes.contains(parts.first() + '/' + parts.at(1) + '/'))
			{
				snapshot.appendChild(document.importNode(item, true));
			}
		}
		else if (scopes.contains(scope))
		{
			auto saved = document.importNode(item, false).toElement();
			if (tag == "custom-instrument")
			{
				auto* instrument = m_nodeCustomInstruments.value(key);
				if (!instrument)
				{
					continue;
				}
				saved.appendChild(document.importNode(copyNodeInstrumentLayer(*instrument, {}), true));
			}
			else if (tag == "custom-effect")
			{
				auto* mixer = Engine::mixer();
				for (mix_ch_t index = 1; index < mixer->numChannels(); ++index)
				{
					auto* channel = mixer->mixerChannel(index);
					if (channel->nodeWorkflowId() == QUuid(key))
					{
						saved.setAttribute("name", channel->m_name);
						saved.setAttribute("volume", channel->m_volumeModel.value());
						channel->m_fxChain.saveState(document, saved);
						break;
					}
				}
			}
			snapshot.appendChild(saved);
		}
	}
	return snapshot;
}

static bool allowsCustomInstruments(const Song& song, const QDomElement& workflow, QString scope)
{
	QSet<QString> visited;
	while (scope.startsWith("custom/"))
	{
		if (visited.contains(scope))
		{
			return false;
		}
		visited.insert(scope);
		const auto id = scope.section('/', 1, 1);
		auto graph = workflow.firstChildElement("custom-graph");
		while (!graph.isNull() && graph.attribute("id") != id)
		{
			graph = graph.nextSiblingElement("custom-graph");
		}
		if (graph.isNull())
		{
			return false;
		}
		scope = graph.attribute("parent");
	}
	if (!scope.startsWith("source/"))
	{
		return true;
	}
	const auto id = QUuid(scope.section('/', 1, 1));
	const auto matches = [id](auto* track) {
		return track->type() == Track::Type::Instrument && track->nodeWorkflowId() == id;
	};
	const auto& songTracks = song.tracks();
	const auto& patternTracks = Engine::patternStore()->tracks();
	return std::any_of(songTracks.begin(), songTracks.end(), matches)
		|| std::any_of(patternTracks.begin(), patternTracks.end(), matches);
}

QString Song::pasteNodeCustomGraph(const QString& parent, const QDomElement& snapshot)
{
	if (!m_nodeProject || snapshot.tagName() != "custom-graph-copy")
	{
		return {};
	}
	if (!snapshot.firstChildElement("custom-instrument").isNull()
		&& !allowsCustomInstruments(*this, m_nodeWorkflow.documentElement(), parent))
	{
		return {};
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const auto oldRoot = snapshot.attribute("root");
	QHash<QString, QString> ids;
	const auto remap = [&ids](const QString& endpoint) {
		auto parts = endpoint.split('/');
		for (auto& part : parts)
		{
			part = ids.value(part, part);
		}
		return parts.join('/');
	};
	for (auto graph = snapshot.firstChildElement("custom-graph"); !graph.isNull();
		graph = graph.nextSiblingElement("custom-graph"))
	{
		const auto oldId = graph.attribute("id");
		const auto newId = addNodeCustomGraph(oldId == oldRoot ? parent : remap(graph.attribute("parent")),
			graph.attribute("name"));
		if (newId.isEmpty())
		{
			if (ids.contains(oldRoot))
			{
				removeNodeCustomGraph(ids.value(oldRoot));
			}
			return {};
		}
		ids.insert(oldId, newId);
	}
	auto workflow = ensureNodeWorkflow();
	for (auto item = snapshot.firstChildElement(); !item.isNull(); item = item.nextSiblingElement())
	{
		const auto tag = item.tagName();
		const auto oldId = item.attribute("id");
		const auto scope = remap(item.attribute("graph"));
		if (tag == "boundary-port")
		{
			const auto newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
			ids.insert(oldId, newId);
			auto copy = m_nodeWorkflow.importNode(item, false).toElement();
			copy.setAttribute("id", newId);
			copy.setAttribute("graph", scope);
			const auto stereo = item.attribute("stereo");
			if (!stereo.isEmpty())
			{
				if (!ids.contains(stereo))
				{
					ids.insert(stereo, QUuid::createUuid().toString(QUuid::WithoutBraces));
				}
				copy.setAttribute("stereo", ids.value(stereo));
			}
			workflow.appendChild(copy);
		}
		else if (tag == "custom-instrument")
		{
			const auto newId = pasteNodeCustomInstrument(scope, item.firstChildElement("instrument-layer"));
			if (!newId.isEmpty())
			{
				ids.insert(oldId, newId);
			}
		}
		else if (tag == "custom-effect" && !item.firstChildElement().isNull())
		{
			auto* mixer = Engine::mixer();
			const auto index = mixer->createChannel();
			auto* channel = mixer->mixerChannel(index);
			channel->m_name = item.attribute("name");
			channel->m_volumeModel.setValue(item.attribute("volume").toFloat());
			channel->m_fxChain.restoreState(item.firstChildElement());
			channel->m_nodeEffect = true;
			const auto id = channel->nodeWorkflowId();
			setNodeCustomEffectScope(id, scope);
			mixer->deleteChannelSend(index, 0);
			ids.insert(oldId, id.toString(QUuid::WithoutBraces));
		}
	}
	for (auto item = snapshot.firstChildElement(); !item.isNull(); item = item.nextSiblingElement())
	{
		if (item.tagName() == "custom-edge")
		{
			const auto source = item.attribute("source");
			const auto dest = item.attribute("destination");
			if (remap(source) != source && remap(dest) != dest)
			{
				setNodeCustomConnection(remap(item.attribute("graph")), remap(source), remap(dest), true);
			}
		}
		else if (item.tagName() == "node-position")
		{
			setNodeLayoutPosition(remap(item.attribute("key")),
				QPointF(item.attribute("x").toDouble(), item.attribute("y").toDouble()));
		}
	}
	return ids.value(oldRoot);
}

QString Song::nodeCustomEffectScope(const QUuid& id) const
{
	const auto key = id.toString(QUuid::WithoutBraces);
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto plugin = workflow.firstChildElement("custom-effect"); !plugin.isNull();
		plugin = plugin.nextSiblingElement("custom-effect"))
	{
		if (plugin.attribute("id") == key)
		{
			return plugin.attribute("graph");
		}
	}
	return {};
}

void Song::setNodeCustomEffectScope(const QUuid& id, const QString& scope)
{
	if (!m_nodeProject || id.isNull())
	{
		return;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const auto key = id.toString(QUuid::WithoutBraces);
	auto workflow = ensureNodeWorkflow();
	for (auto plugin = workflow.firstChildElement("custom-effect"); !plugin.isNull();
		plugin = plugin.nextSiblingElement("custom-effect"))
	{
		if (plugin.attribute("id") == key)
		{
			if (scope.isEmpty())
			{
				workflow.removeChild(plugin);
				for (auto edge = workflow.firstChildElement("custom-edge"); !edge.isNull();)
				{
					const auto next = edge.nextSiblingElement("custom-edge");
					const auto prefix = QStringLiteral("effect/") + key + '/';
					if (edge.attribute("source").startsWith(prefix) || edge.attribute("destination").startsWith(prefix))
					{
						workflow.removeChild(edge);
					}
					edge = next;
				}
			}
			else
			{
				plugin.setAttribute("graph", scope);
			}
			setModified();
			return;
		}
	}
	if (!scope.isEmpty())
	{
		auto plugin = m_nodeWorkflow.createElement("custom-effect");
		plugin.setAttribute("id", key);
		plugin.setAttribute("graph", scope);
		workflow.appendChild(plugin);
		setModified();
	}
}

QList<Song::NodeCustomConnection> Song::nodeCustomConnections(const QString& scope) const
{
	QList<NodeCustomConnection> result;
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto edge = workflow.firstChildElement("custom-edge"); !edge.isNull();
		edge = edge.nextSiblingElement("custom-edge"))
	{
		if (edge.attribute("graph") == scope)
		{
			result.append({edge.attribute("source"), edge.attribute("destination")});
		}
	}
	return result;
}

bool Song::setNodeCustomConnection(const QString& scope, const QString& source, const QString& destination,
	bool connected)
{
	if (!m_nodeProject || scope.isEmpty() || source.isEmpty() || destination.isEmpty() || source == destination)
	{
		return false;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const auto effectChannel = [](const QString& endpoint) -> mix_ch_t {
		const auto parts = endpoint.split('/');
		if (parts.size() != 3 || parts.front() != "effect")
		{
			return 0;
		}
		const auto id = QUuid(parts.at(1));
		auto* mixer = Engine::mixer();
		for (mix_ch_t index = 1; index < mixer->numChannels(); ++index)
		{
			if (mixer->mixerChannel(index)->nodeWorkflowId() == id)
			{
				return index;
			}
		}
		return 0;
	};
	const auto updateEffectSend = [&](bool enabled) {
		if ((!source.endsWith("/out-L") && !source.endsWith("/out-R"))
			|| (!destination.endsWith("/in-L") && !destination.endsWith("/in-R")))
		{
			return true;
		}
		const auto from = effectChannel(source);
		const auto to = effectChannel(destination);
		return from == 0 || to == 0 || Engine::mixer()->setNodeChannelSend(from,
			source.endsWith('R') ? 1 : 0, to, destination.endsWith('R') ? 1 : 0, enabled);
	};
	auto workflow = ensureNodeWorkflow();
	for (auto edge = workflow.firstChildElement("custom-edge"); !edge.isNull();
		edge = edge.nextSiblingElement("custom-edge"))
	{
		if (edge.attribute("graph") == scope && edge.attribute("source") == source
			&& edge.attribute("destination") == destination)
		{
			if (!connected)
			{
				if (!updateEffectSend(false))
				{
					return false;
				}
				workflow.removeChild(edge);
				restoreCustomGraphAudioRoutes();
				setModified();
			}
			return true;
		}
	}
	if (!connected)
	{
		return false;
	}
	QSet<QString> childScopes;
	for (const auto& child : nodeCustomGraphs(scope))
	{
		childScopes.insert(QStringLiteral("custom/") + child.id + '/');
	}
	if (scope == "root/")
	{
		childScopes.insert(QStringLiteral("song/"));
		childScopes.insert(QStringLiteral("pattern/"));
	}
	else if (scope == "song/" || scope == "pattern/")
	{
		const auto* container = scope == "song/" ? static_cast<const TrackContainer*>(this)
			: static_cast<const TrackContainer*>(Engine::patternStore());
		for (auto* track : container->tracks())
		{
			childScopes.insert(QStringLiteral("source/")
				+ track->nodeWorkflowId().toString(QUuid::WithoutBraces) + '/');
		}
	}
	const auto vstSignal = [](auto* plugin, const QString& key, bool output) -> QString {
		auto* owner = dynamic_cast<vsthost::VstNodeOwner*>(plugin);
		auto* processor = owner ? owner->vstNodeProcessor() : nullptr;
		if (!processor) { return {}; }
		const auto direction = output ? vsthost::NodePortDirection::Output : vsthost::NodePortDirection::Input;
		for (const auto& port : processor->nodePorts())
		{
			if (port.available && port.direction == direction && port.key() == key)
			{
				return port.media == vsthost::NodePortMedia::Audio ? QStringLiteral("audio") : QStringLiteral("midi");
			}
		}
		return {};
	};
	const auto endpoint = [&](const QString& id, bool output) {
		if (output && id.startsWith("track-signal/") && (scope == "song/" || scope == "pattern/"))
		{
			const auto parts = id.split('/');
			if (parts.size() != 3 || (parts[2] != "out-L" && parts[2] != "out-R")) { return QString{}; }
			const auto trackId = QUuid(parts[1]);
			const auto* container = scope == "song/" ? static_cast<const TrackContainer*>(this)
				: static_cast<const TrackContainer*>(Engine::patternStore());
			for (auto* track : container->tracks())
			{
				if (track->nodeWorkflowId() == trackId && sourceGraphAudioBus(*track)) { return QStringLiteral("audio"); }
			}
			return QString{};
		}
		if (scope.startsWith("source/") && id.startsWith("source-"))
		{
			const auto trackId = QUuid(scope.section('/', 1, 1));
			const auto parts = id.split('/');
			for (auto* container : {static_cast<TrackContainer*>(this), static_cast<TrackContainer*>(Engine::patternStore())})
			{
				for (auto* track : container->tracks())
				{
					if (track->nodeWorkflowId() != trackId) { continue; }
					if (parts.size() == 3 && parts[0] == "source-plugin" && QUuid(parts[1]) == trackId
						&& track->type() == Track::Type::Instrument)
					{
						return vstSignal(static_cast<InstrumentTrack*>(track)->instrument(), parts[2], output);
					}
					if (parts.size() == 3 && parts[0] == "source-layer")
					{
						for (const auto& layer : nodeInstrumentLayers(*track))
						{
							if (layer.id == parts[1]) { return vstSignal(layer.instrument->instrument(), parts[2], output); }
						}
					}
					if (parts.size() == 3 && parts[0] == "source-effect")
					{
						auto* bus = sourceGraphAudioBus(*track);
						auto* chain = bus ? bus->effects() : nullptr;
						for (int index = 0; chain && index < chain->effectCount(); ++index)
						{
							auto* effect = chain->effectAt(index);
							if (effect->property("nodeWorkflowSourceEffect").toString() == parts[1])
							{
								return vstSignal(effect, parts[2], output);
							}
						}
					}
					return QString{};
				}
			}
		}
		if (!output && id.startsWith("track-output/"))
		{
			const auto parts = id.split('/');
			if (parts.size() == 3 && scope == QStringLiteral("source/") + parts.at(1) + '/'
				&& (parts.at(2) == "in-L" || parts.at(2) == "in-R"))
			{
				for (auto* container : {static_cast<TrackContainer*>(this),
					static_cast<TrackContainer*>(Engine::patternStore())})
				{
					for (auto* track : container->tracks())
					{
						if (trackMixerChannelModel(*track) && track->nodeWorkflowId() == QUuid(parts.at(1)))
						{
							return QStringLiteral("audio");
						}
					}
				}
			}
		}
		if (output && scope == "root/" && id.startsWith("logical-midi/"))
		{
			for (const auto& input : nodeLogicalInputs())
			{
				if (id == QStringLiteral("logical-midi/") + input.id && input.signalType == "midi")
				{
					return QStringLiteral("midi");
				}
			}
		}
		if (!output && (scope == "root/" || scope == "song/" || scope == "pattern/") && id.startsWith("mixer/"))
		{
			const auto parts = id.split('/');
			if (parts.size() == 3 && (parts.at(2) == "in-L" || parts.at(2) == "in-R"))
			{
				const auto uuid = QUuid(parts.at(1));
				auto* mixer = Engine::mixer();
				for (mix_ch_t index = 0; index < mixer->numChannels(); ++index)
				{
					auto* channel = mixer->mixerChannel(index);
					const bool local = channel->m_nodeEffect && nodeEffectInPattern(uuid) == (scope == "pattern/");
					if (channel->nodeWorkflowId() == uuid && nodeCustomEffectScope(uuid).isEmpty()
						&& (scope == "root/" || local))
					{
						return QStringLiteral("audio");
					}
				}
			}
		}
		if (id.startsWith("instrument/"))
		{
			const auto parts = id.split('/');
			if (parts.size() == 3)
			{
				for (const auto& instrument : nodeCustomInstruments(scope))
				{
					if (instrument.first != parts.at(1))
					{
						continue;
					}
					if (output && (parts.at(2) == "out-L" || parts.at(2) == "out-R"))
					{
						return QStringLiteral("audio");
					}
					if (!output && parts.at(2) == "midi")
					{
						return QStringLiteral("midi");
					}
					return vstSignal(instrument.second->instrument(), parts.at(2), output);
				}
			}
			return QString{};
		}
		if (id.startsWith("effect/"))
		{
			const auto parts = id.split('/');
			if (parts.size() != 3) { return QString{}; }
			const auto index = effectChannel(id);
			if (index == 0) { return QString{}; }
			auto* channel = Engine::mixer()->mixerChannel(index);
			const auto uuid = QUuid(parts.at(1));
			const auto customScope = nodeCustomEffectScope(uuid);
			if (customScope == scope
				|| (customScope.isEmpty() && channel->m_nodeEffect
					&& scope == (nodeEffectInPattern(uuid) ? "pattern/" : "song/")))
			{
				if (!customScope.isEmpty() && (output ? (parts[2] == "out-L" || parts[2] == "out-R")
											  : (parts[2] == "in-L" || parts[2] == "in-R")))
				{
					return QStringLiteral("audio");
				}
				return vstSignal(channel->m_fxChain.effectAt(0), parts[2], output);
			}
			return QString{};
		}
		for (auto port = workflow.firstChildElement("boundary-port"); !port.isNull();
			port = port.nextSiblingElement("boundary-port"))
		{
			if (port.attribute("id") != id)
			{
				continue;
			}
			const auto graph = port.attribute("graph");
			const bool boundaryOutput = port.attribute("direction") == "out";
			if (graph == scope && boundaryOutput != output)
			{
				return port.attribute("signal");
			}
			if (childScopes.contains(graph) && boundaryOutput == output)
			{
				return port.attribute("signal");
			}
			break;
		}
		return QString{};
	};
	const auto signal = endpoint(source, true);
	if (signal.isEmpty() || signal != endpoint(destination, false))
	{
		return false;
	}
	if (!updateEffectSend(true))
	{
		return false;
	}
	auto edge = m_nodeWorkflow.createElement("custom-edge");
	edge.setAttribute("graph", scope);
	edge.setAttribute("source", source);
	edge.setAttribute("destination", destination);
	workflow.appendChild(edge);
	restoreCustomGraphAudioRoutes();
	setModified();
	return true;
}

std::optional<QPair<bool, int>> Song::nodeConnectionAggregation() const
{
	const auto workflow = m_nodeWorkflow.documentElement();
	bool valid = false;
	const auto threshold = workflow.attribute("aggregationThreshold").toInt(&valid);
	if (!valid || threshold < 1 || threshold > 9999)
	{
		return std::nullopt;
	}
	return qMakePair(workflow.attribute("aggregationEnabled") == "1", threshold);
}

bool Song::setNodeSourceMidiBoundary(const QString& id, Track& track)
{
	return setNodeSourceMidiBoundary(id, track, true);
}

bool Song::setNodeSourceMidiBoundary(const QString& id, Track& track, bool connected)
{
	if (!m_nodeProject || (track.type() != Track::Type::Instrument && track.type() != Track::Type::SVS))
	{
		return false;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const auto trackId = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	const auto scope = QStringLiteral("source/") + trackId + '/';
	auto workflow = m_nodeWorkflow.documentElement();
	QDomElement selected;
	for (auto port = workflow.firstChildElement("boundary-port"); !port.isNull();
		port = port.nextSiblingElement("boundary-port"))
	{
		if (port.attribute("graph") == scope && port.attribute("signal") == "midi"
			&& port.attribute("direction") == "in" && port.attribute("id") == id)
		{
			selected = port;
			break;
		}
	}
	if (selected.isNull())
	{
		return id.isEmpty() && connected && setNodeSourceMidiInputConnection(track, true);
	}
	if (connected)
	{
		selected.setAttribute("track", trackId);
		const auto updated = setNodeSourceMidiInputConnection(track, true);
		setModified();
		return updated;
	}
	selected.removeAttribute("track");
	bool anotherInput = false;
	for (auto port = workflow.firstChildElement("boundary-port"); !port.isNull();
		port = port.nextSiblingElement("boundary-port"))
	{
		if (port.attribute("graph") == scope && port.attribute("signal") == "midi"
			&& port.attribute("direction") == "in" && port.attribute("track") == trackId)
		{
			anotherInput = true;
			break;
		}
	}
	setNodeSourceMidiInputConnection(track, anotherInput);
	setModified();
	return true;
}

void Song::setNodeConnectionAggregation(bool enabled, int threshold)
{
	if (!m_nodeProject || threshold < 1 || threshold > 9999
		|| nodeConnectionAggregation() == qMakePair(enabled, threshold))
	{
		return;
	}
	auto workflow = ensureNodeWorkflow();
	workflow.setAttribute("aggregationEnabled", enabled ? "1" : "0");
	workflow.setAttribute("aggregationThreshold", threshold);
	setModified();
}

std::optional<bool> Song::nodePcbBusRouting() const
{
	const auto workflow = m_nodeWorkflow.documentElement();
	if (!workflow.hasAttribute("pcbBusRouting"))
	{
		return std::nullopt;
	}
	return workflow.attribute("pcbBusRouting") == "1";
}

void Song::setNodePcbBusRouting(bool enabled)
{
	if (!m_nodeProject || nodePcbBusRouting() == enabled)
	{
		return;
	}
	ensureNodeWorkflow().setAttribute("pcbBusRouting", enabled ? "1" : "0");
	setModified();
}

bool Song::setNodeBoundaryAudioSource(const QString& graph, const QString& id, Track& track,
	ch_cnt_t channel, bool connected)
{
	const auto* owner = track.trackContainer();
	const auto trackId = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	const bool sourceGraph = graph == QStringLiteral("source/") + trackId + '/'
		&& (track.type() == Track::Type::Instrument || track.type() == Track::Type::SVS
			|| track.type() == Track::Type::Sample);
	if (!m_nodeProject || channel >= 2 || !trackMixerChannelModel(track)
		|| (owner != this && owner != Engine::patternStore())
		|| (!sourceGraph && !((owner == this && graph == "song/")
			|| (owner == Engine::patternStore() && graph == "pattern/"))))
	{
		return false;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = m_nodeWorkflow.documentElement();
	QDomElement target;
	if (sourceGraph && id.isEmpty() && !connected)
	{
		for (auto port = workflow.firstChildElement("boundary-port"); !port.isNull();
			port = port.nextSiblingElement("boundary-port"))
		{
			if (port.attribute("graph") == graph && port.attribute("track") == trackId
				&& port.attribute("channel").toUInt() == channel)
			{
				port.removeAttribute("track");
				port.removeAttribute("channel");
				setModified();
			}
		}
		return true;
	}
	for (auto port = workflow.firstChildElement("boundary-port"); !port.isNull();
		port = port.nextSiblingElement("boundary-port"))
	{
		if (port.attribute("graph") == graph && port.attribute("id") == id
			&& port.attribute("signal") == "audio" && port.attribute("direction") == "out")
		{
			target = port;
			break;
		}
	}
	if (target.isNull() || (!target.attribute("track").isEmpty()
		&& (target.attribute("track") != trackId || target.attribute("channel").toUInt() != channel)))
	{
		return false;
	}
	if (!connected)
	{
		if (target.attribute("track") != trackId)
		{
			return false;
		}
		if (sourceGraph)
		{
			return setNodeSourceAudioOutput(track, channel, std::nullopt);
		}
		setNodeTrackBoundaryConnected(track, channel, false);
		return true;
	}
	for (auto port = workflow.firstChildElement("boundary-port"); !port.isNull();
		port = port.nextSiblingElement("boundary-port"))
	{
		if (port != target && port.attribute("graph") == graph && port.attribute("track") == trackId
			&& port.attribute("channel").toUInt() == channel)
		{
			port.removeAttribute("track");
			port.removeAttribute("channel");
		}
	}
	target.setAttribute("track", trackId);
	target.setAttribute("channel", static_cast<unsigned>(channel));
	if (!sourceGraph)
	{
		setNodeTrackBoundaryConnected(track, channel, true);
	}
	else
	{
		restoreCustomGraphAudioRoutes();
	}
	setModified();
	return true;
}

bool Song::nodeEffectInPattern(const QUuid& effect) const
{
	const auto id = effect.toString(QUuid::WithoutBraces);
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto owner = workflow.firstChildElement("effect-owner"); !owner.isNull();
		owner = owner.nextSiblingElement("effect-owner"))
	{
		if (owner.attribute("effect") == id)
		{
			return owner.attribute("graph") == "pattern";
		}
	}
	return false;
}

void Song::setNodeEffectInPattern(const QUuid& effect, bool pattern)
{
	auto workflow = ensureNodeWorkflow();
	const auto id = effect.toString(QUuid::WithoutBraces);
	for (auto owner = workflow.firstChildElement("effect-owner"); !owner.isNull();
		owner = owner.nextSiblingElement("effect-owner"))
	{
		if (owner.attribute("effect") == id)
		{
			owner.setAttribute("graph", pattern ? "pattern" : "song");
			return;
		}
	}
	auto owner = m_nodeWorkflow.createElement("effect-owner");
	owner.setAttribute("effect", id);
	owner.setAttribute("graph", pattern ? "pattern" : "song");
	workflow.appendChild(owner);
}

QDomElement Song::ensureNodeWorkflow()
{
	auto workflow = m_nodeWorkflow.documentElement();
	if (workflow.isNull())
	{
		workflow = m_nodeWorkflow.createElement("nodeworkflow");
		workflow.setAttribute("version", 1);
		m_nodeWorkflow.appendChild(workflow);
	}
	return workflow;
}

QDomElement Song::ensureNodeSourceSubgraph(Track& track)
{
	const auto* container = track.trackContainer();
	const bool singingVoice = track.type() == Track::Type::SVS;
	const bool sample = track.type() == Track::Type::Sample;
	if (!m_nodeProject || (container != this && container != Engine::patternStore())
		|| (!singingVoice && !sample && track.type() != Track::Type::Instrument))
	{
		return {};
	}

	auto workflow = ensureNodeWorkflow();
	const auto id = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	const auto kind = sample ? NodeWorkflowSourceKind::Sample
		: (singingVoice ? NodeWorkflowSourceKind::SingingVoice : NodeWorkflowSourceKind::Instrument);
	saveDefaultSourceSubgraph(workflow, id, kind);
	auto graph = workflow.firstChildElement("subgraph");
	while (!graph.isNull() && graph.attribute("track") != id)
	{
		graph = graph.nextSiblingElement("subgraph");
	}
	return graph;
}

const QList<Song::NodeInstrumentLayer>& Song::nodeInstrumentLayers(const Track& owner) const
{
	static const QList<NodeInstrumentLayer> empty;
	const auto layers = m_nodeInstrumentLayers.constFind(&owner);
	return layers == m_nodeInstrumentLayers.cend() ? empty : layers.value();
}

QDomElement Song::nodeInstrumentLayerElement(const QString& id) const
{
	auto layer = m_nodeWorkflow.documentElement().firstChildElement("instrument-layer");
	while (!layer.isNull() && layer.attribute("id") != id)
	{
		layer = layer.nextSiblingElement("instrument-layer");
	}
	return layer;
}

TrackContainer* Song::ensureNodeInstrumentTracks()
{
	if (!m_nodeInstrumentTracks)
	{
		class InstrumentLayerTracks final : public TrackContainer
		{
			QString nodeName() const override { return QStringLiteral("instrument-layer-tracks"); }
		};
		m_nodeInstrumentTracks = std::make_unique<InstrumentLayerTracks>();
		m_nodeInstrumentTracks->setType(TrackContainer::Type::Song);
	}
	return m_nodeInstrumentTracks.get();
}

InstrumentTrack* Song::createNodeInstrumentLayer(Track& owner, const QDomElement& element)
{
	auto* instrument = new InstrumentTrack(ensureNodeInstrumentTracks(), false);
	const auto layerId = element.attribute("id");
	const bool firstLayerForOwner = !m_nodeInstrumentLayers.contains(&owner);
	auto& layers = m_nodeInstrumentLayers[&owner];
	if (firstLayerForOwner)
	{
		connect(&owner, &QObject::destroyed, this, [this, owner = &owner] {
			const auto layers = m_nodeInstrumentLayers.take(owner);
			if (layers.isEmpty())
			{
				return;
			}
			const auto guard = Engine::audioEngine()->requestChangesGuard();
			for (const auto& layer : layers)
			{
				auto element = nodeInstrumentLayerElement(layer.id);
				element.parentNode().removeChild(element);
				delete layer.instrument;
			}
		});
	}
	NodeInstrumentLayer layer{layerId, instrument, element.attribute("notes") == "1",
		element.attribute("steps") == "1", {}};
	for (unsigned side = 0; side < layer.outputs.size(); ++side)
	{
		const int target = element.attribute(side == 0 ? "left" : "right", "-1").toInt();
		if (target == 0 || target == 1)
		{
			layer.outputs[side] = static_cast<ch_cnt_t>(target);
		}
	}
	layers.append(std::move(layer));
	return instrument;
}

InstrumentTrack* Song::addNodeInstrumentLayer(
	Track& owner, const QString& plugin, bool fromDrop, const QDomElement& preset)
{
	if (!m_nodeProject || owner.type() != Track::Type::Instrument || plugin.isEmpty())
	{
		return nullptr;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = ensureNodeWorkflow();
	auto element = m_nodeWorkflow.createElement("instrument-layer");
	element.setAttribute("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
	element.setAttribute("owner", owner.nodeWorkflowId().toString(QUuid::WithoutBraces));
	auto* instrument = createNodeInstrumentLayer(owner, element);
	if (preset.isNull())
	{
		instrument->loadInstrument(plugin, nullptr, fromDrop);
	}
	else
	{
		instrument->loadPreset(preset);
		auto* vst = dynamic_cast<vsthost::VstNodeOwner*>(instrument->instrument());
		if (!vst || !vst->vstNodeProcessor() || !vst->vstNodeProcessor()->presetStateRestored())
		{
			auto& layers = m_nodeInstrumentLayers[&owner];
			layers.removeLast();
			if (layers.isEmpty())
			{
				m_nodeInstrumentLayers.remove(&owner);
			}
			delete instrument;
			return nullptr;
		}
	}
	const auto baseName = instrument->name();
	auto name = baseName;
	const auto& layers = nodeInstrumentLayers(owner);
	for (int number = 2; name == owner.name() || std::any_of(layers.cbegin(), layers.cend(),
		[&](const auto& layer) { return layer.instrument != instrument && layer.instrument->name() == name; }); ++number)
	{
		name = QStringLiteral("%1 %2").arg(baseName).arg(number);
	}
	instrument->setName(name);
	workflow.appendChild(element);
	restoreNodeAudioRoutes();
	setModified();
	return instrument;
}

bool Song::setNodeInstrumentLayerEvent(Track& owner, const QString& id, bool step, bool connected)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto found = m_nodeInstrumentLayers.find(&owner);
	if (found == m_nodeInstrumentLayers.end())
	{
		return false;
	}
	for (auto& layer : found.value())
	{
		if (layer.id == id)
		{
			(step ? layer.steps : layer.notes) = connected;
			nodeInstrumentLayerElement(id).setAttribute(step ? "steps" : "notes", connected ? "1" : "0");
			setModified();
			return true;
		}
	}
	return false;
}

bool Song::setNodeInstrumentLayerOutput(Track& owner, const QString& id, ch_cnt_t source,
	std::optional<ch_cnt_t> target)
{
	if (source >= 2 || (target && *target >= 2))
	{
		return false;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto found = m_nodeInstrumentLayers.find(&owner);
	if (found == m_nodeInstrumentLayers.end())
	{
		return false;
	}
	for (auto& layer : found.value())
	{
		if (layer.id == id)
		{
			layer.outputs[source] = target;
			nodeInstrumentLayerElement(id).setAttribute(source == 0 ? "left" : "right",
				target ? static_cast<int>(*target) : -1);
			restoreNodeAudioRoutes();
			setModified();
			return true;
		}
	}
	return false;
}

void Song::removeNodeInstrumentLayer(Track& owner, const QString& id)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto found = m_nodeInstrumentLayers.find(&owner);
	if (found == m_nodeInstrumentLayers.end())
	{
		return;
	}
	auto& layers = found.value();
	const auto layer = std::find_if(layers.begin(), layers.end(), [&id](const auto& entry) { return entry.id == id; });
	if (layer != layers.end())
	{
		auto* instrument = layer->instrument;
		layers.erase(layer);
		auto element = nodeInstrumentLayerElement(id);
		element.parentNode().removeChild(element);
		delete instrument;
		setModified();
	}
}

QDomElement Song::copyNodeInstrumentLayer(Track& owner, const QString& id) const
{
	const auto& layers = nodeInstrumentLayers(owner);
	const auto layer = std::find_if(layers.cbegin(), layers.cend(), [&id](const auto& entry) {
		return entry.id == id;
	});
	auto* instrument = id.isEmpty() && owner.type() == Track::Type::Instrument
		? static_cast<InstrumentTrack*>(&owner)
		: (layer == layers.cend() ? nullptr : layer->instrument);
	if (!instrument || !instrument->instrument())
	{
		return {};
	}
	QDomDocument document;
	auto state = document.createElement("instrument-layer");
	document.appendChild(state);
	auto track = instrument->saveState(document, state);
	track.removeAttribute("nodeid");
	const auto children = track.elementsByTagName("journallingObject");
	while (children.count() > 0)
	{
		auto child = children.item(0);
		child.parentNode().removeChild(child);
	}
	return state;
}

QString Song::pasteNodeInstrumentLayer(Track& owner, const QDomElement& state)
{
	const auto track = state.firstChildElement("track");
	if (!m_nodeProject || owner.type() != Track::Type::Instrument || state.tagName() != "instrument-layer"
		|| track.isNull())
	{
		return {};
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = ensureNodeWorkflow();
	auto element = m_nodeWorkflow.importNode(state, true).toElement();
	const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	element.setAttribute("id", id);
	element.setAttribute("owner", owner.nodeWorkflowId().toString(QUuid::WithoutBraces));
	auto* instrument = createNodeInstrumentLayer(owner, element);
	instrument->restoreState(element.firstChildElement("track"));
	workflow.appendChild(element);
	restoreNodeAudioRoutes();
	setModified();
	return id;
}

QList<QPair<QString, InstrumentTrack*>> Song::nodeCustomInstruments(const QString& scope) const
{
	QList<QPair<QString, InstrumentTrack*>> result;
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto element = workflow.firstChildElement("custom-instrument"); !element.isNull();
		element = element.nextSiblingElement("custom-instrument"))
	{
		const auto id = element.attribute("id");
		if (element.attribute("graph") == scope)
		{
			if (auto* track = m_nodeCustomInstruments.value(id))
			{
				result.append(qMakePair(id, track));
			}
		}
	}
	return result;
}

QString Song::addNodeCustomInstrument(const QString& scope, const QString& plugin, const QString& file, bool fromDrop,
	const QDomElement& preset)
{
	if (!m_nodeProject || !scope.startsWith("custom/") || plugin.isEmpty()
		|| !allowsCustomInstruments(*this, m_nodeWorkflow.documentElement(), scope))
	{
		return {};
	}
	auto* track = new InstrumentTrack(ensureNodeInstrumentTracks(), false);
	track->audioBusHandle()->setMixerInputs(AudioBusHandle::MixerInputs{});
	Instrument* instance = nullptr;
	if (preset.isNull())
	{
		instance = track->loadInstrument(plugin, nullptr, fromDrop);
	}
	else
	{
		track->loadPreset(preset);
		auto* owner = dynamic_cast<vsthost::VstNodeOwner*>(track->instrument());
		if (owner && owner->vstNodeProcessor() && owner->vstNodeProcessor()->presetStateRestored())
		{
			instance = track->instrument();
		}
	}
	if (!instance)
	{
		delete track;
		return {};
	}
	if (!file.isEmpty())
	{
		instance->loadFile(file);
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	auto workflow = ensureNodeWorkflow();
	auto element = m_nodeWorkflow.createElement("custom-instrument");
	element.setAttribute("id", id);
	element.setAttribute("graph", scope);
	workflow.appendChild(element);
	m_nodeCustomInstruments.insert(id, track);
	setModified();
	return id;
}

QString Song::pasteNodeCustomInstrument(const QString& scope, const QDomElement& state)
{
	const auto settings = state.firstChildElement("track");
	if (!m_nodeProject || !scope.startsWith("custom/") || state.tagName() != "instrument-layer"
		|| settings.isNull() || !allowsCustomInstruments(*this, m_nodeWorkflow.documentElement(), scope))
	{
		return {};
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto* track = new InstrumentTrack(ensureNodeInstrumentTracks(), false);
	track->restoreState(settings);
	track->audioBusHandle()->setMixerInputs(AudioBusHandle::MixerInputs{});
	const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	auto workflow = ensureNodeWorkflow();
	auto element = m_nodeWorkflow.createElement("custom-instrument");
	element.setAttribute("id", id);
	element.setAttribute("graph", scope);
	workflow.appendChild(element);
	m_nodeCustomInstruments.insert(id, track);
	setModified();
	return id;
}

void Song::removeNodeCustomInstrument(const QString& scope, const QString& id)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = m_nodeWorkflow.documentElement();
	for (auto element = workflow.firstChildElement("custom-instrument"); !element.isNull();
		element = element.nextSiblingElement("custom-instrument"))
	{
		if (element.attribute("id") == id && element.attribute("graph") == scope)
		{
			workflow.removeChild(element);
			const auto prefix = QStringLiteral("instrument/") + id + '/';
			for (auto edge = workflow.firstChildElement("custom-edge"); !edge.isNull();)
			{
				const auto next = edge.nextSiblingElement("custom-edge");
				if (edge.attribute("source").startsWith(prefix) || edge.attribute("destination").startsWith(prefix))
				{
					workflow.removeChild(edge);
				}
				edge = next;
			}
			delete m_nodeCustomInstruments.take(id);
			setModified();
			return;
		}
	}
}

void Song::saveNodeInstrumentLayers()
{
	for (const auto& layers : std::as_const(m_nodeInstrumentLayers))
	{
		for (const auto& layer : layers)
		{
			auto element = nodeInstrumentLayerElement(layer.id);
			element.removeChild(element.firstChild());
			layer.instrument->saveState(m_nodeWorkflow, element);
		}
	}
	for (auto element = m_nodeWorkflow.documentElement().firstChildElement("custom-instrument");
		!element.isNull(); element = element.nextSiblingElement("custom-instrument"))
	{
		if (auto* track = m_nodeCustomInstruments.value(element.attribute("id")))
		{
			element.removeChild(element.firstChildElement("track"));
			track->saveState(m_nodeWorkflow, element);
		}
	}
}

void Song::restoreNodeInstrumentLayers()
{
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto element = workflow.firstChildElement("instrument-layer"); !element.isNull();
		element = element.nextSiblingElement("instrument-layer"))
	{
		const auto ownerId = QUuid(element.attribute("owner"));
		const auto state = element.firstChildElement("track");
		if (state.isNull() || ownerId.isNull() || element.attribute("id").isEmpty())
		{
			continue;
		}
		for (const auto* container : {static_cast<const TrackContainer*>(this),
			static_cast<const TrackContainer*>(Engine::patternStore())})
		{
			for (auto* track : container->tracks())
			{
				if (track->type() == Track::Type::Instrument && track->nodeWorkflowId() == ownerId)
				{
					createNodeInstrumentLayer(*track, element)->restoreState(state);
				}
			}
		}
	}
	for (auto element = workflow.firstChildElement("custom-instrument"); !element.isNull();
		element = element.nextSiblingElement("custom-instrument"))
	{
		const auto id = element.attribute("id");
		const auto state = element.firstChildElement("track");
		if (id.isEmpty() || state.isNull() || m_nodeCustomInstruments.contains(id))
		{
			continue;
		}
		auto* track = new InstrumentTrack(ensureNodeInstrumentTracks(), false);
		track->audioBusHandle()->setMixerInputs(AudioBusHandle::MixerInputs{});
		track->restoreState(state);
		m_nodeCustomInstruments.insert(id, track);
	}
}

bool Song::setNodeSourceAudioOutput(Track& track, ch_cnt_t sourceSide, std::optional<ch_cnt_t> outputSide)
{
	if (sourceSide >= 2 || (outputSide && *outputSide >= 2))
	{
		return false;
	}

	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto graph = ensureNodeSourceSubgraph(track);
	if (graph.isNull())
	{
		return false;
	}
	setSourceAudioOutput(graph, sourceSide, outputSide);
	restoreNodeAudioRoutes();
	setModified();
	return true;
}

bool Song::setNodeSourceMidiInputConnection(Track& track, bool connected)
{
	if (track.type() != Track::Type::Instrument && track.type() != Track::Type::SVS)
	{
		return false;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto graph = ensureNodeSourceSubgraph(track);
	if (graph.isNull())
	{
		return false;
	}

	if (sourceMidiInputConnected(graph) != connected)
	{
		setSourceMidiInputConnected(graph, connected);
		setModified();
	}
	return true;
}

bool Song::removeDefaultNodeSourceMidiInput(Track& track)
{
	if (track.type() != Track::Type::Instrument && track.type() != Track::Type::SVS)
	{
		return false;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto graph = ensureNodeSourceSubgraph(track);
	if (graph.isNull() || graph.attribute("defaultMidiInput") == "0")
	{
		return false;
	}
	const auto trackId = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	const auto ports = nodeBoundaryPorts(QStringLiteral("source/") + trackId + '/');
	const bool explicitInput = std::any_of(ports.cbegin(), ports.cend(), [&trackId](const auto& port) {
		return !port.output && port.signal == "midi" && port.track == trackId;
	});
	graph.setAttribute("defaultMidiInput", 0);
	setSourceMidiInputConnected(graph, explicitInput);
	setModified();
	return true;
}

Song::NodeSourceConnections Song::nodeSourceConnections(Track& track) const
{
	const auto trackId = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto graph = workflow.firstChildElement("subgraph"); !graph.isNull();
		graph = graph.nextSiblingElement("subgraph"))
	{
		if (graph.attribute("track") == trackId)
		{
			const auto kind = track.type() == Track::Type::SVS
				? NodeWorkflowSourceKind::SingingVoice : NodeWorkflowSourceKind::Instrument;
			return {sourceMidiInputConnected(graph), sourceEventConnected(graph, kind),
				graph.attribute("stepEvents", "1") != "0", graph.attribute("defaultMidiInput", "1") != "0",
				sourceAudioOutputs(graph)};
		}
	}
	return {true, true, true, true, {0u, 1u}};
}

bool Song::setNodeSourceStepConnection(Track& track, bool connected)
{
	if (track.type() != Track::Type::Instrument || track.trackContainer() != Engine::patternStore())
	{
		return false;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto graph = ensureNodeSourceSubgraph(track);
	if (graph.isNull())
	{
		return false;
	}
	if (track.nodeSourceStepConnected() != connected)
	{
		graph.setAttribute("stepEvents", connected ? "1" : "0");
		track.setNodeSourceStepConnected(connected);
		setModified();
	}
	return true;
}

bool Song::setNodeSourceEventConnection(Track& track, bool connected)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto graph = ensureNodeSourceSubgraph(track);
	if (graph.isNull())
	{
		return false;
	}
	if (track.nodeSourceEventConnected() == connected)
	{
		return true;
	}

	const bool singingVoice = track.type() == Track::Type::SVS;
	const auto kind = singingVoice ? NodeWorkflowSourceKind::SingingVoice : NodeWorkflowSourceKind::Instrument;
	setSourceEventConnected(graph, kind, connected);
	if (!connected)
	{
		Engine::audioEngine()->removePlayHandlesOfTypes(&track,
			singingVoice ? PlayHandle::Type::SVSPlayHandle : PlayHandle::Type::NotePlayHandle);
	}
	track.setNodeSourceEventConnected(connected);
	setModified();
	return true;
}

QList<Song::NodePianoRoll> Song::nodePianoRolls() const
{
	QList<NodePianoRoll> rolls;
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto roll = workflow.firstChildElement("piano-roll"); !roll.isNull();
		roll = roll.nextSiblingElement("piano-roll"))
	{
		const auto id = roll.attribute("id");
		const auto kind = roll.attribute("kind");
		if (!QUuid(id).isNull() && (kind == "midi" || kind == "svs"))
		{
			QStringList targets;
			for (auto target = roll.firstChildElement("target"); !target.isNull(); target = target.nextSiblingElement("target"))
			{
				targets.append(target.attribute("track"));
			}
			QStringList boundaryTargets;
			for (auto target = roll.firstChildElement("boundary-target"); !target.isNull();
				target = target.nextSiblingElement("boundary-target"))
			{
				boundaryTargets.append(target.attribute("port"));
			}
			rolls.append({id, kind == "svs", targets, boundaryTargets});
		}
	}
	return rolls;
}

void Song::addNodePianoRoll(bool singingVoice)
{
	if (!m_nodeProject)
	{
		return;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = ensureNodeWorkflow();
	auto roll = m_nodeWorkflow.createElement("piano-roll");
	roll.setAttribute("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
	roll.setAttribute("kind", singingVoice ? "svs" : "midi");
	workflow.appendChild(roll);
	setModified();
}

Clip* Song::nodePianoRollClip(const QString& id)
{
	if (auto* clip = m_nodePianoRollClips.value(id))
	{
		return clip;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const auto workflow = m_nodeWorkflow.documentElement();
	auto roll = workflow.firstChildElement("piano-roll");
	while (!roll.isNull() && roll.attribute("id") != id)
	{
		roll = roll.nextSiblingElement("piano-roll");
	}
	if (!m_nodeProject || roll.isNull())
	{
		return nullptr;
	}
	const auto wasModified = isModified();
	if (!m_nodePianoRollTracks)
	{
		class PianoRollTracks final : public TrackContainer
		{
			QString nodeName() const override { return QStringLiteral("standalone-piano-roll-tracks"); }
		};
		m_nodePianoRollTracks = std::make_unique<PianoRollTracks>();
		m_nodePianoRollTracks->setType(TrackContainer::Type::Song);
	}
	const bool singing = roll.attribute("kind") == "svs";
	auto* container = m_nodePianoRollTracks.get();
	Track* track = singing ? static_cast<Track*>(new SVSTrack(container))
		: static_cast<Track*>(new InstrumentTrack(container, false));
	track->setName(singing ? tr("Empty SVS Piano Roll") : tr("Empty MIDI Piano Roll"));
	auto* clip = track->createClip(TimePos{});
	const auto storedClip = roll.firstChildElement(clip->nodeName());
	if (!storedClip.isNull())
	{
		clip->restoreState(storedClip);
	}
	else if (!singing)
	{
		static_cast<MidiClip*>(clip)->setClipType(MidiClip::Type::MelodyClip);
	}
	m_nodePianoRollClips.insert(id, clip);
	connect(clip, &Clip::dataChanged, this, [this, clip, roll]() mutable {
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		auto storedClip = roll.firstChildElement(clip->nodeName());
		if (!storedClip.isNull())
		{
			roll.removeChild(storedClip);
		}
		clip->saveState(m_nodeWorkflow, roll);
		if (roll.attribute("kind") == "svs")
		{
			restoreNodeVoiceSource(roll);
		}
		setModified();
		updateLength();
	});
	setModified(wasModified);
	return clip;
}

bool Song::setNodePianoRollTarget(const QString& id, Track& track, bool connected)
{
	if (!m_nodeProject || track.trackContainer() != this
		|| (track.type() != Track::Type::Instrument && track.type() != Track::Type::SVS))
	{
		return false;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const auto trackId = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	const auto workflow = m_nodeWorkflow.documentElement();
	const auto kind = track.type() == Track::Type::SVS ? "svs" : "midi";
	for (auto roll = workflow.firstChildElement("piano-roll"); !roll.isNull(); roll = roll.nextSiblingElement("piano-roll"))
	{
		if (roll.attribute("id") != id || roll.attribute("kind") != kind)
		{
			continue;
		}
		for (auto target = roll.firstChildElement("target"); !target.isNull(); target = target.nextSiblingElement("target"))
		{
			if (target.attribute("track") == trackId)
			{
				if (!connected)
				{
					roll.removeChild(target);
					if (track.type() == Track::Type::SVS)
					{
							restoreNodeVoiceSource(roll);
					}
					setModified();
					updateLength();
				}
				return true;
			}
		}
		if (connected && nodePianoRollClip(id))
		{
			auto target = m_nodeWorkflow.createElement("target");
			target.setAttribute("track", trackId);
			roll.appendChild(target);
			if (track.type() == Track::Type::SVS)
			{
				restoreNodeVoiceSource(roll);
			}
			setModified();
			updateLength();
		}
		return true;
	}
	return false;
}

bool Song::setNodePianoRollBoundaryTarget(const QString& id, const QString& boundary, bool connected)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const auto ports = nodeBoundaryPorts(QStringLiteral("song/"));
	const auto port = std::find_if(ports.cbegin(), ports.cend(), [&boundary](const auto& endpoint) {
		return !endpoint.output && endpoint.id == boundary;
	});
	if (!m_nodeProject || port == ports.cend() || (port->signal != "voice" && port->signal != "midi"))
	{
		return false;
	}
	auto workflow = m_nodeWorkflow.documentElement();
	const auto kind = port->signal == "voice" ? QStringLiteral("svs") : QStringLiteral("midi");
	for (auto roll = workflow.firstChildElement("piano-roll"); !roll.isNull(); roll = roll.nextSiblingElement("piano-roll"))
	{
		if (roll.attribute("id") != id || roll.attribute("kind") != kind)
		{
			continue;
		}
		QDomElement match;
		for (auto node = roll.firstChildElement("boundary-target"); !node.isNull();
			node = node.nextSiblingElement("boundary-target"))
		{
			if (node.attribute("port") == boundary)
			{
				match = node;
				break;
			}
		}
		if (connected && match.isNull())
		{
			if (!nodePianoRollClip(id))
			{
				return false;
			}
			match = m_nodeWorkflow.createElement("boundary-target");
			match.setAttribute("port", boundary);
			roll.appendChild(match);
		}
		else if (!connected && !match.isNull())
		{
			roll.removeChild(match);
		}
		if (kind == "svs")
		{
			restoreNodeVoiceSource(roll);
		}
		setModified();
		updateLength();
		return true;
	}
	return false;
}

QDomElement Song::copyStandaloneNode(const QString& kind, const QString& id) const
{
	if (!m_nodeProject || (kind != "step-pattern" && kind != "piano-roll" && kind != "aggregate-input"))
	{
		return {};
	}
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto item = workflow.firstChildElement(kind); !item.isNull(); item = item.nextSiblingElement(kind))
	{
		if (item.attribute("id") == id)
		{
			QDomDocument document;
			return document.importNode(item, true).toElement();
		}
	}
	return {};
}

bool Song::pasteStandaloneNode(const QDomElement& state)
{
	const auto kind = state.tagName();
	if (!m_nodeProject || (kind != "step-pattern" && kind != "piano-roll" && kind != "aggregate-input"))
	{
		return false;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = ensureNodeWorkflow();
	auto copy = m_nodeWorkflow.importNode(state, true).toElement();
	copy.setAttribute("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
	const auto connectionTag = kind == "aggregate-input" ? QStringLiteral("destination") : QStringLiteral("target");
	for (auto endpoint = copy.firstChildElement(connectionTag); !endpoint.isNull();
		endpoint = copy.firstChildElement(connectionTag))
	{
		copy.removeChild(endpoint);
	}
	if (auto voice = copy.firstChildElement("svsclip"); !voice.isNull())
	{
		voice.removeChild(voice.firstChildElement("journallingObject"));
		voice.setAttribute("newEntity", 1);
	}
	if (auto midi = copy.firstChildElement("midiclip"); !midi.isNull())
	{
		midi.removeChild(midi.firstChildElement("journallingObject"));
	}
	workflow.appendChild(copy);
	setModified();
	return true;
}

bool Song::removeStandaloneNode(const QString& kind, const QString& id)
{
	if (!m_nodeProject || (kind != "step-pattern" && kind != "piano-roll" && kind != "aggregate-input"))
	{
		return false;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = m_nodeWorkflow.documentElement();
	for (auto item = workflow.firstChildElement(kind); !item.isNull(); item = item.nextSiblingElement(kind))
	{
		if (item.attribute("id") != id)
		{
			continue;
		}
		workflow.removeChild(item);
		if (kind == "piano-roll")
		{
			for (auto* track : tracks())
			{
				if (track->type() == Track::Type::SVS)
				{
					static_cast<SVSTrack*>(track)->setNodeVoiceSource(id, {});
				}
			}
			if (auto* clip = m_nodePianoRollClips.take(id))
			{
				delete clip->getTrack();
			}
		}
		setModified();
		updateLength();
		return true;
	}
	return false;
}

static QSet<QString> resolvedPianoRollTargets(const QDomElement& workflow, const QDomElement& roll)
{
	QSet<QString> tracks;
	for (auto target = roll.firstChildElement("target"); !target.isNull(); target = target.nextSiblingElement("target"))
	{
		tracks.insert(target.attribute("track"));
	}
	const bool singingVoice = roll.attribute("kind") == "svs";
	const auto signal = singingVoice ? QStringLiteral("voice") : QStringLiteral("midi");
	const auto inlet = singingVoice ? QStringLiteral("voice-in") : QStringLiteral("midi-in");
	for (auto target = roll.firstChildElement("boundary-target"); !target.isNull();
		target = target.nextSiblingElement("boundary-target"))
	{
		for (const auto& destination : nodeInputDestinations(workflow, target.attribute("port"), signal))
		{
			const auto id = destination.attribute("track");
			const auto port = destination.attribute("port");
			const auto boundary = singingVoice ? QDomElement{} : findNodeMidiBoundary(workflow, id, port);
			if (port == inlet || (!boundary.isNull() && boundary.attribute("track") == id))
			{
				tracks.insert(id);
			}
		}
	}
	return tracks;
}

void Song::restoreNodeVoiceSource(const QDomElement& roll)
{
	const auto state = roll.firstChildElement("svsclip");
	const auto targets = resolvedPianoRollTargets(m_nodeWorkflow.documentElement(), roll);
	for (auto* track : tracks())
	{
		if (track->type() == Track::Type::SVS)
		{
			const auto id = track->nodeWorkflowId().toString(QUuid::WithoutBraces);
			static_cast<SVSTrack*>(track)->setNodeVoiceSource(roll.attribute("id"),
				targets.contains(id) ? state : QDomElement{});
		}
	}
}

QList<Clip*> Song::nodePianoRollSources(Track& track) const
{
	QList<Clip*> clips;
	if (!m_nodeProject || m_nodePianoRollClips.isEmpty() || track.trackContainer() != this
		|| (track.type() != Track::Type::Instrument && track.type() != Track::Type::SVS))
	{
		return clips;
	}
	const auto trackId = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	const auto workflow = m_nodeWorkflow.documentElement();
	const auto kind = track.type() == Track::Type::SVS ? "svs" : "midi";
	for (auto roll = workflow.firstChildElement("piano-roll"); !roll.isNull(); roll = roll.nextSiblingElement("piano-roll"))
	{
		if (roll.attribute("kind") != kind)
		{
			continue;
		}
		if (resolvedPianoRollTargets(workflow, roll).contains(trackId))
		{
			if (auto* clip = m_nodePianoRollClips.value(roll.attribute("id")))
			{
				clips.append(clip);
			}
		}
	}
	return clips;
}

bool Song::isEmpty() const
{
	if (!TrackContainer::isEmpty())
	{
		return false;
	}
	for (auto* track : tracks())
	{
		for (const auto* source : nodePianoRollSources(*track))
		{
			if (const auto* midi = dynamic_cast<const MidiClip*>(source); midi && !midi->notes().empty())
			{
				return false;
			}
			if (const auto* svs = dynamic_cast<const SVSClip*>(source); svs && !svs->notes().isEmpty())
			{
				return false;
			}
		}
	}
	return true;
}

QList<Song::NodeStepPattern> Song::nodeStepPatterns() const
{
	QList<NodeStepPattern> patterns;
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto pattern = workflow.firstChildElement("step-pattern"); !pattern.isNull();
		pattern = pattern.nextSiblingElement("step-pattern"))
	{
		const auto id = pattern.attribute("id");
		if (!QUuid(id).isNull())
		{
			QStringList targets;
			QStringList boundaryTargets;
			for (auto target = pattern.firstChildElement("target"); !target.isNull();
				target = target.nextSiblingElement("target"))
			{
				targets.append(target.attribute("track"));
			}
			for (auto target = pattern.firstChildElement("boundary-target"); !target.isNull();
				target = target.nextSiblingElement("boundary-target"))
			{
				boundaryTargets.append(target.attribute("port"));
			}
			patterns.append({id, pattern.attribute("activeSteps").toUInt()
				& ((1u << NodeStepPattern::StepCount) - 1), targets, boundaryTargets});
		}
	}
	return patterns;
}

void Song::addNodeStepPattern()
{
	if (!m_nodeProject)
	{
		return;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = ensureNodeWorkflow();
	auto pattern = m_nodeWorkflow.createElement("step-pattern");
	pattern.setAttribute("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
	pattern.setAttribute("activeSteps", 0);
	workflow.appendChild(pattern);
	setModified();
}

bool Song::setNodeStep(const QString& id, int step, bool enabled)
{
	if (!m_nodeProject || step < 0 || step >= NodeStepPattern::StepCount)
	{
		return false;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = m_nodeWorkflow.documentElement();
	for (auto pattern = workflow.firstChildElement("step-pattern"); !pattern.isNull();
		pattern = pattern.nextSiblingElement("step-pattern"))
	{
		if (pattern.attribute("id") != id)
		{
			continue;
		}
		const auto active = pattern.attribute("activeSteps").toUInt();
		const auto bit = 1u << step;
		const auto updated = enabled ? active | bit : active & ~bit;
		if (updated != active)
		{
			pattern.setAttribute("activeSteps", updated);
			setModified();
		}
		return true;
	}
	return false;
}

bool Song::setNodeStepTarget(const QString& id, Track& track, bool connected)
{
	if (!m_nodeProject || track.type() != Track::Type::Instrument || track.trackContainer() != Engine::patternStore())
	{
		return false;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const auto targetId = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto pattern = workflow.firstChildElement("step-pattern"); !pattern.isNull();
		pattern = pattern.nextSiblingElement("step-pattern"))
	{
		if (pattern.attribute("id") != id)
		{
			continue;
		}
		for (auto target = pattern.firstChildElement("target"); !target.isNull();
			target = target.nextSiblingElement("target"))
		{
			if (target.attribute("track") == targetId)
			{
				if (!connected)
				{
					pattern.removeChild(target);
					setModified();
				}
				return true;
			}
		}
		if (connected)
		{
			auto target = m_nodeWorkflow.createElement("target");
			target.setAttribute("track", targetId);
			pattern.appendChild(target);
			setModified();
		}
		return true;
	}
	return false;
}

bool Song::setNodeStepBoundaryTarget(const QString& id, const QString& boundary, bool connected)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const auto ports = nodeBoundaryPorts(QStringLiteral("pattern/"));
	if (!m_nodeProject || std::none_of(ports.cbegin(), ports.cend(), [&boundary](const auto& port) {
		return !port.output && port.signal == "step" && port.id == boundary;
	}))
	{
		return false;
	}
	auto workflow = m_nodeWorkflow.documentElement();
	for (auto pattern = workflow.firstChildElement("step-pattern"); !pattern.isNull();
		pattern = pattern.nextSiblingElement("step-pattern"))
	{
		if (pattern.attribute("id") != id)
		{
			continue;
		}
		for (auto target = pattern.firstChildElement("boundary-target"); !target.isNull();
			target = target.nextSiblingElement("boundary-target"))
		{
			if (target.attribute("port") == boundary)
			{
				if (!connected)
				{
					pattern.removeChild(target);
					setModified();
				}
				return true;
			}
		}
		if (connected)
		{
			auto target = m_nodeWorkflow.createElement("boundary-target");
			target.setAttribute("port", boundary);
			pattern.appendChild(target);
			setModified();
		}
		return true;
	}
	return false;
}

bool Song::nodeStepActiveForTrack(Track& track, const TimePos& position) const
{
	if (!m_nodeProject)
	{
		return false;
	}
	const auto workflow = m_nodeWorkflow.documentElement();
	auto pattern = workflow.firstChildElement("step-pattern");
	if (pattern.isNull())
	{
		return false;
	}
	const auto step = (position.getTicks() * TimePos::stepsPerBar() + TimePos::ticksPerBar() - 1)
		/ TimePos::ticksPerBar();
	if (step < 0 || step >= NodeStepPattern::StepCount || TimePos::stepPosition(step) != position)
	{
		return false;
	}
	const auto targetId = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	for (; !pattern.isNull(); pattern = pattern.nextSiblingElement("step-pattern"))
	{
		if ((pattern.attribute("activeSteps").toUInt() & (1u << step)) == 0)
		{
			continue;
		}
		for (auto target = pattern.firstChildElement("target"); !target.isNull();
			target = target.nextSiblingElement("target"))
		{
			if (target.attribute("track") == targetId)
			{
				return true;
			}
		}
		for (auto target = pattern.firstChildElement("boundary-target"); !target.isNull();
			target = target.nextSiblingElement("boundary-target"))
		{
			for (const auto& endpoint : nodeInputDestinations(workflow, target.attribute("port"), "step"))
			{
				if (endpoint.attribute("track") == targetId && endpoint.attribute("port") == "step-in")
				{
					return true;
				}
			}
		}
	}
	return false;
}

QList<Song::NodeLogicalInput> Song::nodeLogicalInputs() const
{
	QList<NodeLogicalInput> result;
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto item = workflow.firstChildElement("aggregate-input"); !item.isNull();
		item = item.nextSiblingElement("aggregate-input"))
	{
		NodeLogicalInput input{item.attribute("id"), item.attribute("signal"), {}};
		bool reserved = false;
		for (auto port = workflow.firstChildElement("boundary-port"); !port.isNull();
			port = port.nextSiblingElement("boundary-port"))
		{
			if (port.attribute("id") == input.id)
			{
				reserved = true;
				break;
			}
		}
		if (reserved || input.id.isEmpty() || (input.signalType != "midi" && input.signalType != "audio"))
		{
			continue;
		}
		for (const auto& edge : nodeInputDestinations(workflow, input.id, input.signalType))
		{
			input.destinations.append(qMakePair(edge.attribute("track"), edge.attribute("port")));
		}
		result.append(std::move(input));
	}
	return result;
}

void Song::addNodeLogicalInput(const QString& signalType)
{
	if (!m_nodeProject || (signalType != "midi" && signalType != "audio"))
	{
		return;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto workflow = ensureNodeWorkflow();
	auto input = m_nodeWorkflow.createElement("aggregate-input");
	input.setAttribute("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
	input.setAttribute("signal", signalType);
	workflow.appendChild(input);
	setModified();
}

bool Song::setNodeLogicalInputTarget(const QString& inputId, const QString& signalType,
	Track& track, const QString& port, bool connected)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const auto type = track.type();
	const auto trackId = track.nodeWorkflowId().toString(QUuid::WithoutBraces);
	const bool midi = signalType == "midi" && (port == "midi-in"
		|| !findNodeMidiBoundary(m_nodeWorkflow.documentElement(), trackId, port).isNull())
		&& (type == Track::Type::Instrument || type == Track::Type::SVS);
	const bool audio = signalType == "audio" && (port == "audio-in:L" || port == "audio-in:R")
		&& type == Track::Type::Sample;
	const bool step = signalType == "step" && port == "step-in"
		&& type == Track::Type::Instrument && track.trackContainer() == Engine::patternStore();
	const bool voice = signalType == "voice" && port == "voice-in" && type == Track::Type::SVS;
	if (!m_nodeProject || inputId.isEmpty() || (!midi && !audio && !step && !voice))
	{
		return false;
	}
	const auto* expectedOwner = static_cast<const TrackContainer*>(this);
	const auto workflow = m_nodeWorkflow.documentElement();
	for (auto entry = workflow.firstChildElement("boundary-port"); !entry.isNull();
		entry = entry.nextSiblingElement("boundary-port"))
	{
		if (entry.attribute("id") != inputId)
		{
			continue;
		}
		const auto graph = entry.attribute("graph");
		if (entry.attribute("direction") != "in" || entry.attribute("signal") != signalType
			|| (graph != "song/" && graph != "pattern/"))
		{
			return false;
		}
		if (graph == "pattern/")
		{
			expectedOwner = Engine::patternStore();
		}
		break;
	}
	if (track.trackContainer() != expectedOwner)
	{
		return false;
	}

	if (!setNodeInputDestination(ensureNodeWorkflow(), inputId, signalType, trackId, port, connected))
	{
		return false;
	}
	if (voice)
	{
		for (auto roll = workflow.firstChildElement("piano-roll"); !roll.isNull();
			roll = roll.nextSiblingElement("piano-roll"))
		{
			if (roll.attribute("kind") != "svs")
			{
				continue;
			}
			for (auto edge = roll.firstChildElement("boundary-target"); !edge.isNull();
				edge = edge.nextSiblingElement("boundary-target"))
			{
				if (edge.attribute("port") == inputId)
				{
					restoreNodeVoiceSource(roll);
					break;
				}
			}
		}
	}
	if (voice || midi)
	{
		updateLength();
	}
	setModified();
	return true;
}

int Song::dispatchNodeMidiInput(const QString& inputId, const MidiEvent& event,
	const TimePos& time, f_cnt_t offset)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	if (!m_nodeProject || event.source() != MidiEvent::Source::Internal)
	{
		return 0;
	}
	if ((event.type() == MidiNoteOn || event.type() == MidiNoteOff || event.type() == MidiKeyPressure
		|| event.type() == MidiMetaEvent) && (event.key() < 0 || event.key() >= NumKeys))
	{
		return 0;
	}

	const bool noteRelease = event.type() == MidiNoteOff
		|| (event.type() == MidiNoteOn && event.velocity() == 0);
	auto trackIds = nodeInstrumentMidiTargets(m_nodeWorkflow.documentElement(), inputId, noteRelease);
	int delivered = 0;
	const auto workflow = m_nodeWorkflow.documentElement();
	QMultiHash<QString, QString> outgoing;
	for (auto edge = workflow.firstChildElement("custom-edge"); !edge.isNull();
		edge = edge.nextSiblingElement("custom-edge"))
	{
		outgoing.insert(edge.attribute("source"), edge.attribute("destination"));
	}
	QStringList pending{QStringLiteral("logical-midi/") + inputId};
	QSet<QString> visited;
	while (!pending.isEmpty())
	{
		const auto endpoint = pending.takeLast();
		if (visited.contains(endpoint))
		{
			continue;
		}
		visited.insert(endpoint);
		pending.append(outgoing.values(endpoint));
		if (endpoint.startsWith("instrument/") && endpoint.endsWith("/midi"))
		{
			const auto parts = endpoint.split('/');
			if (parts.size() == 3)
			{
				if (auto* instrument = m_nodeCustomInstruments.value(parts.at(1)))
				{
					instrument->processInEvent(event, time, offset);
					instrument->audioBusHandle()->markNodeEvent(noteRelease ? 2 : 3);
					++delivered;
				}
			}
		}
	}
	for (auto* container : {static_cast<TrackContainer*>(this), static_cast<TrackContainer*>(Engine::patternStore())})
	{
		for (auto* track : container->tracks())
		{
			if (track->type() == Track::Type::Instrument && track->nodeSourceEventConnected()
				&& trackIds.remove(track->nodeWorkflowId().toString(QUuid::WithoutBraces)))
			{
				auto* instrument = static_cast<InstrumentTrack*>(track);
				instrument->processInEvent(event, time, offset);
				instrument->audioBusHandle()->markNodeEvent(noteRelease ? 2 : 3);
				++delivered;
			}
		}
	}
	return delivered;
}

void Song::setNodeMasterOutputConnected(unsigned side, bool connected)
{
	if (!m_nodeProject || side >= 2)
	{
		return;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto* mixer = Engine::mixer();
	const auto bit = 1u << side;
	const auto current = mixer->nodeMasterOutputMask();
	const auto updated = connected ? current | bit : current & ~bit;
	if (updated != current)
	{
		ensureNodeWorkflow().setAttribute("masterOutputMask", updated);
		mixer->setNodeMasterOutputMask(updated);
		setModified();
	}
}

void Song::restoreNodeWorkflow(DataFile& dataFile)
{
	m_nodePianoRollClips.clear();
	m_nodePianoRollTracks.reset();
	m_nodeInstrumentLayers.clear();
	m_nodeCustomInstruments.clear();
	m_nodeInstrumentTracks.reset();
	setProjectWorkspace(m_nodeProject, dataFile.isNodeWorkflow());
	m_nodeWorkflow = QDomDocument{};
	m_deferredVstOutputs = {};
	m_vstTrackAudio.clear();
	const auto saved = dataFile.nodeWorkflow();
	if (!saved.isNull())
	{
		m_nodeWorkflow.appendChild(m_nodeWorkflow.importNode(saved, true));
	}
	bool validMask = false;
	const auto mask = saved.attribute("masterOutputMask").toUInt(&validMask);
	Engine::mixer()->setNodeMasterOutputMask(validMask && mask <= 3 ? mask : 3);
}

void Song::mixDeferredVstOutputs()
{
	if (m_nodeProject && m_deferredVstOutputs) { m_deferredVstOutputs(); }
}

void Song::publishVstTrackAudio(const AudioBusHandle* bus, const AudioBuffer& buffer) const
{
	if (!m_nodeProject) { return; }
	const auto route = m_vstTrackAudio.constFind(bus);
	if (route != m_vstTrackAudio.cend()) { (*route)(buffer); }
}

void Song::refreshVstNodePorts()
{
	if (!m_nodeProject) { return; }
	restoreCustomGraphAudioRoutes();
	emit nodeTrackRoutingChanged();
}

void Song::restoreCustomGraphAudioRoutes()
{
	if (!m_nodeProject)
	{
		return;
	}
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const auto workflow = m_nodeWorkflow.documentElement();
	QMultiHash<QString, QString> outgoing;
	for (auto edge = workflow.firstChildElement("custom-edge"); !edge.isNull();
		edge = edge.nextSiblingElement("custom-edge"))
	{
		outgoing.insert(edge.attribute("source"), edge.attribute("destination"));
	}
	auto* mixer = Engine::mixer();
	QHash<QString, QPair<mix_ch_t, ch_cnt_t>> destinations;
	QList<mix_ch_t> effects;
	for (mix_ch_t index = 0; index < mixer->numChannels(); ++index)
	{
		const auto id = mixer->mixerChannel(index)->nodeWorkflowId();
		const auto isCustomEffect = !nodeCustomEffectScope(id).isEmpty();
		if (isCustomEffect && index != 0)
		{
			effects.append(index);
		}
		const auto prefix = (isCustomEffect ? QStringLiteral("effect/") : QStringLiteral("mixer/"))
			+ id.toString(QUuid::WithoutBraces) + '/';
		destinations.insert(prefix + QStringLiteral("in-L"), qMakePair(index, ch_cnt_t{0}));
		destinations.insert(prefix + QStringLiteral("in-R"), qMakePair(index, ch_cnt_t{1}));
	}
	for (auto* container : {static_cast<TrackContainer*>(this), static_cast<TrackContainer*>(Engine::patternStore())})
	{
		for (auto* track : container->tracks())
		{
			if (!trackMixerChannelModel(*track))
			{
				continue;
			}
			const auto id = track->nodeWorkflowId().toString(QUuid::WithoutBraces);
			for (ch_cnt_t side = 0; side < 2; ++side)
			{
				const auto target = nodeTrackMixerEndpoint(*track, side);
				if (!target || !nodeTrackBoundaryConnected(*track, side))
				{
					continue;
				}
				const auto output = QStringLiteral("track-output/") + id
					+ (side == 0 ? QStringLiteral("/in-L") : QStringLiteral("/in-R"));
				destinations.insert(output, qMakePair(target->channel, target->side));
				for (const auto& boundary : nodeBoundaryPorts(QStringLiteral("source/") + id + '/'))
				{
					if (boundary.output && boundary.signal == "audio" && boundary.track == id
						&& boundary.channel == side && !outgoing.contains(boundary.id))
					{
						destinations.insert(boundary.id, qMakePair(target->channel, target->side));
					}
				}
			}
		}
	}
	const auto resolvedInputs = [&outgoing, &destinations](const QString& source) {
		QList<QPair<mix_ch_t, ch_cnt_t>> result;
		QStringList pending{source};
		QSet<QString> examined;
		while (!pending.isEmpty())
		{
			const auto endpoint = pending.takeLast();
			if (examined.contains(endpoint))
			{
				continue;
			}
			examined.insert(endpoint);
			const auto terminal = destinations.constFind(endpoint);
			if (terminal != destinations.cend() && !result.contains(*terminal))
			{
				result.append(*terminal);
			}
			pending.append(outgoing.values(endpoint));
		}
		std::sort(result.begin(), result.end());
		return result;
	};
	struct AudioEdge
	{
		std::array<std::vector<float>, 2> audio;
		std::array<long, 2> periods{-1, -1};
		std::array<std::size_t, 2> frames{};
	};
	struct MidiEdge
	{
		std::array<std::vector<vsthost::VstNodeMidiEvent>, 2> events;
		std::array<long, 2> periods{-1, -1};
	};
	struct VstRoutes
	{
		vsthost::VstNodeProcessor* processor;
		QString prefix;
		std::vector<vsthost::VstNodePort> ports;
		bool beforeMix;
		std::vector<std::pair<unsigned, std::shared_ptr<AudioEdge>>> audioInput, audioOutput;
		std::vector<std::pair<unsigned, std::shared_ptr<MidiEdge>>> midiInput, midiOutput;
	};
	std::vector<VstRoutes> vstNodes;
	const auto addVstNode = [&vstNodes](auto* plugin, const QString& prefix, bool beforeMix) {
		auto* owner = dynamic_cast<vsthost::VstNodeOwner*>(plugin);
		auto* processor = owner ? owner->vstNodeProcessor() : nullptr;
		if (processor) { vstNodes.push_back({processor, prefix, processor->nodePorts(), beforeMix}); }
	};
	for (auto* container : {static_cast<TrackContainer*>(this), static_cast<TrackContainer*>(Engine::patternStore())})
	{
		for (auto* track : container->tracks())
		{
			if (track->type() == Track::Type::Instrument)
			{
				const auto id = track->nodeWorkflowId().toString(QUuid::WithoutBraces);
				addVstNode(static_cast<InstrumentTrack*>(track)->instrument(), QStringLiteral("source-plugin/") + id + '/', true);
			}
			for (const auto& layer : nodeInstrumentLayers(*track))
			{
				addVstNode(layer.instrument->instrument(), QStringLiteral("source-layer/") + layer.id + '/', true);
			}
			if (auto* bus = sourceGraphAudioBus(*track))
			{
				for (const auto index : sourceGraphEffectSlots(*track))
				{
					auto* effect = bus->effects()->effectAt(index);
					const auto id = effect->property("nodeWorkflowSourceEffect").toString();
					addVstNode(effect, QStringLiteral("source-effect/") + id + '/', true);
				}
			}
		}
	}
	for (auto it = m_nodeCustomInstruments.cbegin(); it != m_nodeCustomInstruments.cend(); ++it)
	{
		addVstNode(it.value()->instrument(), QStringLiteral("instrument/") + it.key() + '/', true);
	}
	for (mix_ch_t index = 1; index < mixer->numChannels(); ++index)
	{
		auto* channel = mixer->mixerChannel(index);
		if (!channel->m_nodeEffect) { continue; }
		const auto id = channel->nodeWorkflowId().toString(QUuid::WithoutBraces);
		addVstNode(channel->m_fxChain.effectAt(0), QStringLiteral("effect/") + id + '/', false);
	}
	QHash<QString, std::pair<VstRoutes*, const vsthost::VstNodePort*>> vstInputs;
	for (auto& node : vstNodes)
	{
		for (const auto& port : node.ports)
		{
			if (port.available && port.direction == vsthost::NodePortDirection::Input)
			{
				vstInputs.insert(node.prefix + port.key(), {&node, &port});
			}
		}
	}
	const auto visitVstInputs = [&outgoing, &vstInputs](const QStringList& sources,
		vsthost::NodePortMedia media, auto consume) {
		QStringList pending;
		for (const auto& source : sources) { pending.append(outgoing.values(source)); }
		QSet<QString> visited;
		while (!pending.isEmpty())
		{
			const auto endpoint = pending.takeLast();
			if (visited.contains(endpoint)) { continue; }
			visited.insert(endpoint);
			const auto target = vstInputs.constFind(endpoint);
			if (target == vstInputs.cend())
			{
				pending.append(outgoing.values(endpoint));
				continue;
			}
			const auto [receiver, input] = *target;
			if (input->media == media) { consume(*receiver, *input); }
		}
	};
	const auto makeAudioEdge = [] {
		auto edge = std::make_shared<AudioEdge>();
		for (auto& block : edge->audio) { block.resize(MAXIMUM_BUFFER_SIZE); }
		return edge;
	};
	for (auto& node : vstNodes)
	{
		for (const auto& port : node.ports)
		{
			if (!port.available || port.direction != vsthost::NodePortDirection::Output) { continue; }
			visitVstInputs({node.prefix + port.key()}, port.media, [&](VstRoutes& receiver, const auto& input) {
				if (port.media == vsthost::NodePortMedia::Audio)
				{
					auto edge = makeAudioEdge();
					node.audioOutput.emplace_back(port.audioKey(), edge);
					receiver.audioInput.emplace_back(input.audioKey(), std::move(edge));
				}
				else
				{
					auto edge = std::make_shared<MidiEdge>();
					for (auto& block : edge->events) { block.reserve(vsthost::RealtimeMidiQueue::Capacity); }
					node.midiOutput.emplace_back(port.bus, edge);
					receiver.midiInput.emplace_back(input.bus, std::move(edge));
				}
			});
		}
	}
	m_vstTrackAudio.clear();
	for (auto* container : {static_cast<TrackContainer*>(this), static_cast<TrackContainer*>(Engine::patternStore())})
	{
		for (auto* track : container->tracks())
		{
			auto* bus = sourceGraphAudioBus(*track);
			if (!bus) { continue; }
			const auto id = track->nodeWorkflowId().toString(QUuid::WithoutBraces);
			const auto boundaries = nodeBoundaryPorts(QStringLiteral("source/") + id + '/');
			std::vector<std::pair<ch_cnt_t, std::shared_ptr<AudioEdge>>> routes;
			for (ch_cnt_t side = 0; side < 2; ++side)
			{
				QStringList sources{QStringLiteral("track-signal/") + id
					+ (side == 0 ? QStringLiteral("/out-L") : QStringLiteral("/out-R"))};
				for (const auto& boundary : boundaries)
				{
					if (boundary.output && boundary.signal == "audio" && boundary.track == id && boundary.channel == side)
					{
						sources.append(boundary.id);
					}
				}
				visitVstInputs(sources, vsthost::NodePortMedia::Audio, [&](VstRoutes& receiver, const auto& input) {
					auto edge = makeAudioEdge();
					routes.emplace_back(side, edge);
					receiver.audioInput.emplace_back(input.audioKey(), std::move(edge));
				});
			}
			if (routes.empty()) { continue; }
			m_vstTrackAudio.insert(bus, [routes = std::move(routes)](const AudioBuffer& buffer) {
				const auto source = buffer.groupBuffers(0);
				const auto period = Controller::runningPeriods();
				const auto slot = period & 1;
				for (const auto& [side, edge] : routes)
				{
					const auto frames = std::min<f_cnt_t>(source.frames(), edge->audio[slot].size());
					for (f_cnt_t frame = 0; frame < frames; ++frame)
					{
						edge->audio[slot][frame] = source.sample(side, frame);
					}
					edge->frames[slot] = frames;
					edge->periods[slot] = period;
				}
			});
		}
	}
	using DeferredOutput = std::pair<AudioBusHandle::MixerInput, std::shared_ptr<AudioEdge>>;
	std::vector<DeferredOutput> deferredOutputs;
	for (auto& node : vstNodes)
	{
		std::vector<std::pair<unsigned, AudioBusHandle::MixerInput>> routes;
		for (const auto& port : node.ports)
		{
			if (!port.available || port.direction != vsthost::NodePortDirection::Output
				|| port.media != vsthost::NodePortMedia::Audio)
			{
				continue;
			}
			for (const auto& [index, side] : resolvedInputs(node.prefix + port.key()))
			{
				auto* target = mixer->mixerChannel(index);
				AudioBusHandle::MixerInput input{target, &target->m_volumeModel, side};
				if (node.beforeMix)
				{
					routes.emplace_back(port.audioKey(), input);
				}
				else
				{
					auto edge = std::make_shared<AudioEdge>();
					for (auto& block : edge->audio) { block.resize(MAXIMUM_BUFFER_SIZE); }
					node.audioOutput.emplace_back(port.audioKey(), edge);
					deferredOutputs.emplace_back(input, std::move(edge));
				}
			}
		}
		vsthost::VstNodeRouting routing;
		if (!node.audioInput.empty())
		{
			routing.audioInput = [processor = node.processor, routes = std::move(node.audioInput)](
				std::span<float> samples, unsigned stride) {
				const auto period = Controller::runningPeriods() - 1;
				if (period < 0 || !stride) { return; }
				const auto slot = period & 1;
				for (const auto& [key, edge] : routes)
				{
					const auto channel = processor->nodeAudioIndex(key, vsthost::NodePortDirection::Input);
					if (channel >= stride || edge->periods[slot] != period) { continue; }
					const auto frames = std::min(samples.size() / stride, edge->frames[slot]);
					for (std::size_t frame = 0; frame < frames; ++frame)
					{
						samples[frame * stride + channel] += edge->audio[slot][frame];
					}
				}
			};
		}
		if (!node.midiInput.empty())
		{
			routing.midiInput = [routes = std::move(node.midiInput)](std::span<vsthost::VstNodeMidiEvent> input) {
				const auto period = Controller::runningPeriods() - 1;
				if (period < 0) { return std::size_t{0}; }
				const auto slot = period & 1;
				std::size_t count = 0;
				for (const auto& [bus, edge] : routes)
				{
					if (edge->periods[slot] != period) { continue; }
					for (auto event : edge->events[slot])
					{
						if (count == input.size()) { return count; }
						event.bus = bus;
						input[count++] = event;
					}
				}
				return count;
			};
		}
		if (!node.midiOutput.empty())
		{
			routing.midiOutput = [routes = std::move(node.midiOutput)](const vsthost::VstNodeMidiEvent& event) {
				const auto period = Controller::runningPeriods();
				const auto slot = period & 1;
				for (const auto& [bus, edge] : routes)
				{
					if (bus != event.bus) { continue; }
					auto& events = edge->events[slot];
					if (edge->periods[slot] != period)
					{
						events.clear();
						edge->periods[slot] = period;
					}
					if (events.size() < events.capacity()) { events.push_back(event); }
				}
			};
		}
		node.processor->setNodeRouting(std::move(routing));
		if (routes.empty() && node.audioOutput.empty())
		{
			node.processor->setNodeAudioReceiver({});
			continue;
		}
		node.processor->setNodeAudioReceiver([mixer, processor = node.processor,
			routes = std::move(routes), edges = std::move(node.audioOutput)](
			std::span<const float> samples, unsigned stride) {
			const auto period = Controller::runningPeriods();
			const auto slot = period & 1;
			for (const auto& [key, edge] : edges)
			{
				const auto channel = processor->nodeAudioIndex(key, vsthost::NodePortDirection::Output);
				if (channel >= stride) { continue; }
				auto& block = edge->audio[slot];
				const auto frames = std::min(block.size(), samples.size() / stride);
				for (std::size_t frame = 0; frame < frames; ++frame)
				{
					block[frame] = samples[frame * stride + channel];
				}
				edge->periods[slot] = period;
				edge->frames[slot] = frames;
			}
			for (const auto& [key, target] : routes)
			{
				if (const auto index = target.resolveChannel(*mixer, 0))
				{
					const auto source = processor->nodeAudioIndex(key, vsthost::NodePortDirection::Output);
					mixer->mixStridedMonoToChannel(samples, stride, source, *index, target.side);
				}
			}
		});
	}
	m_deferredVstOutputs = {};
	if (!deferredOutputs.empty())
	{
		m_deferredVstOutputs = [mixer, outputs = std::move(deferredOutputs)] {
			const auto period = Controller::runningPeriods() - 1;
			if (period < 0) { return; }
			const auto slot = period & 1;
			for (const auto& [target, edge] : outputs)
			{
				if (edge->periods[slot] != period) { continue; }
				if (const auto index = target.resolveChannel(*mixer, 0))
				{
					mixer->mixStridedMonoToChannel(std::span(edge->audio[slot]).first(edge->frames[slot]),
						1, 0, *index, target.side);
				}
			}
		};
	}
	for (auto it = m_nodeCustomInstruments.cbegin(); it != m_nodeCustomInstruments.cend(); ++it)
	{
		AudioBusHandle::MixerFanout inputs;
		for (ch_cnt_t side = 0; side < 2; ++side)
		{
			const auto endpoint = QStringLiteral("instrument/") + it.key()
				+ (side == 0 ? QStringLiteral("/out-L") : QStringLiteral("/out-R"));
			const auto targets = resolvedInputs(endpoint);
			for (const auto& [index, channel] : targets)
			{
				auto* receiver = mixer->mixerChannel(index);
				inputs[side].push_back({receiver, &receiver->m_volumeModel, channel});
			}
		}
		it.value()->audioBusHandle()->setMixerFanout(std::move(inputs));
	}

	QHash<mix_ch_t, QSet<QPair<mix_ch_t, unsigned>>> desiredSends;
	for (const auto index : effects)
	{
		const auto id = mixer->mixerChannel(index)->nodeWorkflowId().toString(QUuid::WithoutBraces);
		auto& desired = desiredSends[index];
		for (const auto side : {0u, 1u})
		{
			const auto output = QStringLiteral("effect/") + id
				+ (side == 0 ? QStringLiteral("/out-L") : QStringLiteral("/out-R"));
			for (const auto& [target, channel] : resolvedInputs(output))
			{
				desired.insert(qMakePair(target, side * 2 + static_cast<unsigned>(channel)));
			}
		}
		QList<QPair<mix_ch_t, unsigned>> stale;
		for (const auto* send : mixer->mixerChannel(index)->m_sends)
		{
			for (unsigned pair = 0; pair < 4; ++pair)
			{
				const auto key = qMakePair(send->receiverIndex(), pair);
				if ((send->channelPairs() & (1u << pair)) && !desired.contains(key))
				{
					stale.append(key);
				}
			}
		}
		for (const auto& [target, pair] : stale)
		{
			mixer->setNodeChannelSend(index, static_cast<ch_cnt_t>(pair / 2), target,
				static_cast<ch_cnt_t>(pair % 2), false);
		}
	}
	for (const auto index : effects)
	{
		for (const auto& [target, pair] : desiredSends.value(index))
		{
			mixer->setNodeChannelSend(index, static_cast<ch_cnt_t>(pair / 2), target,
				static_cast<ch_cnt_t>(pair % 2), true);
		}
	}
}

void Song::restoreNodeAudioRoutes()
{
	if (!m_nodeProject)
	{
		return;
	}
	for (const auto& roll : nodePianoRolls())
	{
		if (!roll.targets.isEmpty() || !roll.boundaryTargets.isEmpty())
		{
			nodePianoRollClip(roll.id);
		}
	}

	const auto workflow = m_nodeWorkflow.documentElement();
	QHash<QString, QDomElement> sourceGraphs;
	for (auto roll = workflow.firstChildElement("piano-roll"); !roll.isNull(); roll = roll.nextSiblingElement("piano-roll"))
	{
		if (roll.attribute("kind") == "svs")
		{
			restoreNodeVoiceSource(roll);
		}
	}
	for (auto graph = workflow.firstChildElement("subgraph"); !graph.isNull();
		graph = graph.nextSiblingElement("subgraph"))
	{
		sourceGraphs.insert(graph.attribute("track"), graph);
	}

	auto* mixer = Engine::mixer();
	QHash<QString, MixerChannel*> mixerChannels;
	QSet<QString> assignedEffects;
	for (auto owner = workflow.firstChildElement("effect-owner"); !owner.isNull();
		owner = owner.nextSiblingElement("effect-owner"))
	{
		assignedEffects.insert(owner.attribute("effect"));
	}
	QSet<mix_ch_t> songEffects;
	QSet<mix_ch_t> patternEffects;
	for (auto* container : {static_cast<TrackContainer*>(this), static_cast<TrackContainer*>(Engine::patternStore())})
	{
		auto& effects = container == this ? songEffects : patternEffects;
		for (auto* track : container->tracks())
		{
			for (ch_cnt_t side = 0; side < 2; ++side)
			{
				const auto route = nodeTrackMixerEndpoint(*track, side);
				if (route && mixer->mixerChannel(route->channel)->m_nodeEffect)
				{
					effects.insert(route->channel);
				}
			}
		}
		QList<mix_ch_t> pending(effects.begin(), effects.end());
		while (!pending.isEmpty())
		{
			const auto channel = pending.takeLast();
			for (const auto* send : mixer->mixerChannel(channel)->m_sends)
			{
				const auto destination = send->receiverIndex();
				if (send->receiver()->m_nodeEffect && !effects.contains(destination))
				{
					effects.insert(destination);
					pending.append(destination);
				}
			}
		}
	}
	for (mix_ch_t channel = 0; channel < mixer->numChannels(); ++channel)
	{
		auto* target = mixer->mixerChannel(channel);
		const auto id = target->nodeWorkflowId();
		const auto key = id.toString(QUuid::WithoutBraces);
		mixerChannels.insert(key, target);
		if (target->m_nodeEffect && !assignedEffects.contains(key))
		{
			setNodeEffectInPattern(id, patternEffects.contains(channel) && !songEffects.contains(channel));
		}
		if (target->m_nodeEffect)
		{
			target->setNodePatternEffect(nodeEffectInPattern(id));
		}
	}

	QHash<QString, AudioBusHandle*> buses;
	QHash<AudioBusHandle*, QDomElement> busSourceGraphs;
	for (auto* container : {static_cast<TrackContainer*>(this), static_cast<TrackContainer*>(Engine::patternStore())})
	{
		for (auto* track : container->tracks())
		{
			AudioBusHandle* bus = nullptr;
			switch (track->type())
			{
				case Track::Type::Instrument: bus = static_cast<InstrumentTrack*>(track)->audioBusHandle(); break;
				case Track::Type::Sample: bus = static_cast<SampleTrack*>(track)->audioBusHandle(); break;
				case Track::Type::SVS: bus = static_cast<SVSTrack*>(track)->audioBusHandle(); break;
				case Track::Type::SVC: bus = static_cast<SVCTrack*>(track)->audioBusHandle(); break;
				default: break;
			}
			if (bus)
			{
				const auto trackId = track->nodeWorkflowId().toString(QUuid::WithoutBraces);
				buses.insert(trackId, bus);
				const auto graph = sourceGraphs.value(trackId);
				if (!graph.isNull())
				{
					busSourceGraphs.insert(bus, graph);
				}
				if (track->type() == Track::Type::Instrument || track->type() == Track::Type::SVS)
				{
					const auto kind = track->type() == Track::Type::SVS
						? NodeWorkflowSourceKind::SingingVoice : NodeWorkflowSourceKind::Instrument;
					track->setNodeSourceEventConnected(graph.isNull() || sourceEventConnected(graph, kind));
					track->setNodeSourceStepConnected(graph.attribute("stepEvents", "1") != "0");
				}
			}
		}
	}

	const AudioBusHandle::MixerInputs defaultInputs{
		AudioBusHandle::MixerInput{nullptr, {}, 0}, AudioBusHandle::MixerInput{nullptr, {}, 1}};
	QHash<AudioBusHandle*, AudioBusHandle::MixerInputs> inputs;
	for (auto route = workflow.firstChildElement("track-audio-route"); !route.isNull();
		route = route.nextSiblingElement("track-audio-route"))
	{
		auto* bus = buses.value(route.attribute("track"));
		const auto source = route.attribute("side");
		if (!bus || (source != "L" && source != "R"))
		{
			continue;
		}

		auto found = inputs.find(bus);
		if (found == inputs.end())
		{
			found = inputs.insert(bus, defaultInputs);
		}
		auto& input = (*found)[source == "L" ? 0 : 1];
		const auto target = mixerChannels.constFind(route.attribute("mixer"));
		const auto side = route.attribute("input");
		if (target != mixerChannels.cend() && (side == "L" || side == "R"))
		{
			input = AudioBusHandle::MixerInput{*target, &(*target)->m_volumeModel,
				side == "L" ? ch_cnt_t{0} : ch_cnt_t{1}};
		}
		else
		{
			input.reset();
		}
	}
	for (auto item = workflow.firstChildElement("track-boundary-disconnect"); !item.isNull();
		item = item.nextSiblingElement("track-boundary-disconnect"))
	{
		auto* bus = buses.value(item.attribute("track"));
		const auto side = item.attribute("side");
		if (!bus || (side != "L" && side != "R"))
		{
			continue;
		}
		auto found = inputs.find(bus);
		if (found == inputs.end())
		{
			found = inputs.insert(bus, defaultInputs);
		}
		(*found)[side == "L" ? 0 : 1].reset();
	}
	for (auto* bus : std::as_const(buses))
	{
		const auto graph = busSourceGraphs.constFind(bus);
		const auto overrides = inputs.constFind(bus);
		if (graph == busSourceGraphs.cend() && overrides == inputs.cend())
		{
			bus->setMixerInputs(std::nullopt);
			continue;
		}

		const auto trackOutputs = overrides == inputs.cend() ? defaultInputs : *overrides;
		if (graph == busSourceGraphs.cend())
		{
			bus->setMixerInputs(trackOutputs);
			continue;
		}

		const auto sides = sourceAudioOutputs(*graph);
		AudioBusHandle::MixerInputs resolved{};
		for (std::size_t source = 0; source < sides.size(); ++source)
		{
			if (const auto output = sides[source])
			{
				resolved[source] = trackOutputs[*output];
			}
		}
		bus->setMixerInputs(resolved);
	}
	for (auto it = m_nodeInstrumentLayers.cbegin(); it != m_nodeInstrumentLayers.cend(); ++it)
	{
		auto* owner = static_cast<const InstrumentTrack*>(it.key());
		auto* ownerBus = const_cast<InstrumentTrack*>(owner)->audioBusHandle();
		const auto trackOutputs = inputs.value(ownerBus, defaultInputs);
		for (const auto& layer : it.value())
		{
			AudioBusHandle::MixerInputs resolved{};
			for (unsigned source = 0; source < layer.outputs.size(); ++source)
			{
				if (const auto target = layer.outputs[source])
				{
					resolved[source] = trackOutputs[*target];
				}
			}
			auto* bus = layer.instrument->audioBusHandle();
			bus->setNextMixerChannel(ownerBus->nextMixerChannel());
			bus->setMixerInputs(resolved);
		}
	}
	restoreCustomGraphAudioRoutes();
}

void Song::saveProjectState(DataFile& dataFile)
{
	using gui::getGUI;

	// Transient state belongs in journal snapshots, never in saved project files.
	auto runtime = dataFile.createElement("journalRuntime");
	runtime.setAttribute("mode", static_cast<int>(m_playMode));
	runtime.setAttribute("playing", m_playing);
	runtime.setAttribute("paused", m_paused);
	runtime.setAttribute("recording", m_recording);
	runtime.setAttribute("fileName", m_fileName);
	runtime.setAttribute("oldFileName", m_oldFileName);
	runtime.setAttribute("loopMidiClip", m_loopMidiClip);
	runtime.setAttribute("currentPattern", Engine::patternStore()->currentPattern());
	if (const auto* clip = previewClip())
	{
		const auto* track = clip->getTrack();
		const auto* container = track->trackContainer();
		const auto& containerTracks = container->tracks();
		const auto& clips = track->getClips();
		runtime.setAttribute("previewParent", container == this ? "song" : "pattern");
		runtime.setAttribute("previewTrack",
			static_cast<int>(std::distance(
				containerTracks.begin(), std::find(containerTracks.begin(), containerTracks.end(), track))));
		runtime.setAttribute(
			"previewClip", static_cast<int>(std::distance(clips.begin(), std::find(clips.begin(), clips.end(), clip))));
	}
	for (std::size_t index = 0; index < PlayModeCount; ++index)
	{
		const auto& timeline = m_timelines[index];
		auto position = dataFile.createElement("position");
		position.setAttribute("mode", static_cast<int>(index));
		position.setAttribute("ticks", timeline.ticks());
		position.setAttribute("frameOffset", timeline.frameOffset());
		position.setAttribute("seconds", QString::number(timeline.getElapsedSeconds(), 'g', 17));
		position.setAttribute("playStart", timeline.playStartPosition().getTicks());
		runtime.appendChild(position);
	}
	dataFile.content().appendChild(runtime);

	if (m_nodeProject)
	{
		saveNodeInstrumentLayers();
		dataFile.setNodeWorkflow(m_nodeWorkflow.documentElement());
	}
	m_tempoModel.saveSettings(dataFile, dataFile.head(), "bpm");
	m_timeSigModel.saveSettings(dataFile, dataFile.head(), "timesig");
	m_masterVolumeModel.saveSettings(dataFile, dataFile.head(), "mastervol");
	m_masterPitchModel.saveSettings(dataFile, dataFile.head(), "masterpitch");

	saveState(dataFile, dataFile.content());
	removeUnownedTrackGraphData(dataFile.nodeWorkflow(), dataFile.content());
	Engine::mixer()->saveState(dataFile, dataFile.content());
	getTimeline(PlayMode::Song).saveState(dataFile, dataFile.content());
	if (getGUI() != nullptr)
	{
		getGUI()->getControllerRackView()->saveState(dataFile, dataFile.content());
		getGUI()->pianoRoll()->saveState(dataFile, dataFile.content());
		getGUI()->automationEditor()->m_editor->saveState(dataFile, dataFile.content());
		getGUI()->getProjectNotes()->SerializingObject::saveState(dataFile, dataFile.content());
	}

	saveControllerStates(dataFile, dataFile.content());
	saveScaleStates(dataFile, dataFile.content());
	saveKeymapStates(dataFile, dataFile.content());
}

void Song::restoreProjectState(DataFile& dataFile)
{
#ifdef WANT_AGENT
	if (agent::CommandBus::instance().agentExecutionActive())
	{
		throw agent::AgentProjectResetError();
	}
#endif
	using gui::getGUI;
	const bool wasLoading = m_loadingProject;
	const QScopedValueRollback<bool> loadingGuard(m_loadingProject, true);

	QDomNode node;
	clearProject();
	clearErrors();
	Engine::audioEngine()->requestChangeInModel();

	restoreNodeWorkflow(dataFile);
	m_tempoModel.loadSettings(dataFile.head(), "bpm");
	m_timeSigModel.loadSettings(dataFile.head(), "timesig");
	m_masterVolumeModel.loadSettings(dataFile.head(), "mastervol");
	m_masterPitchModel.loadSettings(dataFile.head(), "masterpitch");
	getTimeline(PlayMode::Song).setLoopEnabled(false);
	PeakController::initGetControllerBySetting();

	node = dataFile.content().firstChildElement(Engine::mixer()->nodeName());
	if (!node.isNull())
	{
		Engine::mixer()->restoreState(node.toElement());
		if (getGUI() != nullptr)
		{
			getGUI()->mixerView()->refreshDisplay();
		}
	}

	node = dataFile.content().firstChild();
	QDomNodeList trackContainers = dataFile.content().elementsByTagName("trackcontainer");
	m_nLoadingTrack = 0;
	for (int index = 0, count = trackContainers.count(); index < count; ++index)
	{
		QDomNode trackNode = trackContainers.at(index).firstChild();
		while (!trackNode.isNull())
		{
			if (trackNode.isElement() && trackNode.nodeName() == "track")
			{
				++m_nLoadingTrack;
				if (static_cast<Track::Type>(trackNode.toElement().attribute("type").toInt()) == Track::Type::Pattern)
				{
					count += trackNode.toElement()
								 .elementsByTagName("patterntrack")
								 .at(0)
								 .toElement()
								 .firstChildElement()
								 .childNodes()
								 .count();
				}
			}
			trackNode = trackNode.nextSibling();
		}
	}

	while (!node.isNull())
	{
		if (node.isElement())
		{
			if (node.nodeName() == "trackcontainer")
			{
				static_cast<JournallingObject*>(this)->restoreState(node.toElement());
			}
			else if (node.nodeName() == "controllers")
			{
				restoreControllerStates(node.toElement());
			}
			else if (node.nodeName() == "scales")
			{
				restoreScaleStates(node.toElement());
			}
			else if (node.nodeName() == "keymaps")
			{
				restoreKeymapStates(node.toElement());
			}
			else if (node.nodeName() == getTimeline(PlayMode::Song).nodeName())
			{
				getTimeline(PlayMode::Song).restoreState(node.toElement());
			}
			else if (getGUI() != nullptr)
			{
				if (node.nodeName() == getGUI()->getControllerRackView()->nodeName())
				{
					getGUI()->getControllerRackView()->restoreState(node.toElement());
				}
				else if (node.nodeName() == getGUI()->pianoRoll()->nodeName())
				{
					getGUI()->pianoRoll()->restoreState(node.toElement());
				}
				else if (node.nodeName() == getGUI()->automationEditor()->m_editor->nodeName())
				{
					getGUI()->automationEditor()->m_editor->restoreState(node.toElement());
				}
				else if (node.nodeName() == getGUI()->getProjectNotes()->nodeName())
				{
					getGUI()->getProjectNotes()->SerializingObject::restoreState(node.toElement());
				}
			}
		}
		node = node.nextSibling();
	}

	Engine::patternStore()->fixIncorrectPositions();
	ControllerConnection::finalizeConnections();
	m_controllers.erase(
		std::remove_if(m_controllers.begin(), m_controllers.end(),
			[](Controller* controller) { return controller->type() == Controller::ControllerType::Dummy; }),
		m_controllers.end());
	AutomationClip::resolveAllIDs();

	const auto runtime = dataFile.content().firstChildElement("journalRuntime");
	if (!runtime.isNull())
	{
		for (auto position = runtime.firstChildElement("position"); !position.isNull();
			position = position.nextSiblingElement("position"))
		{
			const int mode = position.attribute("mode").toInt();
			if (mode >= 0 && mode < static_cast<int>(PlayModeCount))
			{
				m_timelines[mode].restorePlaybackPosition(position.attribute("ticks").toInt(),
					position.attribute("frameOffset").toFloat(), position.attribute("seconds").toDouble(),
					TimePos(position.attribute("playStart").toInt()));
			}
		}
		setProjectFileName(runtime.attribute("fileName"));
		m_oldFileName = runtime.attribute("oldFileName");
		m_playMode = static_cast<PlayMode>(runtime.attribute("mode").toInt());
		m_playing = runtime.attribute("playing").toInt() != 0;
		m_paused = runtime.attribute("paused").toInt() != 0;
		m_recording = runtime.attribute("recording").toInt() != 0;
		m_loopMidiClip = runtime.attribute("loopMidiClip").toInt() != 0;
		const int pattern = runtime.attribute("currentPattern").toInt();
		if (pattern >= 0 && pattern < Engine::patternStore()->numOfPatterns())
		{
			Engine::patternStore()->setCurrentPattern(pattern);
		}
		m_noteClipToPlay = nullptr;
		m_automationClipToPlay = nullptr;
		if (m_playMode == PlayMode::MidiClip || m_playMode == PlayMode::AutomationClip)
		{
			const auto& previewTracks
				= runtime.attribute("previewParent") == "song" ? tracks() : Engine::patternStore()->tracks();
			const int trackIndex = runtime.attribute("previewTrack", "-1").toInt();
			const int clipIndex = runtime.attribute("previewClip", "-1").toInt();
			if (trackIndex >= 0 && trackIndex < static_cast<int>(previewTracks.size()))
			{
				const auto& clips = previewTracks[trackIndex]->getClips();
				if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size()))
				{
					if (m_playMode == PlayMode::MidiClip)
					{
						m_noteClipToPlay
							= dynamic_cast<MidiClip*>(clips[clipIndex]) || dynamic_cast<SVSClip*>(clips[clipIndex])
							? clips[clipIndex]
							: nullptr;
					}
					else
					{
						m_automationClipToPlay = dynamic_cast<AutomationClip*>(clips[clipIndex]);
					}
				}
			}
			if (previewClip() == nullptr)
			{
				m_playing = false;
				m_paused = false;
				m_playMode = PlayMode::None;
			}
		}
		m_vstSyncController.setPlaybackState(m_playing && !m_paused);
		emit playbackStateChanged();
	}
	restoreNodeInstrumentLayers();
	restoreNodeAudioRoutes();
	Engine::audioEngine()->doneChangeInModel();
	m_loadingProject = wasLoading;
	updateLength();
}

// load given song
void Song::loadProject( const QString & fileName )
{
#ifdef WANT_AGENT
	if (agent::CommandBus::instance().agentExecutionActive())
	{
		throw agent::AgentProjectResetError();
	}
#endif
	using gui::getGUI;

	QDomNode node;

	const QScopedValueRollback<bool> loadingGuard(m_loadingProject, true);
	const bool wasJournalling = Engine::projectJournal()->isJournalling();

	Engine::projectJournal()->setJournalling( false );

	m_oldFileName = m_fileName;
	setProjectFileName(fileName);

	DataFile dataFile( m_fileName );

	bool cantLoadProject = false;
	// if file could not be opened, head-node is null and we create
	// new project
	if( dataFile.head().isNull() )
	{
		cantLoadProject = true;
	}
	else
	{
		// We check if plugins contain local paths to prevent malicious code being
		// added to project bundles and loaded with "local:" paths
		if (dataFile.hasLocalPlugins())
		{
			cantLoadProject = true;

			if (getGUI() != nullptr)
			{
				QMessageBox::critical(nullptr, tr("Aborting project load"),
					tr("Project file contains local paths to plugins, which could be used to "
						"run malicious code."));
			}
			else
			{
				QTextStream(stderr) << tr("Can't load project: "
					"Project file contains local paths to plugins.")
					<< Qt::endl;
			}
		}
	}

	if (cantLoadProject)
	{
		if( m_loadOnLaunch )
		{
			createNewProject();
		}
		setProjectFileName(m_oldFileName);
		Engine::projectJournal()->setJournalling(wasJournalling);
		return;
	}

	m_oldFileName = m_fileName;

	clearProject();

	clearErrors();

	Engine::audioEngine()->requestChangeInModel();

	// get the header information from the DOM
	restoreNodeWorkflow(dataFile);
	m_tempoModel.loadSettings( dataFile.head(), "bpm" );
	m_timeSigModel.loadSettings( dataFile.head(), "timesig" );
	m_masterVolumeModel.loadSettings( dataFile.head(), "mastervol" );
	m_masterPitchModel.loadSettings( dataFile.head(), "masterpitch" );

	getTimeline(PlayMode::Song).setLoopEnabled(false);

	//Backward compatibility for LMMS <= 0.4.15
	PeakController::initGetControllerBySetting();

	// Load mixer first to be able to set the correct range for mixer channels
	node = dataFile.content().firstChildElement( Engine::mixer()->nodeName() );
	if( !node.isNull() )
	{
		Engine::mixer()->restoreState( node.toElement() );
		if( getGUI() != nullptr )
		{
			// refresh MixerView
			getGUI()->mixerView()->refreshDisplay();
		}
	}

	node = dataFile.content().firstChild();

	QDomNodeList tclist=dataFile.content().elementsByTagName("trackcontainer");
	m_nLoadingTrack=0;
	for( int i=0,n=tclist.count(); i<n; ++i )
	{
		QDomNode nd=tclist.at(i).firstChild();
		while(!nd.isNull())
		{
			if( nd.isElement() && nd.nodeName() == "track" )
			{
				++m_nLoadingTrack;
				if (static_cast<Track::Type>(nd.toElement().attribute("type").toInt()) == Track::Type::Pattern)
				{
					n += nd.toElement().elementsByTagName("patterntrack").at(0)
						.toElement().firstChildElement().childNodes().count();
				}
			}
			nd = nd.nextSibling();
		}
	}

	while( !node.isNull() && !isCancelled() )
	{
		if( node.isElement() )
		{
			if( node.nodeName() == "trackcontainer" )
			{
				( (JournallingObject *)( this ) )->restoreState( node.toElement() );
			}
			else if( node.nodeName() == "controllers" )
			{
				restoreControllerStates( node.toElement() );
			}
			else if (node.nodeName() == "scales")
			{
				restoreScaleStates(node.toElement());
			}
			else if (node.nodeName() == "keymaps")
			{
				restoreKeymapStates(node.toElement());
			}
			else if (node.nodeName() == getTimeline(PlayMode::Song).nodeName())
			{
				getTimeline(PlayMode::Song).restoreState(node.toElement());
			}
			else if( getGUI() != nullptr )
			{
				if( node.nodeName() == getGUI()->getControllerRackView()->nodeName() )
				{
					getGUI()->getControllerRackView()->restoreState( node.toElement() );
				}
				else if( node.nodeName() == getGUI()->pianoRoll()->nodeName() )
				{
					getGUI()->pianoRoll()->restoreState( node.toElement() );
				}
				else if( node.nodeName() == getGUI()->automationEditor()->m_editor->nodeName() )
				{
					getGUI()->automationEditor()->m_editor->restoreState( node.toElement() );
				}
				else if( node.nodeName() == getGUI()->getProjectNotes()->nodeName() )
				{
					 getGUI()->getProjectNotes()->SerializingObject::restoreState( node.toElement() );
				}
			}
		}
		node = node.nextSibling();
	}

	// quirk for fixing projects with broken positions of Clips inside pattern tracks
	Engine::patternStore()->fixIncorrectPositions();

	// Connect controller links to their controllers
	// now that everything is loaded
	ControllerConnection::finalizeConnections();

	// Remove dummy controllers that was added for correct connections
	m_controllers.erase(std::remove_if(m_controllers.begin(), m_controllers.end(),
		[](Controller* c){return c->type() == Controller::ControllerType::Dummy;}),
		m_controllers.end());

	restoreNodeInstrumentLayers();
	// Resolve automation after the nested plugin models have been restored.
	AutomationClip::resolveAllIDs();
	restoreNodeAudioRoutes();

	Engine::audioEngine()->doneChangeInModel();

	ConfigManager::inst()->addRecentlyOpenedProject( fileName );

	Engine::projectJournal()->setJournalling( true );

	emit projectLoaded();

	if( isCancelled() )
	{
		m_isCancelled = false;
		createNewProject();
		return;
	}

	if ( hasErrors())
	{
		if ( getGUI() != nullptr )
		{
			QMessageBox::warning( nullptr, tr("LMMS Error report"), errorSummary(),
							QMessageBox::Ok );
		}
		else
		{
			QTextStream(stderr) << Engine::getSong()->errorSummary() << Qt::endl;
		}
	}

	m_loadingProject = false;
	updateLength();
	setModified(false);
	m_loadOnLaunch = false;
}


// only save current song as filename and do nothing else
bool Song::saveProjectFile(const QString & filename, bool withResources)
{
	return saveProjectFile(filename, withResources, m_nodeProject);
}

bool Song::saveProjectFile(const QString& filename, bool withResources, bool nodeFormat)
{
	using gui::getGUI;
	const auto suffix = QFileInfo(filename).suffix().toLower();
	if ((!nodeFormat || suffix == "mmp" || suffix == "mmpz") && hasExtendedSourceGraphs())
	{
		return false;
	}

#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
	qSetGlobalQHashSeed(0);
#else
	QHashSeed::setDeterministicGlobalSeed();
#endif

	DataFile dataFile( DataFile::Type::SongProject );
	m_savingProject = true;

	if (nodeFormat)
	{
		saveNodeInstrumentLayers();
		dataFile.setNodeWorkflow(m_nodeWorkflow.documentElement());
	}
	m_tempoModel.saveSettings( dataFile, dataFile.head(), "bpm" );
	m_timeSigModel.saveSettings( dataFile, dataFile.head(), "timesig" );
	m_masterVolumeModel.saveSettings( dataFile, dataFile.head(), "mastervol" );
	m_masterPitchModel.saveSettings( dataFile, dataFile.head(), "masterpitch" );

	saveState( dataFile, dataFile.content() );
	removeUnownedTrackGraphData(dataFile.nodeWorkflow(), dataFile.content());
	getTimeline(PlayMode::Song).saveState(dataFile, dataFile.content());

	Engine::mixer()->saveState( dataFile, dataFile.content() );
	if( getGUI() != nullptr )
	{
		getGUI()->getControllerRackView()->saveState( dataFile, dataFile.content() );
		getGUI()->pianoRoll()->saveState( dataFile, dataFile.content() );
		getGUI()->automationEditor()->m_editor->saveState( dataFile, dataFile.content() );
		getGUI()->getProjectNotes()->SerializingObject::saveState( dataFile, dataFile.content() );
	}

	saveControllerStates( dataFile, dataFile.content() );

	saveScaleStates(dataFile, dataFile.content());
	saveKeymapStates(dataFile, dataFile.content());

	m_savingProject = false;

	const bool written = dataFile.writeFile(filename, withResources);
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
	qSetGlobalQHashSeed(-1);
#else
	QHashSeed::resetRandomGlobalSeed();
#endif
	return written;
}



// Save the current song
bool Song::guiSaveProject()
{
	return guiSaveProjectAs(m_fileName);
}




// Save the current song with the given filename
bool Song::guiSaveProjectAs(const QString & filename)
{
	const auto extension = QFileInfo(filename).suffix().toLower();
	if (m_nodeProject && (extension == "mmp" || extension == "mmpz"))
	{
		return false;
	}
	DataFile dataFile(DataFile::Type::SongProject);
	if (m_nodeProject)
	{
		dataFile.setNodeWorkflow();
	}
	QString fileNameWithExtension = dataFile.nameWithExtension(filename);

	bool withResources = m_saveOptions.saveAsProjectBundle.value();

	bool const saveResult = saveProjectFile(fileNameWithExtension, withResources);

	// After saving, restore default save options.
	m_saveOptions.setDefaultOptions();

	// If we saved a bundle, we keep the project on the original
	// file and still keep it as modified
	if (saveResult && !withResources)
	{
		setModified(false);
		setProjectFileName(fileNameWithExtension);
	}

	return saveResult;
}




void Song::saveControllerStates( QDomDocument & doc, QDomElement & element )
{
	// save settings of controllers
	QDomElement controllersNode = doc.createElement( "controllers" );
	element.appendChild( controllersNode );
	for (const auto& controller : m_controllers)
	{
		controller->saveState(doc, controllersNode);
	}
}




void Song::restoreControllerStates( const QDomElement & element )
{
	QDomNode node = element.firstChild();
	while( !node.isNull() && !isCancelled() )
	{
		Controller * c = Controller::create( node.toElement(), this );
		if (c) {addController(c);}
		else
		{
			// Fix indices to ensure correct connections
			m_controllers.push_back(Controller::create(Controller::ControllerType::Dummy, this));
		}

		node = node.nextSibling();
	}
}



void Song::removeAllControllers()
{
	while (m_controllers.size() != 0)
	{
		removeController(m_controllers.at(0));
	}

	m_controllers.clear();
}



void Song::saveScaleStates(QDomDocument &doc, QDomElement &element)
{
	QDomElement scalesNode = doc.createElement("scales");
	element.appendChild(scalesNode);

	for (const auto& scale : m_scales)
	{
		scale->saveState(doc, scalesNode);
	}
}


void Song::restoreScaleStates(const QDomElement &element)
{
	QDomNode node = element.firstChild();

	for (auto i = std::size_t{0}; i < MaxScaleCount && !node.isNull() && !isCancelled(); i++)
	{
		m_scales[i]->restoreState(node.toElement());
		node = node.nextSibling();
	}
	emit scaleListChanged(-1);
}


void Song::saveKeymapStates(QDomDocument &doc, QDomElement &element)
{
	QDomElement keymapsNode = doc.createElement("keymaps");
	element.appendChild(keymapsNode);

	for (const auto& keymap : m_keymaps)
	{
		keymap->saveState(doc, keymapsNode);
	}
}


void Song::restoreKeymapStates(const QDomElement &element)
{
	QDomNode node = element.firstChild();

	for (auto i = std::size_t{0}; i < MaxKeymapCount && !node.isNull() && !isCancelled(); i++)
	{
		m_keymaps[i]->restoreState(node.toElement());
		node = node.nextSibling();
	}
	emit keymapListChanged(-1);
}


void Song::exportProjectMidi(const std::filesystem::path& filePath) const
{
	// instantiate midi export plugin
	TrackContainer::TrackList const & tracks = this->tracks();
	TrackContainer::TrackList const & patternStoreTracks = Engine::patternStore()->tracks();

	ExportFilter *exf = dynamic_cast<ExportFilter *> (Plugin::instantiate("midiexport", nullptr, nullptr));
	if (exf)
	{
		exf->tryExport(tracks, patternStoreTracks, getTempo(), m_masterPitchModel.value(), filePath);
	}
	else
	{
		qDebug() << "failed to load midi export filter!";
	}

}



void Song::updateFramesPerTick()
{
	Engine::updateFramesPerTick();
}




void Song::setModified()
{
	setModified(true);
}

void Song::setProjectFileName(QString const & projectFileName)
{
	if (m_fileName != projectFileName)
	{
		m_fileName = projectFileName;
		emit projectFileNameChanged();
	}
}




void Song::addController( Controller * controller )
{
	bool containsController = std::find(m_controllers.begin(), m_controllers.end(), controller) != m_controllers.end();
	if (controller && !containsController)
	{
		if (Engine::projectJournal()->isIncrementalTransaction())
		{
			recordControllerMembership(controller, m_controllers.size(), false);
		}
		m_controllers.push_back(controller);
		emit controllerAdded( controller );

		this->setModified();
	}
}

void Song::recordControllerMembership(Controller* controller, std::size_t rackIndex, bool removing)
{
	struct ParkedController
	{
		QPointer<Controller> controller;
		MidiPort::Mode midiMode = MidiPort::Mode::Disabled;
		bool detached = false;
		~ParkedController()
		{
			if (detached)
			{
				delete controller.data();
			}
		}
	};
	if (!removing)
	{
		m_controllers.reserve(m_controllers.size() + 1);
	}
	auto parked = std::make_shared<ParkedController>();
	parked->controller = controller;
	const auto processing = std::find(Controller::s_controllers.begin(), Controller::s_controllers.end(), controller);
	const auto processingIndex = processing == Controller::s_controllers.end()
		? -1
		: std::distance(Controller::s_controllers.begin(), processing);
	const QPointer<Song> song(this);
	const auto attach = [song, parked, rackIndex, processingIndex] {
		auto* target = parked->controller.data();
		auto& rack = song->m_controllers;
		rack.insert(rack.begin() + std::min(rackIndex, rack.size()), target);
		if (processingIndex >= 0)
		{
			auto& list = Controller::s_controllers;
			list.insert(list.begin() + std::min<std::size_t>(processingIndex, list.size()), target);
		}
		if (auto* midi = dynamic_cast<MidiController*>(target))
		{
			midi->midiPort()->setMode(parked->midiMode);
		}
		parked->detached = false;
		emit song->controllerAdded(target);
	};
	const auto detach = [song, parked] {
		auto* target = parked->controller.data();
		if (auto* midi = dynamic_cast<MidiController*>(target))
		{
			parked->midiMode = midi->midiPort()->mode();
			midi->midiPort()->setMode(MidiPort::Mode::Disabled);
		}
		std::erase(song->m_controllers, target);
		std::erase(Controller::s_controllers, target);
		parked->detached = true;
		emit song->controllerRemoved(target);
	};
	const auto valid = [song, parked] { return song && parked->controller; };
	const bool recorded = removing ? Engine::projectJournal()->addIncrementalChange({valid, attach, detach})
								   : Engine::projectJournal()->addIncrementalChange({valid, detach, attach});
	if (!recorded)
	{
		throw std::runtime_error("Could not record controller membership.");
	}
	if (removing)
	{
		detach();
	}
}

void Song::removeController( Controller * controller )
{
	auto it = std::find(m_controllers.begin(), m_controllers.end(), controller);
	if (it != m_controllers.end())
	{
		if (Engine::projectJournal()->isIncrementalTransaction())
		{
			const auto index = std::distance(m_controllers.begin(), it);
			const auto targets = ControllerConnection::targetsForController(controller);
			for (auto* target : targets)
			{
				target->replaceControllerConnectionIncrementally(nullptr);
			}
			recordControllerMembership(controller, index, true);
			setModified();
			return;
		}
		m_controllers.erase(it);

		emit controllerRemoved( controller );
		delete controller;

		this->setModified();
	}
}




void Song::clearErrors()
{
	m_errors.clear();
}



void Song::collectError( const QString error )
{
	if (!m_errors.contains(error)) { m_errors[error] = 1; }
	else { m_errors[ error ]++; }
}



bool Song::hasErrors()
{
	return !(m_errors.isEmpty());
}



QString Song::errorSummary()
{
	QString errors;

	auto i = m_errors.constBegin();
	while (i != m_errors.constEnd())
	{
		errors.append( i.key() );
		if( i.value() > 1 )
		{
			errors.append( tr(" (repeated %1 times)").arg( i.value() ) );
		}
		errors.append("\n");
		++i;
	}

	errors.prepend( "\n\n" );
	errors.prepend( tr( "The following errors occurred while loading: " ) );

	return errors;
}

bool Song::isSavingProject() const {
	return m_savingProject;
}


std::shared_ptr<const Scale> Song::getScale(unsigned int index) const
{
	if (index >= MaxScaleCount) {index = 0;}

	return std::atomic_load(&m_scales[index]);
}


std::shared_ptr<const Keymap> Song::getKeymap(unsigned int index) const
{
	if (index >= MaxKeymapCount) {index = 0;}

	return std::atomic_load(&m_keymaps[index]);
}


void Song::setScale(unsigned int index, std::shared_ptr<Scale> newScale)
{
	if (index >= MaxScaleCount) {index = 0;}

	Engine::audioEngine()->requestChangeInModel();
	std::atomic_store(&m_scales[index], newScale);
	emit scaleListChanged(index);
	Engine::audioEngine()->doneChangeInModel();
}


void Song::setKeymap(unsigned int index, std::shared_ptr<Keymap> newMap)
{
	if (index >= MaxKeymapCount) {index = 0;}

	Engine::audioEngine()->requestChangeInModel();
	std::atomic_store(&m_keymaps[index], newMap);
	emit keymapListChanged(index);
	Engine::audioEngine()->doneChangeInModel();
}
} // namespace lmms
