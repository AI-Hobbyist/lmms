/*
 * ProjectJournal.cpp - implementation of ProjectJournal
 *
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include <cstdlib>
#include <QDomElement>

#include "ProjectJournal.h"
#include "Engine.h"
#include "JournallingObject.h"
#include "lmms_math.h"
#include "Song.h"
#include "AutomationClip.h"

namespace lmms
{

//! Avoid clashes between loaded IDs (have the bit cleared)
//! and newly created IDs (have the bit set)
static const int EO_ID_MSB = 1 << 23;

const int ProjectJournal::MAX_UNDO_STATES = 100; // TODO: make this configurable in settings

ProjectJournal::ProjectJournal() :
	m_joIDs(),
	m_undoCheckPoints(),
	m_redoCheckPoints(),
	m_journalling( false )
{
}




void ProjectJournal::undo()
{
	if (hasActiveTransaction())
	{
		return;
	}

	while( !m_undoCheckPoints.isEmpty() )
	{
		CheckPoint c = m_undoCheckPoints.pop();
		JournallingObject *jo = m_joIDs[c.joID];

		if( jo )
		{
			const bool prev = isJournalling();
			setJournalling(false);
			auto* song = dynamic_cast<Song*>(jo);
			DataFile curState(c.projectSnapshot ? DataFile::Type::SongProject : DataFile::Type::JournalData);
			if (c.projectSnapshot && song != nullptr)
			{
				song->saveProjectState(curState);
			}
			else
			{
				jo->saveState(curState, curState.content());
			}
			m_redoCheckPoints.push(
				CheckPoint(c.joID, curState, c.projectSnapshot, song != nullptr && song->isModified()));

			if (c.projectSnapshot && song != nullptr)
			{
				m_restoringProject = true;
				song->restoreProjectState(c.data);
				m_restoringProject = false;
				song->setModified(c.modified);
			}
			else
			{
				jo->restoreState(c.data.content().firstChildElement());
			}
			setJournalling( prev );
			if (!c.projectSnapshot)
			{
				Engine::getSong()->setModified();
			}

			// loading AutomationClip connections correctly
			if (!c.projectSnapshot && !c.data.content().elementsByTagName("automationclip").isEmpty())
			{
				AutomationClip::resolveAllIDs();
			}
			break;
		}
	}
}



void ProjectJournal::redo()
{
	if (hasActiveTransaction())
	{
		return;
	}

	while( !m_redoCheckPoints.isEmpty() )
	{
		CheckPoint c = m_redoCheckPoints.pop();
		JournallingObject *jo = m_joIDs[c.joID];

		if( jo )
		{
			const bool prev = isJournalling();
			setJournalling(false);
			auto* song = dynamic_cast<Song*>(jo);
			DataFile curState(c.projectSnapshot ? DataFile::Type::SongProject : DataFile::Type::JournalData);
			if (c.projectSnapshot && song != nullptr)
			{
				song->saveProjectState(curState);
			}
			else
			{
				jo->saveState(curState, curState.content());
			}
			m_undoCheckPoints.push(
				CheckPoint(c.joID, curState, c.projectSnapshot, song != nullptr && song->isModified()));

			if (c.projectSnapshot && song != nullptr)
			{
				m_restoringProject = true;
				song->restoreProjectState(c.data);
				m_restoringProject = false;
				song->setModified(c.modified);
			}
			else
			{
				jo->restoreState(c.data.content().firstChildElement());
			}
			setJournalling( prev );
			if (!c.projectSnapshot)
			{
				Engine::getSong()->setModified();
			}
			break;
		}
	}
}

bool ProjectJournal::canUndo() const
{
	return !m_undoCheckPoints.isEmpty();
}

bool ProjectJournal::canRedo() const
{
	return !m_redoCheckPoints.isEmpty();
}



void ProjectJournal::addJournalCheckPoint( JournallingObject *jo )
{
	if( isJournalling() )
	{
		m_redoCheckPoints.clear();

		DataFile dataFile( DataFile::Type::JournalData );
		jo->saveState( dataFile, dataFile.content() );

		m_undoCheckPoints.push( CheckPoint( jo->id(), dataFile ) );
		if( m_undoCheckPoints.size() > MAX_UNDO_STATES )
		{
			m_undoCheckPoints.remove( 0, m_undoCheckPoints.size() - MAX_UNDO_STATES );
		}
	}
}

bool ProjectJournal::beginTransaction(Song* song)
{
	if (song == nullptr || !song->isJournalling())
	{
		return false;
	}

	DataFile dataFile(DataFile::Type::SongProject);
	const bool journalling = isJournalling();
	setJournalling(false);
	song->saveProjectState(dataFile);
	m_transactions.push_back(Transaction{CheckPoint(song->id(), dataFile, true, song->isModified()), journalling});
	setJournalling(false);
	return true;
}

bool ProjectJournal::commitTransaction()
{
	if (!hasActiveTransaction())
	{
		return false;
	}

	if (m_transactions.size() == 1)
	{
		m_redoCheckPoints.clear();
		m_undoCheckPoints.push(m_transactions.back().checkpoint);
		if (m_undoCheckPoints.size() > MAX_UNDO_STATES)
		{
			m_undoCheckPoints.remove(0, m_undoCheckPoints.size() - MAX_UNDO_STATES);
		}
	}

	const bool journalling = m_transactions.back().journalling;
	m_transactions.pop_back();
	setJournalling(journalling);
	return true;
}

bool ProjectJournal::rollbackTransaction()
{
	if (!hasActiveTransaction())
	{
		return false;
	}

	auto checkpoint = m_transactions.back().checkpoint;
	const bool journalling = m_transactions.back().journalling;
	auto* song = dynamic_cast<Song*>(m_joIDs.value(checkpoint.joID, nullptr));
	if (song == nullptr || !checkpoint.projectSnapshot)
	{
		m_transactions.pop_back();
		setJournalling(journalling);
		return false;
	}

	setJournalling(false);
	m_restoringProject = true;
	song->restoreProjectState(checkpoint.data);
	m_restoringProject = false;
	song->setModified(checkpoint.modified);

	m_transactions.pop_back();
	setJournalling(journalling);
	return true;
}

bool ProjectJournal::hasActiveTransaction() const
{
	return !m_transactions.empty();
}


jo_id_t ProjectJournal::allocID(JournallingObject* obj)
{
	jo_id_t id;
	for (jo_id_t tid = fastRand(); m_joIDs.contains(id = tid % EO_ID_MSB | EO_ID_MSB); tid++) {}
	m_joIDs[id] = obj;
	return id;
}


void ProjectJournal::reallocID( const jo_id_t _id, JournallingObject * _obj )
{
	//printf("realloc %d %d\n", _id, _obj );
//	if( m_joIDs.contains( _id ) )
	{
		m_joIDs[_id] = _obj;
	}
}




jo_id_t ProjectJournal::idToSave( jo_id_t id )
{
	return id & ~EO_ID_MSB;
}

jo_id_t ProjectJournal::idFromSave( jo_id_t id )
{
	return id | EO_ID_MSB;
}




void ProjectJournal::clearJournal()
{
	if (m_restoringProject || hasActiveTransaction())
	{
		return;
	}

	m_undoCheckPoints.clear();
	m_redoCheckPoints.clear();

	for( JoIdMap::Iterator it = m_joIDs.begin(); it != m_joIDs.end(); )
	{
		if( it.value() == nullptr )
		{
			it = m_joIDs.erase( it );
		}
		else
		{
			++it;
		}
	}
}

void ProjectJournal::stopAllJournalling()
{
	for( JoIdMap::Iterator it = m_joIDs.begin(); it != m_joIDs.end(); ++it)
	{
		if( it.value() != nullptr )
		{
			it.value()->setJournalling(false);
		}
	}
	setJournalling(false);
}



} // namespace lmms
