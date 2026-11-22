/*
 * Song.h - class song - the root of the model-tree
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

#ifndef LMMS_SONG_H
#define LMMS_SONG_H

#include <array>
#include <functional>
#include <memory>

#include <QDomDocument>
#include <QPointF>
#include <optional>
#include <QString>
#include <QStringList>
#include <QHash>  // IWYU pragma: keep

#include "AudioEngine.h"
#include "Controller.h"
#include "Metronome.h"
#include "lmms_constants.h"
#include "MeterModel.h"
#include "Timeline.h"
#include "TrackContainer.h"
#include "VstSyncController.h"

#include <filesystem> // QTBUG-73263

namespace lmms
{

class InstrumentTrack;
class AudioBusHandle;
class AudioBuffer;

class AutomationTrack;
class AutomationClip;
class Clip;
class DataFile;
class MidiEvent;
class Keymap;
class MidiClip;
class SVSClip;
class ProjectJournal;
class Scale;

namespace gui
{
class SongEditor;
class ControllerRackView;
}


const bpm_t MinTempo = 10;
const bpm_t DefaultTempo = 140;
const bpm_t MaxTempo = 999;
const tick_t MaxSongLength = 9999 * DefaultTicksPerBar;


class LMMS_EXPORT Song : public TrackContainer
{
	Q_OBJECT
	mapPropertyFromModel( int,getTempo,setTempo,m_tempoModel );
	mapPropertyFromModel( int,masterPitch,setMasterPitch,m_masterPitchModel );
	mapPropertyFromModel( int,masterVolume,setMasterVolume, m_masterVolumeModel );
public:
	enum class PlayMode
	{
		None,
		Song,
		Pattern,
		MidiClip,
		AutomationClip,
		Count
	} ;
	constexpr static auto PlayModeCount = static_cast<std::size_t>(PlayMode::Count);

	struct SaveOptions {
		/**
		 * Should we discard MIDI ControllerConnections from project files?
		 */
		BoolModel discardMIDIConnections{false};
		/**
		 * Should we save the project as a project bundle? (with resources)
		 */
		BoolModel saveAsProjectBundle{false};

		void setDefaultOptions() {
			discardMIDIConnections.setValue(false);
			saveAsProjectBundle.setValue(false);
		}
	};

	void clearErrors();
	void collectError( const QString error );
	bool hasErrors();
	QString errorSummary();

	void processNextBuffer();

	inline int getLoadingTrackCount() const
	{
		return m_nLoadingTrack;
	}

	inline int getMilliseconds() const
	{
		return getMilliseconds(m_playMode);
	}

	//! Returns the elapsed milliseconds since the start of the song
	//! This function attempts to give the correct value even despite mid-song tempo changes
	inline int getMilliseconds(PlayMode playMode) const
	{
		return 1000 * getTimeline(playMode).getElapsedSeconds();
	}

	inline int getBars() const
	{
		return currentBar();
	}

	inline int ticksPerBar() const
	{
		return TimePos::ticksPerBar(m_timeSigModel);
	}

	// Returns the beat position inside the bar, 0-based
	inline int getBeat() const
	{
		return getPlayPos().getBeatWithinBar(m_timeSigModel);
	}
	// the remainder after bar and beat are removed
	inline int getBeatTicks() const
	{
		return getPlayPos().getTickWithinBeat(m_timeSigModel);
	}
	inline int getTicks() const
	{
		return currentTick();
	}
	inline f_cnt_t getFrames() const
	{
		return currentFrame();
	}
	inline bool isPaused() const
	{
		return m_paused;
	}

	inline bool isPlaying() const
	{
		return m_playing == true && m_exporting == false;
	}

	inline bool isStopped() const
	{
		return m_playing == false && m_paused == false;
	}

	inline bool isExporting() const
	{
		return m_exporting;
	}

	inline void setExportLoop( bool exportLoop )
	{
		m_exportLoop = exportLoop;
	}

	inline bool isRecording() const
	{
		return m_recording;
	}

	inline void setLoopRenderCount(int count)
	{
		if (count < 1)
			m_loopRenderCount = 1;
		else
			m_loopRenderCount = count;
		m_loopRenderRemaining = m_loopRenderCount;
	}

	inline int getLoopRenderCount() const
	{
		return m_loopRenderCount;
	}

	bool isExportDone() const;
	int getExportProgress() const;

	inline void setRenderBetweenMarkers( bool renderBetweenMarkers )
	{
		m_renderBetweenMarkers = renderBetweenMarkers;
	}

	inline PlayMode playMode() const
	{
		return m_playMode;
	}

	bool setPlayMode(PlayMode mode, const Clip* clip = nullptr);
	struct PlaybackState
	{
		PlayMode mode;
		bool playing;
		bool paused;
		bool recording;
		bool loopPreview;
		const Clip* clip;
	};
	PlaybackState capturePlaybackState() const;
	void restorePlaybackState(const PlaybackState& state);
	bool exportLoop() const { return m_exportLoop; }
	bool renderBetweenMarkers() const { return m_renderBetweenMarkers; }
	const Clip* previewClip() const;
	void stopPreviewOf(const Clip* clip);

	const TimePos& getPlayPos(PlayMode pm) const
	{
		return getTimeline(pm).pos();
	}
	const TimePos& getPlayPos() const
	{
		return getPlayPos(m_playMode);
	}

	void setPlayPos(tick_t ticks, PlayMode playMode)
	{
		getTimeline(playMode).setTicks(ticks);
	}
	void setPlayPos(tick_t ticks)
	{
		setPlayPos(ticks, m_playMode);
	}

	auto getTimeline(PlayMode mode) -> Timeline&;
	auto getTimeline(PlayMode mode) const -> const Timeline&;
	auto getTimeline() -> Timeline& { return getTimeline(m_playMode); }
	auto getTimeline() const -> const Timeline& { return getTimeline(m_playMode); }

	void updateLength();
	bar_t length() const
	{
		return m_length;
	}


	bpm_t getTempo();

	AutomationTrack * globalAutomationTrack()
	{
		return m_globalAutomationTrack;
	}

	//TODO: Add Q_DECL_OVERRIDE when Qt4 is dropped
	AutomatedValueMap automatedValuesAt(TimePos time, int clipNum = -1) const override;

	// file management
	void createNewProject();
	void createNodeProject();
	bool isNodeProject() const { return m_nodeProject; }
	void mixDeferredVstOutputs();
	void publishVstTrackAudio(const AudioBusHandle* bus, const AudioBuffer& buffer) const;
	void refreshVstNodePorts();
	void setNodeMasterOutputConnected(unsigned side, bool connected);
	std::optional<QPointF> nodeTrackPosition(Track& track) const;
	std::optional<QPointF> nodeLayoutPosition(const QString& key) const;
	void setNodeLayoutPosition(const QString& key, const QPointF& position);
	void setNodeTrackPosition(Track& track, const QPointF& position);
	struct NodeMixerEndpoint
	{
		mix_ch_t channel;
		ch_cnt_t side;
	};
	std::optional<NodeMixerEndpoint> nodeTrackMixerEndpoint(Track& track, ch_cnt_t sourceSide) const;
	bool nodeTrackBoundaryConnected(Track& track, ch_cnt_t side) const;
	bool setNodeTrackBoundaryConnected(Track& track, ch_cnt_t side, bool connected);
	bool setNodeTrackAudioRoute(Track& track, ch_cnt_t sourceSide,
		std::optional<mix_ch_t> mixerChannel, ch_cnt_t destinationSide);
	bool setNodeSourceMidiInputConnection(Track& track, bool connected);
	bool removeDefaultNodeSourceMidiInput(Track& track);
	struct NodeSourceConnections
	{
		bool midiInput;
		bool events;
		bool stepEvents;
		bool defaultMidiInput;
		std::array<std::optional<unsigned>, 2> audioOutputs;
	};
	NodeSourceConnections nodeSourceConnections(Track& track) const;
	struct NodeInstrumentLayer
	{
		QString id;
		InstrumentTrack* instrument;
		bool notes;
		bool steps;
		std::array<std::optional<ch_cnt_t>, 2> outputs;
	};
	const QList<NodeInstrumentLayer>& nodeInstrumentLayers(const Track& owner) const;
	InstrumentTrack* addNodeInstrumentLayer(Track& owner, const QString& plugin, bool fromDrop,
		const QDomElement& preset = {});
	bool setNodeInstrumentLayerEvent(Track& owner, const QString& id, bool step, bool connected);
	bool setNodeInstrumentLayerOutput(Track& owner, const QString& id, ch_cnt_t source,
		std::optional<ch_cnt_t> target);
	void removeNodeInstrumentLayer(Track& owner, const QString& id);
	QDomElement copyNodeInstrumentLayer(Track& owner, const QString& id) const;
	QString pasteNodeInstrumentLayer(Track& owner, const QDomElement& state);
	bool setNodeSourceEventConnection(Track& track, bool connected);
	bool setNodeSourceStepConnection(Track& track, bool connected);
	bool convertToNodeProject(const QString& fileName);
	QStringList classicConversionChanges();
	bool convertToClassicProject(const QString& fileName);
	bool setNodeSourceAudioOutput(Track& track, ch_cnt_t sourceSide, std::optional<ch_cnt_t> outputSide);
	int dispatchNodeMidiInput(const QString& inputId, const MidiEvent& event,
		const TimePos& time, f_cnt_t offset);
	bool setNodeLogicalInputTarget(const QString& inputId, const QString& signalType,
		Track& track, const QString& port, bool connected);
	struct NodeLogicalInput
	{
		QString id;
		QString signalType;
		QList<QPair<QString, QString>> destinations;
	};
	QList<NodeLogicalInput> nodeLogicalInputs() const;
	void addNodeLogicalInput(const QString& signalType);
	struct NodeBoundaryPort
	{
		QString id;
		QString signal;
		bool output;
		QString track;
		unsigned channel;
		QList<QPair<QString, QString>> destinations;
		QString stereoGroup;
		int stereoSide = -1;
		QString name;
	};
	QList<NodeBoundaryPort> nodeBoundaryPorts(const QString& graph) const;
	void addNodeBoundaryPort(const QString& graph, const QString& signal, bool output);
	QString createNodeBoundaryPort(const QString& graph, const QString& signal, bool output, bool stereo);
	void removeNodeBoundaryPort(const QString& graph, const QString& id);
	bool renameNodeBoundaryPort(const QString& graph, const QString& id, const QString& name);
	struct NodeCustomGraph
	{
		QString id;
		QString name;
	};
	QList<NodeCustomGraph> nodeCustomGraphs(const QString& parent) const;
	QString addNodeCustomGraph(const QString& parent, const QString& name);
	bool renameNodeCustomGraph(const QString& id, const QString& name);
	bool removeNodeCustomGraph(const QString& id);
	QDomElement copyNodeCustomGraph(const QString& id) const;
	QString pasteNodeCustomGraph(const QString& parent, const QDomElement& snapshot);
	struct NodeCustomConnection
	{
		QString source;
		QString destination;
	};
	QList<NodeCustomConnection> nodeCustomConnections(const QString& scope) const;
	bool setNodeCustomConnection(const QString& scope, const QString& source, const QString& destination,
		bool connected);
	QString nodeCustomEffectScope(const QUuid& id) const;
	void setNodeCustomEffectScope(const QUuid& id, const QString& scope);
	QList<QPair<QString, InstrumentTrack*>> nodeCustomInstruments(const QString& scope) const;
	QString addNodeCustomInstrument(const QString& scope, const QString& plugin, const QString& file = {},
		bool fromDrop = false, const QDomElement& preset = {});
	QString pasteNodeCustomInstrument(const QString& scope, const QDomElement& state);
	void removeNodeCustomInstrument(const QString& scope, const QString& id);
	std::optional<QPair<bool, int>> nodeConnectionAggregation() const;
	std::optional<bool> nodePcbBusRouting() const;
	void setNodePcbBusRouting(bool enabled);
	void setNodeConnectionAggregation(bool enabled, int threshold);
	bool setNodeBoundaryAudioSource(const QString& graph, const QString& port, Track& track,
		ch_cnt_t channel, bool connected);
	bool setNodeSourceMidiBoundary(const QString& port, Track& track);
	bool setNodeSourceMidiBoundary(const QString& port, Track& track, bool connected);
	bool hasExtendedSourceGraphs() const;
	static bool hasExtendedSourceGraphs(const QDomElement& workflow, const QDomElement& content);
	QList<int> sourceGraphEffectSlots(Track& track) const;
	bool nodeEffectInPattern(const QUuid& effect) const;
	void setNodeEffectInPattern(const QUuid& effect, bool pattern);
	struct NodeStepPattern
	{
		static constexpr int StepCount = 16;
		QString id;
		unsigned activeSteps;
		QStringList targets;
		QStringList boundaryTargets;
	};
	QList<NodeStepPattern> nodeStepPatterns() const;
	void addNodeStepPattern();
	bool setNodeStep(const QString& id, int step, bool enabled);
	bool setNodeStepTarget(const QString& id, Track& track, bool connected);
	bool setNodeStepBoundaryTarget(const QString& id, const QString& boundary, bool connected);
	bool nodeStepActiveForTrack(Track& track, const TimePos& position) const;
	struct NodePianoRoll
	{
		QString id;
		bool singingVoice;
		QStringList targets;
		QStringList boundaryTargets;
	};
	QList<NodePianoRoll> nodePianoRolls() const;
	void addNodePianoRoll(bool singingVoice);
	Clip* nodePianoRollClip(const QString& id);
	bool setNodePianoRollTarget(const QString& id, Track& track, bool connected);
	bool setNodePianoRollBoundaryTarget(const QString& id, const QString& boundary, bool connected);
	QDomElement copyStandaloneNode(const QString& kind, const QString& id) const;
	bool pasteStandaloneNode(const QDomElement& state);
	bool removeStandaloneNode(const QString& kind, const QString& id);
	QList<Clip*> nodePianoRollSources(Track& track) const;
	bool isEmpty() const;
	void createNewProjectFromTemplate( const QString & templ );
	void loadProject( const QString & filename );
	bool guiSaveProject();
	bool guiSaveProjectAs(const QString & filename);
	bool saveProjectFile(const QString & filename, bool withResources = false);

	const QString & projectFileName() const
	{
		return m_fileName;
	}

	bool isLoadingProject() const
	{
		return m_loadingProject;
	}

	void loadingCancelled()
	{
		m_isCancelled = true;
		Engine::audioEngine()->clearNewPlayHandles();
	}

	bool isCancelled()
	{
		return m_isCancelled;
	}

	bool isModified() const
	{
		return m_modified;
	}
	// Restore the dirty flag after transient operations such as audio rendering.
	void setModified(bool value);

	QString nodeName() const override
	{
		return "song";
	}

	virtual bool fixedClips() const
	{
		return false;
	}

	void addController( Controller * c );
	void removeController( Controller * c );


	const ControllerVector & controllers() const
	{
		return m_controllers;
	}


	MeterModel & getTimeSigModel()
	{
		return m_timeSigModel;
	}

	IntModel& tempoModel()
	{
		return m_tempoModel;
	}

	IntModel& masterVolumeModel() { return m_masterVolumeModel; }

	IntModel& masterPitchModel() { return m_masterPitchModel; }

	void exportProjectMidi(const std::filesystem::path& filePath) const;
	void saveProjectState(DataFile& dataFile);
	void restoreProjectState(DataFile& dataFile);

	inline void setLoadOnLaunch(bool value) { m_loadOnLaunch = value; }
	SaveOptions &getSaveOptions() {
		return m_saveOptions;
	}

	bool isSavingProject() const;

	std::shared_ptr<const Scale> getScale(unsigned int index) const;
	std::shared_ptr<const Keymap> getKeymap(unsigned int index) const;
	void setScale(unsigned int index, std::shared_ptr<Scale> newScale);
	void setKeymap(unsigned int index, std::shared_ptr<Keymap> newMap);

	const std::string& syncKey() const noexcept { return m_vstSyncController.sharedMemoryKey(); }

	Metronome& metronome() { return m_metronome; }

public slots:
	void playSong();
	void record();
	void playAndRecord();
	void playPattern();
	void playMidiClip( const lmms::MidiClip * midiClipToPlay, bool loop = true );
	void playSVSClip(const lmms::SVSClip* clip, bool loop = true);
	void playAutomationClip(const lmms::AutomationClip* clip, bool loop = true);
	void togglePause();
	void stop();

	void startExport();
	void stopExport();


	void setModified();

	void clearProject();

	void addPatternTrack();


private slots:
	void insertBar();
	void removeBar();
	void addSampleTrack();
	void addAutomationTrack();

	void setTempo();
	void setTimeSignature();

	void masterVolumeChanged();

	void savePlayStartPosition();

	void updateFramesPerTick();



private:
	bool saveProjectFile(const QString& fileName, bool withResources, bool nodeFormat);
	friend class ProjectJournal;

	Song();
	Song( const Song & );
	~Song() override;


	inline bar_t currentBar() const
	{
		return getPlayPos(m_playMode).getBar();
	}

	inline tick_t currentTick() const
	{
		return getPlayPos(m_playMode).getTicks();
	}

	inline f_cnt_t currentFrame() const
	{
		return getTimeline(m_playMode).ticks() * Engine::framesPerTick() + getTimeline(m_playMode).frameOffset();
	}

	void saveControllerStates( QDomDocument & doc, QDomElement & element );
	void restoreNodeAudioRoutes();
	void restoreCustomGraphAudioRoutes();
	void restoreNodeInstrumentLayers();
	TrackContainer* ensureNodeInstrumentTracks();
	void saveNodeInstrumentLayers();
	QDomElement nodeInstrumentLayerElement(const QString& id) const;
	InstrumentTrack* createNodeInstrumentLayer(Track& owner, const QDomElement& element);
	void restoreNodeVoiceSource(const QDomElement& roll);
	void restoreNodeWorkflow(DataFile& dataFile);
	QDomElement ensureNodeWorkflow();
	QDomElement ensureNodeSourceSubgraph(Track& track);
	void restoreControllerStates( const QDomElement & element );

	void removeAllControllers();

	void saveScaleStates(QDomDocument &doc, QDomElement &element);
	void restoreScaleStates(const QDomElement &element);

	void saveKeymapStates(QDomDocument &doc, QDomElement &element);
	void restoreKeymapStates(const QDomElement &element);

	void processAutomations(const TrackList& tracks, TimePos timeStart, f_cnt_t frames);
	void processMetronome(size_t bufferOffset);

	void setProjectFileName(QString const & projectFileName);

	AutomationTrack * m_globalAutomationTrack;

	IntModel m_tempoModel;
	MeterModel m_timeSigModel;
	int m_oldTicksPerBar;
	IntModel m_masterVolumeModel;
	IntModel m_masterPitchModel;

	void recordControllerMembership(Controller* controller, std::size_t rackIndex, bool removing);
	ControllerVector m_controllers;

	int m_nLoadingTrack;

	bool m_nodeProject = false;
	QDomDocument m_nodeWorkflow;
	std::function<void()> m_deferredVstOutputs;
	QHash<const AudioBusHandle*, std::function<void(const AudioBuffer&)>> m_vstTrackAudio;
	std::unique_ptr<TrackContainer> m_nodePianoRollTracks;
	QHash<QString, Clip*> m_nodePianoRollClips;
	QString m_fileName;
	QString m_oldFileName;
	bool m_modified;
	bool m_loadOnLaunch;

	volatile bool m_recording;
	volatile bool m_exporting;
	volatile bool m_exportLoop;
	volatile bool m_renderBetweenMarkers;
	volatile bool m_playing;
	volatile bool m_paused;

	bool m_savingProject;
	bool m_loadingProject;
	bool m_isCancelled;

	SaveOptions m_saveOptions;

	QHash<QString, int> m_errors;

	std::array<Timeline, PlayModeCount> m_timelines;

	PlayMode m_playMode;
	bar_t m_length;

	const Clip* m_noteClipToPlay;
	const AutomationClip* m_automationClipToPlay = nullptr;
	bool m_loopMidiClip;

	VstSyncController m_vstSyncController;

	int m_loopRenderCount;
	int m_loopRenderRemaining;
	TimePos m_exportSongBegin;
	TimePos m_exportLoopBegin;
	TimePos m_exportLoopEnd;
	TimePos m_exportSongEnd;
	TimePos m_exportEffectiveLength;

	std::shared_ptr<Scale> m_scales[MaxScaleCount];
	std::shared_ptr<Keymap> m_keymaps[MaxKeymapCount];

	AutomatedValueMap m_oldAutomatedValues;

	Metronome m_metronome;

	// Keep existing Song fields at their original offsets for native plugins.
	std::unique_ptr<TrackContainer> m_nodeInstrumentTracks;
	QHash<QString, InstrumentTrack*> m_nodeCustomInstruments;
	QHash<const Track*, QList<NodeInstrumentLayer>> m_nodeInstrumentLayers;

	friend class Engine;
	friend class gui::SongEditor;
	friend class gui::ControllerRackView;

signals:
	void nodeTrackRoutingChanged();
	void projectLoaded();
	void playbackStateChanged();
	void playbackPositionJumped();
	void lengthChanged( int bars );
	void tempoChanged( lmms::bpm_t newBPM );
	void timeSignatureChanged( int oldTicksPerBar, int ticksPerBar );
	void controllerAdded( lmms::Controller * );
	void controllerRemoved( lmms::Controller * );
	void stopped();
	void modified();
	void projectFileNameChanged();
	void scaleListChanged(int index);
	void keymapListChanged(int index);
	// Emitted while loading is guarded, before replacing any project objects.
	void projectAboutToReset();
} ;


} // namespace lmms

#endif // LMMS_SONG_H
