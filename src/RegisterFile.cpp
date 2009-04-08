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
#include "RegisterFile.h"
#include "Processor.h"
#include <cassert>
using namespace Simulator;
using namespace std;

//
// RegisterFile implementation
//

RegisterFile::RegisterFile(Processor& parent, Allocator& alloc, Network& network, const Config& config)
:   Structure<RegAddr>(&parent, parent.GetKernel(), "registers"),
    p_pipelineR1(*this), p_pipelineR2(*this), p_pipelineW(*this), p_asyncR(*this), p_asyncW(*this),
    m_integers(config.numIntegers),
    m_floats(config.numFloats),
    m_parent(parent),
    m_allocator(alloc), m_network(network)
{
    // Initialize all registers to empty
    for (RegSize i = 0; i < config.numIntegers; ++i)
    {
        m_integers[i] = MAKE_EMPTY_REG();
    }

    for (RegSize i = 0; i < config.numFloats; ++i)
    {
        m_floats[i] = MAKE_EMPTY_REG();
    }

    // Set port priorities
    SetPriority(p_asyncW,    1);
    SetPriority(p_pipelineW, 0);
}

RegSize RegisterFile::GetSize(RegType type) const
{
    const vector<RegValue>& regs = (type == RT_FLOAT) ? m_floats : m_integers;
    return regs.size();
}

bool RegisterFile::ReadRegister(const RegAddr& addr, RegValue& data) const
{
    const vector<RegValue>& regs = (addr.type == RT_FLOAT) ? m_floats : m_integers;
    if (addr.index >= regs.size())
    {
        throw SimulationException(*this, "A component attempted to read from a non-existing register");
    }
    data = regs[addr.index];
    return true;
}

// Admin version
bool RegisterFile::WriteRegister(const RegAddr& addr, const RegValue& data)
{
    vector<RegValue>& regs = (addr.type == RT_FLOAT) ? m_floats : m_integers;
    if (addr.index < regs.size())
    {
	    regs[addr.index] = data;
	    return true;
	}
	return false;
}

bool RegisterFile::Clear(const RegAddr& addr, RegSize size)
{
    std::vector<RegValue>& regs = (addr.type == RT_FLOAT) ? m_floats : m_integers;
    if (addr.index + size > regs.size())
    {
        throw SimulationException(*this, "A component attempted to clear a non-existing register");
    }

    COMMIT
    {
        const RegValue value = MAKE_EMPTY_REG();
        for (RegSize i = 0; i < size; ++i)
        {
			regs[addr.index + i] = value;
		}
    }

    return true;
}

bool RegisterFile::WriteRegister(const RegAddr& addr, RegValue& data, bool from_memory)
{
	std::vector<RegValue>& regs = (addr.type == RT_FLOAT) ? m_floats : m_integers;
    if (addr.index >= regs.size())
    {
        throw SimulationException(*this, "A component attempted to write to a non-existing register");
    }
    
	assert(data.m_state == RST_EMPTY || data.m_state == RST_WAITING || data.m_state == RST_FULL);
	
	if (data.m_state == RST_EMPTY)
	{
		assert(data.m_remote.reg.fid == INVALID_LFID);
		assert(data.m_waiting.head   == INVALID_TID);
	}

    RegValue& value = regs[addr.index];
	if (value.m_state == RST_EMPTY)
	{
	    if (value.m_memory.size != 0)
	    {
	        if (data.m_state == RST_WAITING)
	        {
	            // Check that the memory information isn't changed
	            assert(data.m_memory.fid         == value.m_memory.fid);
	            assert(data.m_memory.offset      == value.m_memory.offset);
	            assert(data.m_memory.size        == value.m_memory.size);
	            assert(data.m_memory.sign_extend == value.m_memory.sign_extend);
	            assert(data.m_memory.next        == value.m_memory.next);
	        }
	        else if (!from_memory)
	        {
    	        // Only the memory can write to memory-pending registers
    			throw SimulationException(*this, "Writing to a memory-load destination register");
		    }
		}
	}
	else if (value.m_state == RST_WAITING)
    {
	    if (data.m_state == RST_EMPTY)
	    {
			throw SimulationException(*this, "Resetting a waiting register");
		}
        
        if (data.m_state == RST_FULL)
        {
            if (value.m_waiting.head != INVALID_TID)
            {
		        // This write caused a reschedule
                if (!m_allocator.ActivateThreads(value.m_waiting))
                {
                    DeadlockWrite("Unable to wake up threads from %s", addr.str().c_str());
                    return false;
                }
            }
            
            if (value.m_remote.reg.fid != INVALID_LFID)
            {
                // Another processor wants this value
                if (!m_network.SendRegister(value.m_remote.reg, data))
                {
                    DeadlockWrite("Unable to send register from %s", addr.str().c_str());
                    return false;
                }
            }
        }
    }

    COMMIT{ value = data; }
    return true;
}
