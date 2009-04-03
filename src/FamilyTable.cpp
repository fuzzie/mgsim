/*
mgsim: Microgrid Simulator
Copyright (C) 2006,2007,2008,2009  The Microgrid Project.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
*/
#include "FamilyTable.h"
#include "Processor.h"
#include <cassert>
using namespace Simulator;
using namespace std;

FamilyTable::FamilyTable(Processor& parent, const Config& config)
:   Structure<LFID>(&parent, parent.GetKernel(), "families"),
    m_parent(parent),
    m_globals(config.numGlobals),
    m_families(config.numFamilies),
    m_numFamiliesUsed(0)
{
    for (size_t i = 0; i < config.numGlobals; ++i)
    {
        m_globals[i].used = false;
		m_globals[i].fid  = INVALID_LFID;
    }

    for (size_t i = 0; i < config.numFamilies; ++i)
    {
		// Deny access to empty families
		m_families[i].created     = false;
		m_families[i].parent.lpid = INVALID_LPID;
		m_families[i].parent.gpid = INVALID_GPID;

        m_families[i].next    = i + 1;
        m_families[i].state   = FST_EMPTY;
    }

    m_empty.head = 0;
    m_empty.tail = config.numFamilies - 1;

    m_families[m_empty.tail].next = INVALID_LFID;
}

LFID FamilyTable::AllocateFamily(GFID gfid)
{
    LFID fid = m_empty.head;
    if (m_empty.head != INVALID_LFID)
    {
		// There are empty families, pop one off the queue
		Family& family = m_families[fid];
        assert(family.state == FST_EMPTY);

		if (gfid != INVALID_GFID)
		{
			// This is a global family, set up the mapping as well.
			// It should've already been reserved.
			assert(m_globals[gfid].used);
			COMMIT{
				m_globals[gfid].fid = fid;
				family.gfid = gfid;
			}
		}

        COMMIT
		{
            m_empty.head = m_families[fid].next;
            family.state = FST_ALLOCATED;
            family.next  = INVALID_LFID;
            
            m_numFamiliesUsed++;
		}
    }
    return fid;
}

LFID FamilyTable::TranslateFamily(GFID gfid) const
{
	assert(m_globals[gfid].used);
	return m_globals[gfid].fid;
}

GFID FamilyTable::AllocateGlobal(LFID lfid)
{
    // Find a free Global ID
    for (size_t i = 0; i < m_globals.size(); ++i)
    {
        if (!m_globals[i].used)
        {
            // Mark it as used
			COMMIT
			{
				m_globals[i].used = true;
				m_globals[i].fid  = lfid;
			}
			DebugSimWrite("Allocated G%u for F%u", i, lfid);
            return GFID(i);
        }
    }

    return INVALID_GFID;
}

bool FamilyTable::ReserveGlobal(GFID fid)
{
    assert(fid != INVALID_GFID);
    assert(!m_globals[fid].used);

    COMMIT{ m_globals[fid].used = true; }
    DebugSimWrite("Reserved family G%u", fid);
    return true;
}

bool FamilyTable::UnreserveGlobal(GFID fid)
{
    assert(fid != INVALID_GFID);
	assert(m_globals[fid].used);

    COMMIT{
        m_globals[fid].used = false;
    	m_globals[fid].fid  = INVALID_LFID;
    }
    DebugSimWrite("Unreserved family G%u", fid);
    return true;
}

bool FamilyTable::FreeFamily(LFID fid)
{
	assert(fid != INVALID_LFID);

    COMMIT
    {
        // Put it on the queue
        if (m_empty.head == INVALID_LFID) {
            m_empty.head = fid;
        } else {
            m_families[m_empty.tail].next = fid;
        }
        m_empty.tail = fid;

		Family& family = m_families[fid];
        family.next  = INVALID_LFID;
        family.state = FST_EMPTY;
        
        m_numFamiliesUsed--;
    }
    return true;
}
