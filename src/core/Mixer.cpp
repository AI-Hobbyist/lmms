/*
 * Mixer.cpp - effect mixer for LMMS
 *
 * Copyright (c) 2008-2011 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "Mixer.h"

#include <QDomElement>
#include <tuple>

#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "DataFile.h"
#include "Mixer.h"
#include "Song.h"

#include <cmath>
#include "agent/IncrementalUndo.h"

#include "InstrumentTrack.h"
#include "MixHelpers.h"
#include "NodeAudioSilenceThreshold.h"
#include "PatternStore.h"
#include "SampleTrack.h"
#include "SVSTrack.h"
#include "SVCTrack.h"
#include "TrackContainer.h" // For TrackContainer::TrackList typedef

namespace lmms
{


MixerRoute::MixerRoute( MixerChannel * from, MixerChannel * to, float amount ) :
	m_from( from ),
	m_to( to ),
	m_amount(amount, 0, 1, 0.001f, nullptr,
			tr("Amount to send from channel %1 to channel %2").arg(m_from->index()).arg(m_to->index()))
{
	//qDebug( "created: %d to %d", m_from->m_channelIndex, m_to->m_channelIndex );
	// create send amount model
}


void MixerRoute::updateName()
{
	m_amount.setDisplayName(
			tr("Amount to send from channel %1 to channel %2").arg(m_from->index()).arg(m_to->index()));
}


MixerChannel::MixerChannel( int idx, Model * _parent ) :
	m_fxChain( nullptr ),
	m_stillRunning( false ),
	m_peakLeft( 0.0f ),
	m_peakRight( 0.0f ),
	m_buffer(Engine::audioEngine()->framesPerPeriod()),
	m_muteModel( false, _parent ),
	m_soloModel( false, _parent ),
	m_volumeModel(1.f, 0.f, 2.f, 0.001f, _parent),
	m_name(),
	m_lock(),
	m_queued( false ),
	m_dependenciesMet(0),
	m_channelIndex(idx)
{
	m_buffer.allocateInterleavedBuffer();
}




MixerChannel::~MixerChannel()
{
}


inline void MixerChannel::processed()
{
	for( const MixerRoute * receiverRoute : m_sends )
	{
		if( receiverRoute->receiver()->m_muted == false )
		{
			receiverRoute->receiver()->incrementDeps();
		}
	}
}

void MixerChannel::incrementDeps()
{
	const auto i = m_dependenciesMet++ + 1;
	if( i >= m_receives.size() && ! m_queued )
	{
		m_queued = true;
		AudioEngineWorkerThread::addJob( this );
	}
}

void MixerChannel::unmuteForSolo()
{
	m_muteModel.setValue(false);

	// if channel is not master, unmute also every channel it sends to/receives from
	if (!isMaster())
	{
		for (const MixerRoute* sendsRoute : m_sends)
		{
			sendsRoute->receiver()->unmuteSenderForSolo();
		}

		for (const MixerRoute* receiverRoute : m_receives)
		{
			receiverRoute->sender()->unmuteReceiverForSolo();
		}
	}
}

void MixerChannel::unmuteSenderForSolo()
{
	m_muteModel.setValue(false);

	// if channel is not master, unmute every channel it sends to
	if (!isMaster())
	{
		for (const MixerRoute* sendsRoute : m_sends)
		{
			sendsRoute->receiver()->unmuteSenderForSolo();
		}
	}
}


void MixerChannel::unmuteReceiverForSolo()
{
	m_muteModel.setValue(false);

	// if channel is not master, unmute every channel it receives from, and of those, unmute the channels they send to
	if (!isMaster())
	{
		for (const MixerRoute* receiverRoute : m_receives)
		{
			receiverRoute->sender()->unmuteReceiverForSolo();
		}

		for (const MixerRoute* sendsRoute : m_sends)
		{
			sendsRoute->receiver()->unmuteSenderForSolo();
		}
	}
}



void MixerChannel::doProcessing()
{
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	const bool monitorSignal = NodeSignalActivity::monitoring();

	if( m_muted == false )
	{
		for( MixerRoute * senderRoute : m_receives )
		{
			MixerChannel * sender = senderRoute->sender();
			FloatModel * sendModel = senderRoute->amount();
			if( ! sendModel ) qFatal( "Error: no send model found from %d to %d", senderRoute->senderIndex(), m_channelIndex );

			const bool monitorRoute = monitorSignal && sender->m_nodeEffect;
			if (sender->m_buffer.hasAnySignal() || sender->m_stillRunning)
			{
				auto buffer = m_buffer.interleavedBuffer().asSampleFrames();

				// figure out if we're getting sample-exact input
				ValueBuffer * sendBuf = sendModel->valueBuffer();
				ValueBuffer * volBuf = sender->m_volumeModel.valueBuffer();

				// mix it's output with this one's output
				auto ch_buf = sender->m_buffer.interleavedBuffer().asSampleFrames();

				auto pairs = senderRoute->channelPairs();
				if (sender->m_nodeEffect && (!m_nodeEffect || sender->m_nodePatternEffect != m_nodePatternEffect))
				{
					if (!sender->nodeBoundaryOutputConnected(0)) { pairs &= ~0b0011u; }
					if (!sender->nodeBoundaryOutputConnected(1)) { pairs &= ~0b1100u; }
				}
				if (monitorRoute)
				{
					const float volume = sender->m_volumeModel.value();
					const float send = sendModel->value();
					const auto threshold = nodeAudioSilenceAmplitude.load(std::memory_order_relaxed);
					for (unsigned source = 0; source < 2; ++source)
					{
						const unsigned targets = (pairs >> (source * 2)) & 0b11;
						unsigned activeTargets = 0;
						for (f_cnt_t frame = 0; targets != 0 && frame < fpp; ++frame)
						{
							const float gain = (volBuf ? volBuf->values()[frame] : volume)
								* (sendBuf ? sendBuf->values()[frame] : send);
							if (std::abs(ch_buf[frame][source] * gain) >= threshold)
							{
								activeTargets = targets;
								break;
							}
						}
						senderRoute->updateNodeAudioSignal(source, activeTargets);
					}
				}
				if (pairs == 0)
				{
					continue;
				}
				if (pairs != MixerRoute::StereoChannels)
				{
					const float volume = sender->m_volumeModel.value();
					const float send = sendModel->value();
					for (f_cnt_t frame = 0; frame < fpp; ++frame)
					{
						const float gain = (volBuf ? volBuf->values()[frame] : volume)
							* (sendBuf ? sendBuf->values()[frame] : send);
						for (unsigned source = 0; source < 2; ++source)
						{
							for (unsigned target = 0; target < 2; ++target)
							{
								if ((pairs & (1u << (source * 2 + target))) != 0)
								{
									buffer[frame][target] += ch_buf[frame][source] * gain;
								}
							}
						}
					}
					toPlanar(m_buffer.interleavedBuffer(), m_buffer.groupBuffers(0));
					m_buffer.updateAllSilenceFlags();
					continue;
				}

				// use sample-exact mixing if sample-exact values are available
				if( ! volBuf && ! sendBuf ) // neither volume nor send has sample-exact data...
				{
					const float v = sender->m_volumeModel.value() * sendModel->value();
					MixHelpers::addMultiplied(buffer.data(), ch_buf.data(), v, fpp);
				}
				else if( volBuf && sendBuf ) // both volume and send have sample-exact data
				{
					MixHelpers::addMultipliedByBuffers(buffer.data(), ch_buf.data(), volBuf, sendBuf, fpp);
				}
				else if( volBuf ) // volume has sample-exact data but send does not
				{
					const float v = sendModel->value();
					MixHelpers::addMultipliedByBuffer(buffer.data(), ch_buf.data(), v, volBuf, fpp);
				}
				else // vice versa
				{
					const float v = sender->m_volumeModel.value();
					MixHelpers::addMultipliedByBuffer(buffer.data(), ch_buf.data(), v, sendBuf, fpp);
				}
				toPlanar(m_buffer.interleavedBuffer(), m_buffer.groupBuffers(0));
				m_buffer.mixSilenceFlags(sender->m_buffer);
			}
			else if (monitorRoute)
			{
				senderRoute->updateNodeAudioSignal(0, 0);
				senderRoute->updateNodeAudioSignal(1, 0);
			}
		}


		const float v = m_volumeModel.value();

		m_stillRunning = m_fxChain.processAudioBuffer(m_buffer);

		const auto peakSamples = SampleFrame{m_buffer.absPeakValue(0), m_buffer.absPeakValue(1)};
		m_peakLeft = std::max(m_peakLeft, peakSamples[0] * v);
		m_peakRight = std::max(m_peakRight, peakSamples[1] * v);
	}
	else
	{
		m_peakLeft = m_peakRight = 0.0f;
	}

	// increment dependency counter of all receivers
	processed();
}



Mixer::Mixer() :
	Model( nullptr ),
	JournallingObject(),
	m_mixerChannels(),
	m_lastSoloed(-1)
{
	// create master channel
	createChannel();
}



Mixer::~Mixer()
{
	while (!m_mixerRoutes.empty())
	{
		deleteChannelSend(m_mixerRoutes.front());
	}
	while( m_mixerChannels.size() )
	{
		MixerChannel * f = m_mixerChannels[m_mixerChannels.size() - 1];
		m_mixerChannels.pop_back();
		delete f;
	}
}



int Mixer::createChannel()
{
	const int index = m_mixerChannels.size();
	auto* journal = Engine::projectJournal();
	if (index > 0 && journal && journal->isIncrementalTransaction())
	{
		// The embedded models are owned by the channel, including while it is
		// parked off the mixer in the redo history.
		auto parked = std::make_shared<std::unique_ptr<MixerChannel>>(std::make_unique<MixerChannel>(index, nullptr));
		auto* channel = parked->get();
		auto* master = m_mixerChannels.front();
		channel->m_name = tr("Channel %1").arg(index);
		channel->m_volumeModel.setDisplayName(channel->m_name + ">" + tr("Volume"));
		channel->m_muteModel.setDisplayName(channel->m_name + ">" + tr("Mute"));
		channel->m_soloModel.setDisplayName(channel->m_name + ">" + tr("Solo"));
		channel->m_muteBeforeSolo = false;
		if (m_lastSoloed != -1 && m_mixerChannels[m_lastSoloed]->m_soloModel.value())
		{
			channel->m_muteModel.setValue(true);
		}
		m_mixerChannels.reserve(m_mixerChannels.size() + 1);
		const QPointer<Mixer> mixer(this);
		const auto attach = [mixer, channel, parked] {
			channel->setIndex(static_cast<int>(mixer->m_mixerChannels.size()));
			mixer->m_mixerChannels.push_back(parked->release());
		};
		const auto detach = [mixer, channel, parked] {
			std::erase(mixer->m_mixerChannels, channel);
			parked->reset(channel);
		};
		const QPointer<FloatModel> identity(&channel->m_volumeModel);
		const auto valid = [mixer, master, identity] {
			// A later edit in this batch may already have parked the channel.
			return mixer && identity && !mixer->m_mixerChannels.empty() && mixer->m_mixerChannels.front() == master;
		};
		if (!journal->addIncrementalChange({valid, detach, attach}))
		{
			throw std::runtime_error("Could not record the new mixer channel.");
		}
		attach();
		createChannelSend(index, 0);
		Engine::getSong()->setModified();
		return index;
	}
	// create new channel
	m_mixerChannels.push_back( new MixerChannel( index, this ) );

	// reset channel state
	clearChannel( index );

	// if there is a soloed channel, mute the new track
	if (m_lastSoloed != -1 && m_mixerChannels[m_lastSoloed]->m_soloModel.value())
	{
		m_mixerChannels[index]->m_muteBeforeSolo = m_mixerChannels[index]->m_muteModel.value();
		m_mixerChannels[index]->m_muteModel.setValue(true);
	}

	emit channelTopologyChanged();
	return index;
}

void Mixer::activateSolo()
{
	for (auto i = std::size_t{1}; i < m_mixerChannels.size(); ++i)
	{
		auto* channel = m_mixerChannels[i];
		const QPointer<FloatModel> identity(&channel->m_volumeModel);
		const auto present = [identity](Mixer*) { return !identity.isNull(); };
		agent::recordIncrementalOwnedValue(this, present, channel->m_muteBeforeSolo, channel->m_muteModel.value(),
										   [channel](Mixer*, bool value) { channel->m_muteBeforeSolo = value; });
		m_mixerChannels[i]->m_muteBeforeSolo = m_mixerChannels[i]->m_muteModel.value();
		m_mixerChannels[i]->m_muteModel.setValue( true );
	}
}

void Mixer::deactivateSolo()
{
	for (auto i = std::size_t{1}; i < m_mixerChannels.size(); ++i)
	{
		m_mixerChannels[i]->m_muteModel.setValue( m_mixerChannels[i]->m_muteBeforeSolo );
	}
}

void Mixer::toggledSolo()
{
	// Incremental replay restores every solo/mute value and the saved mute flags.
	// Re-running the forward solo transition would overwrite those restored values.
	if (Engine::projectJournal()->isRestoringIncrementally())
	{
		return;
	}
	int soloedChan = -1;
	bool resetSolo = m_lastSoloed != -1;
	//untoggle if lastsoloed is entered
	if (resetSolo)
	{
		m_mixerChannels[m_lastSoloed]->m_soloModel.setValue( false );
	}
	//determine the soloed channel
	for (auto i = std::size_t{0}; i < m_mixerChannels.size(); ++i)
	{
		if (m_mixerChannels[i]->m_soloModel.value() == true)
			soloedChan = i;
	}
	// if no channel is soloed, unmute everything, else mute everything
	if (soloedChan != -1)
	{
		if (resetSolo)
		{
			deactivateSolo();
			activateSolo();
		} else {
			activateSolo();
		}
		// unmute the soloed chan and every channel it sends to/receives from
		m_mixerChannels[soloedChan]->unmuteForSolo();
	} else {
		deactivateSolo();
	}
	agent::recordIncrementalValue(this, m_lastSoloed, soloedChan, [](Mixer* mixer, int value) {
		mixer->m_lastSoloed = value;
	});
	m_lastSoloed = soloedChan;
}



void Mixer::deleteChannel( int index )
{
	// channel deletion is performed between mixer rounds
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	auto* journal = Engine::projectJournal();
	const bool incremental = journal && journal->isIncrementalTransaction();

	// go through every instrument and adjust for the channel index change
	TrackContainer::TrackList tracks;

	auto& songTracks = Engine::getSong()->tracks();
	auto& patternStoreTracks = Engine::patternStore()->tracks();
	tracks.insert(tracks.end(), songTracks.begin(), songTracks.end());
	tracks.insert(tracks.end(), patternStoreTracks.begin(), patternStoreTracks.end());

	for( Track* t : tracks )
	{
		if( t->type() == Track::Type::Instrument )
		{
			auto inst = dynamic_cast<InstrumentTrack*>(t);
			int val = inst->mixerChannelModel()->value(0);
			if( val == index )
			{
				// we are deleting this track's channel send
				// send to master
				inst->mixerChannelModel()->setValue(0);
			}
			else if( val > index )
			{
				// subtract 1 to make up for the missing channel
				inst->mixerChannelModel()->setValue(val-1);
			}
		}
		else if (t->type() == Track::Type::SVS || t->type() == Track::Type::SVC)
		{
			auto* model = t->type() == Track::Type::SVC ? static_cast<SVCTrack*>(t)->mixerChannelModel()
														: static_cast<SVSTrack*>(t)->mixerChannelModel();
			const int value = model->value();
			if (value == index)
			{
				model->setValue(0);
			}
			else if (value > index)
			{
				model->setValue(value - 1);
			}
		}
		else if( t->type() == Track::Type::Sample )
		{
			auto strk = dynamic_cast<SampleTrack*>(t);
			int val = strk->mixerChannelModel()->value(0);
			if( val == index )
			{
				// we are deleting this track's channel send
				// send to master
				strk->mixerChannelModel()->setValue(0);
			}
			else if( val > index )
			{
				// subtract 1 to make up for the missing channel
				strk->mixerChannelModel()->setValue(val-1);
			}
		}
	}

	MixerChannel * ch = m_mixerChannels[index];

	// delete all of this channel's sends and receives
	while (!ch->m_sends.empty())
	{
		deleteChannelSend(ch->m_sends.front());
	}
	while (!ch->m_receives.empty())
	{
		deleteChannelSend(ch->m_receives.front());
	}

	if (incremental)
	{
		ch->m_fxChain.clearIncrementally();
		const auto parked = std::make_shared<std::unique_ptr<MixerChannel>>();
		const QPointer<Mixer> owner(this);
		const QPointer<FloatModel> identity(&ch->m_volumeModel);
		const int soloBefore = m_lastSoloed;
		const int soloAfter = soloBefore == index ? -1 : soloBefore - (soloBefore > index);
		const bool parented = ch->m_volumeModel.parent() == this;
		std::vector<std::tuple<MixerChannel*, QPointer<FloatModel>, QString>> names;
		for (auto i = static_cast<std::size_t>(index + 1); i < m_mixerChannels.size(); ++i)
		{
			auto* sibling = m_mixerChannels[i];
			names.emplace_back(sibling, &sibling->m_volumeModel, sibling->m_name);
		}
		const auto refreshIndices = [owner, index] {
			for (auto i = static_cast<std::size_t>(index); i < owner->m_mixerChannels.size(); ++i)
			{
				owner->m_mixerChannels[i]->setIndex(i);
			}
			for (auto* route : owner->m_mixerRoutes)
			{
				route->updateName();
			}
			emit owner->channelTopologyChanged();
		};
		const auto attach = [owner, parked, names, index, soloBefore, parented, refreshIndices] {
			auto* restored = parked->release();
			if (parented)
			{
				restored->m_volumeModel.setParent(owner.data());
				restored->m_muteModel.setParent(owner.data());
				restored->m_soloModel.setParent(owner.data());
			}
			owner->m_mixerChannels.insert(owner->m_mixerChannels.begin() + index, restored);
			owner->m_lastSoloed = soloBefore;
			for (const auto& [target, model, name] : names)
			{
				target->m_name = name;
			}
			refreshIndices();
		};
		const auto detach = [owner, parked, ch, index, soloAfter, parented, refreshIndices] {
			std::erase(owner->m_mixerChannels, ch);
			if (parented)
			{
				ch->m_volumeModel.setParent(nullptr);
				ch->m_muteModel.setParent(nullptr);
				ch->m_soloModel.setParent(nullptr);
			}
			parked->reset(ch);
			owner->m_lastSoloed = soloAfter;
			for (auto i = static_cast<std::size_t>(index); i < owner->m_mixerChannels.size(); ++i)
			{
				owner->validateChannelName(i, i + 1);
			}
			refreshIndices();
		};
		const auto valid = [owner, identity, names] {
			return owner && identity && std::all_of(names.begin(), names.end(), [](const auto& record) {
					   return !std::get<1>(record).isNull();
				   });
		};
		if (!journal->addIncrementalChange({valid, attach, detach}))
		{
			throw std::runtime_error("Could not record mixer channel removal.");
		}
		detach();
		Engine::getSong()->setModified();
		return;
	}

	// if m_lastSoloed was our index, reset it
	if (m_lastSoloed == index) { m_lastSoloed = -1; }
	// if m_lastSoloed is > delete index, it will move left
	else if (m_lastSoloed > index) { --m_lastSoloed; }

	// actually delete the channel
	m_mixerChannels.erase(m_mixerChannels.begin() + index);
	delete ch;

	for (auto i = static_cast<std::size_t>(index); i < m_mixerChannels.size(); ++i)
	{
		validateChannelName( i, i + 1 );

		// set correct channel index
		m_mixerChannels[i]->setIndex(i);

		// now check all routes and update names of the send models
		for( MixerRoute * r : m_mixerChannels[i]->m_sends )
		{
			r->updateName();
		}
		for( MixerRoute * r : m_mixerChannels[i]->m_receives )
		{
			r->updateName();
		}
	}
	emit channelTopologyChanged();
}



void Mixer::moveChannelLeft( int index )
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	// can't move master or first channel
	if (index <= 1 || static_cast<std::size_t>(index) >= m_mixerChannels.size())
	{
		return;
	}
	// channels to swap
	int a = index - 1, b = index;

	// go through every instrument and adjust for the channel index change
	const TrackContainer::TrackList& songTrackList = Engine::getSong()->tracks();
	const TrackContainer::TrackList& patternTrackList = Engine::patternStore()->tracks();

	for (const auto& trackList : {songTrackList, patternTrackList})
	{
		for (const auto& track : trackList)
		{
			if (track->type() == Track::Type::Instrument)
			{
				auto inst = (InstrumentTrack*)track;
				int val = inst->mixerChannelModel()->value(0);
				if( val == a )
				{
					inst->mixerChannelModel()->setValue(b);
				}
				else if( val == b )
				{
					inst->mixerChannelModel()->setValue(a);
				}
			}
			else if (track->type() == Track::Type::SVS || track->type() == Track::Type::SVC)
			{
				auto* model = track->type() == Track::Type::SVC ? static_cast<SVCTrack*>(track)->mixerChannelModel()
																: static_cast<SVSTrack*>(track)->mixerChannelModel();
				const int value = model->value();
				if (value == a)
				{
					model->setValue(b);
				}
				else if (value == b)
				{
					model->setValue(a);
				}
			}
			else if (track->type() == Track::Type::Sample)
			{
				auto strk = (SampleTrack*)track;
				int val = strk->mixerChannelModel()->value(0);
				if( val == a )
				{
					strk->mixerChannelModel()->setValue(b);
				}
				else if( val == b )
				{
					strk->mixerChannelModel()->setValue(a);
				}
			}
		}
	}

	auto* first = m_mixerChannels[a];
	auto* second = m_mixerChannels[b];
	const int soloAfter = m_lastSoloed == a ? b : m_lastSoloed == b ? a : m_lastSoloed;
	const auto apply = [a, b](Mixer* mixer, const auto& state) {
		const auto& [left, right, solo] = state;
		mixer->m_mixerChannels[a] = left;
		mixer->m_mixerChannels[b] = right;
		left->setIndex(a);
		right->setIndex(b);
		mixer->m_lastSoloed = solo;
		emit mixer->channelTopologyChanged();
	};
	const QPointer<FloatModel> firstIdentity(&first->m_volumeModel);
	const QPointer<FloatModel> secondIdentity(&second->m_volumeModel);
	agent::recordIncrementalOwnedValue(
		this, [firstIdentity, secondIdentity](Mixer*) { return firstIdentity && secondIdentity; },
		std::tuple{first, second, m_lastSoloed}, std::tuple{second, first, soloAfter}, apply);
	apply(this, std::tuple{second, first, soloAfter});
	if (Engine::projectJournal()->isIncrementalTransaction())
	{
		Engine::getSong()->setModified();
	}
}



void Mixer::moveChannelRight( int index )
{
	moveChannelLeft( index + 1 );
}



MixerRoute * Mixer::createChannelSend( mix_ch_t fromChannel, mix_ch_t toChannel,
								float amount )
{
//	qDebug( "requested: %d to %d", fromChannel, toChannel );
	// find the existing connection
	MixerChannel * from = m_mixerChannels[fromChannel];
	MixerChannel * to = m_mixerChannels[toChannel];

	for (const auto& send : from->m_sends)
	{
		if (send->receiver() == to)
		{
			// simply adjust the amount
			send->amount()->setValue(amount);
			return send;
		}
	}

	// connection does not exist. create a new one
	return createRoute( from, to, amount );
}


MixerRoute * Mixer::createRoute( MixerChannel * from, MixerChannel * to, float amount )
{
	if( from == to )
	{
		return nullptr;
	}
	if (auto* journal = Engine::projectJournal(); journal && journal->isIncrementalTransaction())
	{
		auto* route = new MixerRoute(from, to, amount);
		recordRouteMembership(route, false);
		return route;
	}
	Engine::audioEngine()->requestChangeInModel();
	auto route = new MixerRoute(from, to, amount);

	// add us to from's sends
	from->m_sends.push_back(route);

	// add us to to's receives
	to->m_receives.push_back(route);

	// add us to mixer's list
	Engine::mixer()->m_mixerRoutes.push_back(route);
	Engine::audioEngine()->doneChangeInModel();

	return route;
}


// delete the connection made by createChannelSend
void Mixer::deleteChannelSend( mix_ch_t fromChannel, mix_ch_t toChannel )
{
	// delete the send
	MixerChannel * from = m_mixerChannels[fromChannel];
	MixerChannel * to	 = m_mixerChannels[toChannel];

	// find and delete the send entry
	for (const auto& send : from->m_sends)
	{
		if (send->receiver() == to)
		{
			deleteChannelSend(send);
			break;
		}
	}
}

void Mixer::recordRouteMembership(MixerRoute* route, bool removing)
{
	std::unique_ptr<MixerRoute> candidate(removing ? nullptr : route);
	auto* sender = route->sender();
	auto* receiver = route->receiver();
	const auto indexOf = [route](const MixerRouteVector& routes) {
		return std::distance(routes.begin(), std::find(routes.begin(), routes.end(), route));
	};
	const auto senderIndex = indexOf(sender->m_sends);
	const auto receiverIndex = indexOf(receiver->m_receives);
	const auto mixerIndex = indexOf(m_mixerRoutes);
	const auto parked = std::make_shared<std::unique_ptr<MixerRoute>>();
	if (!removing)
	{
		*parked = std::move(candidate);
		sender->m_sends.reserve(sender->m_sends.size() + 1);
		receiver->m_receives.reserve(receiver->m_receives.size() + 1);
		m_mixerRoutes.reserve(m_mixerRoutes.size() + 1);
	}
	const QPointer<Mixer> mixer(this);
	const QPointer<MixerRoute> connection(route);
	const QPointer<FloatModel> sourceModel(&sender->m_volumeModel);
	const QPointer<FloatModel> destinationModel(&receiver->m_volumeModel);
	const auto valid = [mixer, connection, sourceModel, destinationModel] {
		return mixer && connection && sourceModel && destinationModel;
	};
	const auto attach = [mixer, parked, senderIndex, receiverIndex, mixerIndex] {
		auto* active = parked->release();
		auto insert = [active](MixerRouteVector& routes, std::ptrdiff_t index) {
			routes.insert(routes.begin() + std::min<std::size_t>(index, routes.size()), active);
		};
		insert(active->sender()->m_sends, senderIndex);
		insert(active->receiver()->m_receives, receiverIndex);
		insert(mixer->m_mixerRoutes, mixerIndex);
	};
	const auto detach = [mixer, connection, parked] {
		auto* active = connection.data();
		std::erase(active->sender()->m_sends, active);
		std::erase(active->receiver()->m_receives, active);
		std::erase(mixer->m_mixerRoutes, active);
		parked->reset(active);
	};
	auto* journal = Engine::projectJournal();
	const bool recorded = removing ? journal->addIncrementalChange({valid, attach, detach})
								   : journal->addIncrementalChange({valid, detach, attach});
	if (!recorded)
	{
		throw std::runtime_error("Could not record mixer route membership.");
	}
	if (removing)
	{
		detach();
	}
	else
	{
		attach();
	}
}

void Mixer::deleteChannelSend( MixerRoute * route )
{
	if (auto* journal = Engine::projectJournal(); journal && journal->isIncrementalTransaction())
	{
		recordRouteMembership(route, true);
		return;
	}
	Engine::audioEngine()->requestChangeInModel();

	auto removeFromMixerRoute = [route](MixerRouteVector& routeVec)
	{
		auto it = std::find(routeVec.begin(), routeVec.end(), route);
		if (it != routeVec.end()) { routeVec.erase(it); }
	};

	// remove us from from's sends
	removeFromMixerRoute(route->sender()->m_sends);

	// remove us from to's receives
	removeFromMixerRoute(route->receiver()->m_receives);

	// remove us from mixer's list
	removeFromMixerRoute(Engine::mixer()->m_mixerRoutes);

	delete route;
	Engine::audioEngine()->doneChangeInModel();
}


bool Mixer::setNodeEffectBoundaryOutput(mix_ch_t channel, ch_cnt_t side, bool connected)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	if (!Engine::getSong()->isNodeProject() || channel >= numChannels() || side >= 2
		|| !m_mixerChannels[channel]->m_nodeEffect)
	{
		return false;
	}
	auto& outputs = m_mixerChannels[channel]->m_nodeBoundaryOutputs;
	const unsigned bit = 1u << side;
	const unsigned updated = connected ? outputs | bit : outputs & ~bit;
	if (outputs != updated)
	{
		outputs = updated;
		Engine::getSong()->setModified();
		emit channelTopologyChanged();
	}
	return true;
}

bool Mixer::setNodeChannelSend(mix_ch_t from, ch_cnt_t source, mix_ch_t to, ch_cnt_t destination, bool connected)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	if (!Engine::getSong()->isNodeProject() || from == 0 || from >= numChannels() || to >= numChannels()
		|| source >= 2 || destination >= 2)
	{
		return false;
	}

	MixerRoute* route = nullptr;
	for (auto* candidate : m_mixerChannels[from]->m_sends)
	{
		if (candidate->receiverIndex() == to)
		{
			route = candidate;
			break;
		}
	}
	const unsigned bit = 1u << (source * 2 + destination);
	const unsigned current = route ? route->channelPairs() : 0;
	const unsigned updated = connected ? current | bit : current & ~bit;
	if (current == updated)
	{
		return true;
	}
	if (!route)
	{
		if (isInfiniteLoop(from, to))
		{
			return false;
		}
		route = createChannelSend(from, to, 1);
	}
	if (!route)
	{
		return false;
	}
	if (updated == 0)
	{
		deleteChannelSend(route);
	}
	else
	{
		route->setChannelPairs(updated);
	}
	Engine::getSong()->setModified();
	emit channelTopologyChanged();
	return true;
}

bool Mixer::isInfiniteLoop( mix_ch_t sendFrom, mix_ch_t sendTo )
{
	if( sendFrom == sendTo ) return true;
	MixerChannel * from = m_mixerChannels[sendFrom];
	MixerChannel * to = m_mixerChannels[sendTo];
	bool b = checkInfiniteLoop( from, to );
	return b;
}


bool Mixer::checkInfiniteLoop( MixerChannel * from, MixerChannel * to )
{
	// can't send master to anything
	if( from == m_mixerChannels[0] )
	{
		return true;
	}

	// can't send channel to itself
	if( from == to )
	{
		return true;
	}

	// follow sendTo's outputs recursively looking for something that sends
	// to sendFrom
	for (const auto& send : to->m_sends)
	{
		if (checkInfiniteLoop(from, send->receiver()))
		{
			return true;
		}
	}

	return false;
}


// how much does fromChannel send its output to the input of toChannel?
FloatModel * Mixer::channelSendModel( mix_ch_t fromChannel, mix_ch_t toChannel )
{
	if( fromChannel == toChannel )
	{
		return nullptr;
	}
	const MixerChannel * from = m_mixerChannels[fromChannel];
	const MixerChannel * to = m_mixerChannels[toChannel];

	for( MixerRoute * route : from->m_sends )
	{
		if( route->receiver() == to )
		{
			return route->amount();
		}
	}

	return nullptr;
}



void Mixer::mixToChannel(const AudioBuffer& buffer, mix_ch_t dest)
{
	const auto channel = m_mixerChannels[dest];
	if (!channel->m_muteModel.value())
	{
		channel->m_lock.lock();
		MixHelpers::add(channel->m_buffer.groupBuffers(0), buffer.groupBuffers(0));

		// Copy the planar buffer to the temporary interleaved buffer so they stay in sync
		toInterleaved(channel->m_buffer.groupBuffers(0), channel->m_buffer.interleavedBuffer());

		channel->m_buffer.mixSilenceFlags(buffer);
		channel->m_lock.unlock();
	}
}

void Mixer::mixMonoToChannel(const AudioBuffer& buffer, ch_cnt_t sourceSide, mix_ch_t dest, ch_cnt_t destSide)
{
	const auto source = buffer.groupBuffers(0);
	if (dest >= m_mixerChannels.size() || sourceSide >= source.channels() || destSide >= 2
		|| buffer.silenceFlags()[sourceSide])
	{
		return;
	}

	auto* channel = m_mixerChannels[dest];
	if (channel->m_muteModel.value())
	{
		return;
	}

	QMutexLocker lock(&channel->m_lock);
	auto target = channel->m_buffer.groupBuffers(0);
	auto interleaved = channel->m_buffer.interleavedBuffer();
	const auto frames = std::min(source.frames(), target.frames());
	for (f_cnt_t frame = 0; frame < frames; ++frame)
	{
		auto& sample = target.sample(destSide, frame);
		sample += source.sample(sourceSide, frame);
		interleaved[frame][destSide] = sample;
	}
	channel->m_buffer.assumeNonSilent(destSide);
}




void Mixer::prepareMasterMix()
{
	m_mixerChannels[0]->m_buffer.silenceAllChannels();
}



void Mixer::masterMix( SampleFrame* _buf )
{
	const int fpp = Engine::audioEngine()->framesPerPeriod();
	const bool monitorSignal = NodeSignalActivity::monitoring();

	// add the channels that have no dependencies (no incoming senders, ie.
	// no receives) to the jobqueue. The channels that have receives get
	// added when their senders get processed, which is detected by
	// dependency counting.
	// also instantly add all muted channels as they don't need to care
	// about their senders, and can just increment the deps of their
	// recipients right away.
	AudioEngineWorkerThread::resetJobQueue( AudioEngineWorkerThread::JobQueue::OperationMode::Dynamic );
	for( MixerChannel * ch : m_mixerChannels )
	{
		ch->m_muted = ch->m_muteModel.value();
		if( ch->m_muted ) // instantly "process" muted channels
		{
			if (monitorSignal)
			{
				for (auto* route : ch->m_receives)
				{
					if (route->sender()->m_nodeEffect)
					{
						route->updateNodeAudioSignal(0, 0);
						route->updateNodeAudioSignal(1, 0);
					}
				}
			}
			ch->processed();
			ch->done();
		}
		else if( ch->m_receives.size() == 0 )
		{
			ch->m_queued = true;
			AudioEngineWorkerThread::addJob( ch );
		}
	}
	while (m_mixerChannels[0]->state() != ThreadableJob::ProcessingState::Done)
	{
		bool found = false;
		for( MixerChannel * ch : m_mixerChannels )
		{
			const auto s = ch->state();
			if (s == ThreadableJob::ProcessingState::Queued
				|| s == ThreadableJob::ProcessingState::InProgress)
			{
				found = true;
				break;
			}
		}
		if( !found )
		{
			break;
		}
		AudioEngineWorkerThread::startAndWaitForJobs();
	}

	auto buffer = m_mixerChannels[0]->m_buffer.interleavedBuffer().asSampleFrames();

	// handle sample-exact data in master volume fader
	ValueBuffer * volBuf = m_mixerChannels[0]->m_volumeModel.valueBuffer();

	if( volBuf )
	{
		for( int f = 0; f < fpp; f++ )
		{
			buffer[f][0] *= volBuf->values()[f];
			buffer[f][1] *= volBuf->values()[f];
		}
	}

	const float v = volBuf
		? 1.0f
		: m_mixerChannels[0]->m_volumeModel.value();
	const auto outputMask = nodeMasterOutputMask();
	if (outputMask != 3)
	{
		for (int frame = 0; frame < fpp; ++frame)
		{
			if ((outputMask & 1) == 0)
			{
				buffer[frame][0] = 0;
			}
			if ((outputMask & 2) == 0)
			{
				buffer[frame][1] = 0;
			}
		}
	}
	MixHelpers::addMultiplied(_buf, buffer.data(), v, fpp);
	if (monitorSignal)
	{
		unsigned mask = 0;
		if (v != 0 && !m_mixerChannels[0]->m_muted)
		{
			const auto threshold = nodeAudioSilenceAmplitude.load(std::memory_order_relaxed);
			for (int frame = 0; frame < fpp && mask != 3; ++frame)
			{
				mask |= std::abs(buffer[frame][0] * v) >= threshold ? 1 : 0;
				mask |= std::abs(buffer[frame][1] * v) >= threshold ? 2 : 0;
			}
		}
		m_nodeMasterSignalActivity.updateAudio(mask);
	}

	// clear all channel buffers and
	// reset channel process state
	for( int i = 0; i < numChannels(); ++i)
	{
		m_mixerChannels[i]->m_buffer.silenceAllChannels();
		m_mixerChannels[i]->reset();
		m_mixerChannels[i]->m_queued = false;
		m_mixerChannels[i]->m_dependenciesMet = 0;
	}
}




void Mixer::clear()
{
	while( m_mixerChannels.size() > 1 )
	{
		deleteChannel(1);
	}

	clearChannel(0);
}



void Mixer::clearChannel(mix_ch_t index)
{
	MixerChannel * ch = m_mixerChannels[index];
	const bool incremental = Engine::projectJournal() && Engine::projectJournal()->isIncrementalTransaction();
	const auto resetName = index == 0 ? tr("Master") : tr("Channel %1").arg(index);
	const QPointer<FloatModel> identity(&ch->m_volumeModel);
	const auto present = [identity](Mixer*) { return !identity.isNull(); };
	agent::recordIncrementalOwnedValue(this, present, ch->m_name, resetName,
									   [ch](Mixer*, const QString& name) { ch->m_name = name; });
	agent::recordIncrementalOwnedValue(this, present, ch->color(), std::optional<QColor>{},
									   [ch](Mixer*, const std::optional<QColor>& color) { ch->setColor(color); });
	for (const auto& [model, label] : {std::pair<Model*, QString>{&ch->m_volumeModel, tr("Volume")},
									   std::pair<Model*, QString>{&ch->m_muteModel, tr("Mute")},
									   std::pair<Model*, QString>{&ch->m_soloModel, tr("Solo")}})
	{
		agent::recordIncrementalValue(model, model->displayName(), resetName + ">" + label,
									  [](Model* target, const QString& name) { target->setDisplayName(name); });
	}
	if (incremental)
	{
		ch->m_fxChain.clearIncrementally();
	}
	else
	{
		ch->m_fxChain.clear();
	}
	ch->m_volumeModel.setValue( 1.0f );
	ch->m_muteModel.setValue( false );
	ch->m_soloModel.setValue( false );
	ch->m_name = resetName;
	ch->m_volumeModel.setDisplayName( ch->m_name + ">" + tr( "Volume" ) );
	ch->m_muteModel.setDisplayName( ch->m_name + ">" + tr( "Mute" ) );
	ch->m_soloModel.setDisplayName( ch->m_name + ">" + tr( "Solo" ) );
	ch->setColor(std::nullopt);

	// send only to master
	if( index > 0)
	{
		// delete existing sends
		while (!ch->m_sends.empty())
		{
			deleteChannelSend(ch->m_sends.front());
		}

		// add send to master
		createChannelSend(index, 0);
	}

	// delete receives
	while (!ch->m_receives.empty())
	{
		deleteChannelSend(ch->m_receives.front());
	}
	if (incremental)
	{
		Engine::getSong()->setModified();
	}
}

QUuid MixerChannel::nodeWorkflowId()
{
	if (m_nodeWorkflowId.isNull())
	{
		m_nodeWorkflowId = QUuid::createUuid();
	}
	return m_nodeWorkflowId;
}


void Mixer::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	const bool nodeWorkflow = DataFile::isNodeWorkflow(_doc);
	// save channels
	for (auto i = std::size_t{0}; i < m_mixerChannels.size(); ++i)
	{
		MixerChannel * ch = m_mixerChannels[i];

		QDomElement mixch = _doc.createElement( QString( "mixerchannel" ) );
		_this.appendChild( mixch );
		if (nodeWorkflow)
		{
			mixch.setAttribute("nodeid", ch->nodeWorkflowId().toString(QUuid::WithoutBraces));
			if (ch->m_nodeEffect)
			{
				mixch.setAttribute("nodeEffect", 1);
				if (ch->m_nodeBoundaryOutputs != 3)
				{
					mixch.setAttribute("nodeEffectOutputs", ch->m_nodeBoundaryOutputs);
				}
				if (const auto position = ch->m_nodeEffectPosition)
				{
					mixch.setAttribute("nodeEffectX", position->x());
					mixch.setAttribute("nodeEffectY", position->y());
				}
			}
		}

		ch->m_fxChain.saveState( _doc, mixch );
		ch->m_volumeModel.saveSettings( _doc, mixch, "volume" );
		ch->m_muteModel.saveSettings( _doc, mixch, "muted" );
		ch->m_soloModel.saveSettings( _doc, mixch, "soloed" );
		mixch.setAttribute("num", static_cast<qulonglong>(i));
		mixch.setAttribute( "name", ch->m_name );
		if (const auto& color = ch->color()) { mixch.setAttribute("color", color->name()); }

		// add the channel sends
		for (const auto& send : ch->m_sends)
		{
			QDomElement sendsDom = _doc.createElement( QString( "send" ) );
			mixch.appendChild( sendsDom );

			sendsDom.setAttribute("channel", send->receiverIndex());
			if (nodeWorkflow && send->channelPairs() != MixerRoute::StereoChannels)
			{
				sendsDom.setAttribute("nodeChannelPairs", send->channelPairs());
			}
			send->amount()->saveSettings(_doc, sendsDom, "amount");
		}
	}
}

// make sure we have at least num channels
void Mixer::allocateChannelsTo(int num)
{
	if (num <= 0) { return; }
	while (static_cast<std::size_t>(num) > m_mixerChannels.size() - 1)
	{
		createChannel();

		// delete the default send to master
		deleteChannelSend( m_mixerChannels.size()-1, 0 );
	}
}


void Mixer::loadSettings( const QDomElement & _this )
{
	const bool nodeWorkflow = DataFile::isNodeWorkflow(_this.ownerDocument());
	clear();
	QDomNode node = _this.firstChild();

	while( ! node.isNull() )
	{
		QDomElement mixch = node.toElement();

		// index of the channel we are about to load
		int num = mixch.attribute( "num" ).toInt();

		// allocate enough channels
		allocateChannelsTo( num );
		if (nodeWorkflow)
		{
			m_mixerChannels[num]->m_nodeWorkflowId = QUuid(mixch.attribute("nodeid"));
		}
		m_mixerChannels[num]->m_nodeEffect = nodeWorkflow && num > 0 && mixch.attribute("nodeEffect") == "1";
		m_mixerChannels[num]->m_nodeBoundaryOutputs = m_mixerChannels[num]->m_nodeEffect
			? mixch.attribute("nodeEffectOutputs", "3").toUInt() & 3u : 3u;
		bool validX = false;
		bool validY = false;
		const auto x = mixch.attribute("nodeEffectX").toDouble(&validX);
		const auto y = mixch.attribute("nodeEffectY").toDouble(&validY);
		m_mixerChannels[num]->m_nodeEffectPosition =
			m_mixerChannels[num]->m_nodeEffect && validX && validY && std::isfinite(x) && std::isfinite(y)
			? std::optional<QPointF>(QPointF(x, y)) : std::nullopt;

		m_mixerChannels[num]->m_volumeModel.loadSettings( mixch, "volume" );
		m_mixerChannels[num]->m_muteModel.loadSettings( mixch, "muted" );
		m_mixerChannels[num]->m_soloModel.loadSettings( mixch, "soloed" );
		m_mixerChannels[num]->m_name = mixch.attribute( "name" );
		if (mixch.hasAttribute("color"))
		{
			m_mixerChannels[num]->setColor(QColor{mixch.attribute("color")});
		}

		m_mixerChannels[num]->m_fxChain.restoreState( mixch.firstChildElement(
			m_mixerChannels[num]->m_fxChain.nodeName() ) );

		// mixer sends
		QDomNodeList chData = mixch.childNodes();
		for (auto i = 0; i < chData.length(); ++i)
		{
			QDomElement chDataItem = chData.at(i).toElement();
			if( chDataItem.nodeName() == QString( "send" ) )
			{
				int sendTo = chDataItem.attribute( "channel" ).toInt();
				allocateChannelsTo( sendTo ) ;
				MixerRoute * mxr = createChannelSend( num, sendTo, 1.0f );
				if (mxr)
				{
					mxr->amount()->loadSettings(chDataItem, "amount");
					const auto pairs = chDataItem.attribute("nodeChannelPairs").toUInt();
					mxr->setChannelPairs(nodeWorkflow && pairs > 0 && pairs < 16
						? pairs : MixerRoute::StereoChannels);
				}
			}
		}



		node = node.nextSibling();
	}

	emit dataChanged();
}


void Mixer::validateChannelName( int index, int oldIndex )
{
	if( m_mixerChannels[index]->m_name == tr( "Channel %1" ).arg( oldIndex ) )
	{
		m_mixerChannels[index]->m_name = tr( "Channel %1" ).arg( index );
	}
}

bool Mixer::isChannelInUse(int index)
{
	// check if the index mixer channel receives audio from any other channel
	if (!m_mixerChannels[index]->m_receives.empty())
	{
		return true;
	}

	// check if the destination mixer channel on any instrument or sample track is the index mixer channel
	TrackContainer::TrackList tracks;

	auto& songTracks = Engine::getSong()->tracks();
	auto& patternStoreTracks = Engine::patternStore()->tracks();
	tracks.insert(tracks.end(), songTracks.begin(), songTracks.end());
	tracks.insert(tracks.end(), patternStoreTracks.begin(), patternStoreTracks.end());

	for (const auto t : tracks)
	{
		if (t->type() == Track::Type::Instrument)
		{
			auto inst = dynamic_cast<InstrumentTrack*>(t);
			if (inst->mixerChannelModel()->value() == index)
			{
				return true;
			}
		}
		else if (t->type() == Track::Type::SVS || t->type() == Track::Type::SVC)
		{
			const auto* model = t->type() == Track::Type::SVC ? static_cast<SVCTrack*>(t)->mixerChannelModel()
															  : static_cast<SVSTrack*>(t)->mixerChannelModel();
			if (model->value() == index) { return true; }
		}
		else if (t->type() == Track::Type::Sample)
		{
			auto strack = dynamic_cast<SampleTrack*>(t);
			if (strack->mixerChannelModel()->value() == index)
			{
				return true;
			}
		}
	}

	return false;
}


} // namespace lmms
