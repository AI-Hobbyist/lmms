/*
 * EffectChain.cpp - class for processing and effects chain
 *
 * Copyright (c) 2006-2008 Danny McRae <khjklujn/at/users.sourceforge.net>
 * Copyright (c) 2008-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "EffectChain.h"

#include <QDomElement>
#include <QPointer>
#include <cassert>
#include <memory>
#include <stdexcept>

#include "AudioBuffer.h"
#include "Effect.h"
#include "DummyEffect.h"
#include "PeakController.h"
#include "ProjectJournal.h"
#include "Song.h"

namespace lmms
{


EffectChain::EffectChain( Model * _parent ) :
	Model( _parent ),
	SerializingObject(),
	m_enabledModel( false, nullptr, tr( "Effects enabled" ) )
{
}




EffectChain::~EffectChain()
{
	clear();
}




void EffectChain::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	m_enabledModel.saveSettings( _doc, _this, "enabled" );
	_this.setAttribute("numofeffects", static_cast<int>(m_effects.size()));

	for( Effect* effect : m_effects)
	{
		if (auto dummy = dynamic_cast<DummyEffect*>(effect)) { _this.appendChild(dummy->originalPluginData()); }
		else
		{
			QDomElement ef = effect->saveState( _doc, _this );
			ef.setAttribute( "name", QString::fromUtf8( effect->descriptor()->name ) );
			ef.appendChild( effect->key().saveXML( _doc ) );
		}
	}
}




void EffectChain::loadSettings( const QDomElement & _this )
{
	clear();

	// TODO This method should probably also lock the audio engine

	m_enabledModel.loadSettings( _this, "enabled" );

	const int plugin_cnt = _this.attribute( "numofeffects" ).toInt();

	QDomNode node = _this.firstChild();
	int fx_loaded = 0;
	while( !node.isNull() && fx_loaded < plugin_cnt )
	{
		if( node.isElement() && node.nodeName() == "effect" )
		{
			QDomElement effectData = node.toElement();

			const QString name = effectData.attribute( "name" );
			EffectKey key( effectData.elementsByTagName( "key" ).item( 0 ).toElement() );

			Effect* e = Effect::instantiate( name.toUtf8(), this, &key );

			if( e != nullptr && e->isOkay() && e->nodeName() == node.nodeName() )
			{
				e->restoreState( effectData );
			}
			else
			{
				delete e;
				e = new DummyEffect( parentModel(), effectData );
			}

			m_effects.push_back( e );
			++fx_loaded;
		}
		node = node.nextSibling();
	}

	emit dataChanged();
}




void EffectChain::appendEffect( Effect * _effect )
{
	Engine::audioEngine()->requestChangeInModel();
	m_effects.push_back(_effect);
	Engine::audioEngine()->doneChangeInModel();

	m_enabledModel.setValue( true );

	emit dataChanged();
}




void EffectChain::removeEffect( Effect * _effect )
{
	Engine::audioEngine()->requestChangeInModel();

	auto found = std::find(m_effects.begin(), m_effects.end(), _effect);
	if( found == m_effects.end() )
	{
		Engine::audioEngine()->doneChangeInModel();
		return;
	}
	m_effects.erase( found );

	Engine::audioEngine()->doneChangeInModel();

	if (m_effects.empty())
	{
		m_enabledModel.setValue( false );
	}

	emit dataChanged();
}




void EffectChain::recordEffectMembership(Effect* effect, int index, bool removing)
{
	struct ParkedEffect
	{
		QPointer<Effect> effect;
		bool detached = false;

		~ParkedEffect()
		{
			if (detached)
			{
				delete effect.data();
			}
		}
	};

	if (!removing)
	{
		m_effects.reserve(m_effects.size() + 1);
	}
	const QPointer<EffectChain> chain(this);
	const QPointer<QObject> parent(effect->parent());
	auto parked = std::make_shared<ParkedEffect>();
	parked->effect = effect;
	const auto attach = [chain, parent, parked, index] {
		auto* target = parked->effect.data();
		target->setParent(parent.data());
		chain->m_effects.insert(chain->m_effects.begin() + index, target);
		parked->detached = false;
	};
	const auto detach = [chain, parked] {
		auto* target = parked->effect.data();
		std::erase(chain->m_effects, target);
		target->setParent(nullptr);
		parked->detached = true;
	};
	const auto valid = [chain, parked] { return chain && parked->effect; };
	const auto refresh = [chain] {
		if (chain)
		{
			emit chain->incrementalContentsChanged();
		}
	};
	auto* journal = Engine::projectJournal();
	if (!(removing ? journal->addIncrementalChange({valid, attach, detach, refresh})
				   : journal->addIncrementalChange({valid, detach, attach, refresh})))
	{
		throw std::runtime_error("Could not record effect membership.");
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

void EffectChain::insertEffectIncrementally(Effect* effect, int index)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	recordEffectMembership(effect, index, false);
	m_enabledModel.setValue(true);
}

void EffectChain::removeEffectIncrementally(Effect* effect)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const auto position = std::find(m_effects.begin(), m_effects.end(), effect);
	if (position == m_effects.end())
	{
		return;
	}
	auto* song = Engine::getSong();
	for (auto* controller : song->controllers())
	{
		auto* peak = dynamic_cast<PeakController*>(controller);
		if (peak && peak->effect() == effect)
		{
			song->removeController(peak);
			break;
		}
	}
	recordEffectMembership(effect, std::distance(m_effects.begin(), position), true);
	if (m_effects.empty())
	{
		m_enabledModel.setValue(false);
	}
}

void EffectChain::clearIncrementally()
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	while (!m_effects.empty())
	{
		removeEffectIncrementally(m_effects.back());
	}
	m_enabledModel.setValue(false);
}

void EffectChain::moveDown( Effect * _effect )
{
	if (_effect != m_effects.back())
	{
		auto it = std::find(m_effects.begin(), m_effects.end(), _effect);
		assert(it != m_effects.end());
		std::swap(*std::next(it), *it);
	}
}




void EffectChain::moveUp( Effect * _effect )
{
	if (_effect != m_effects.front())
	{
		auto it = std::find(m_effects.begin(), m_effects.end(), _effect);
		assert(it != m_effects.end());
		std::swap(*std::prev(it), *it);
	}
}




bool EffectChain::processAudioBuffer(AudioBuffer& buffer)
{
	if( m_enabledModel.value() == false )
	{
		return false;
	}

	bool moreEffects = false;
	for (Effect* effect : m_effects)
	{
		moreEffects |= effect->processAudioBuffer(buffer);
	}

	return moreEffects;
}




void EffectChain::clear()
{
	emit aboutToClear();

	Engine::audioEngine()->requestChangeInModel();

	while (m_effects.size())
	{
		auto e = m_effects[m_effects.size() - 1];
		m_effects.pop_back();
		delete e;
	}

	Engine::audioEngine()->doneChangeInModel();

	m_enabledModel.setValue( false );
}

} // namespace lmms
