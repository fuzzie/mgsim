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
#include <cassert>
#include "Pipeline.h"
#include "Processor.h"
using namespace Simulator;
using namespace std;

Pipeline::PipeAction Pipeline::WritebackStage::read()
{
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::WritebackStage::write()
{
    bool     suspend         = (m_input.suspend != SUSPEND_NONE);
    uint64_t writebackValue  = m_writebackValue;
    RegSize  writebackSize   = m_writebackSize;
    RegIndex writebackOffset = m_writebackOffset;
    
    if (m_input.Rc.valid() && m_input.Rcv.m_state != RST_INVALID)
    {
        // We have something to write back
        RegValue value;
        value.m_state = m_input.Rcv.m_state;

        assert(m_input.Rcv.m_size % sizeof(Integer) == 0);
        RegSize nRegs = m_input.Rcv.m_size / sizeof(Integer);
        
        if (writebackSize == 0)
        {
            // New data write
            writebackSize   = nRegs;
            writebackOffset = 0;
            if (value.m_state == RST_FULL)
            {
                // New data write
                switch (m_input.Rc.type)
                {
                    case RT_INTEGER: writebackValue = m_input.Rcv.m_integer.get(m_input.Rcv.m_size); break;
                    case RT_FLOAT:   writebackValue = m_input.Rcv.m_float.toint(m_input.Rcv.m_size); break;
                }
            }
        }

        // Take data from input
        switch (value.m_state)
        {
        case RST_EMPTY:
            break;
            
        case RST_WAITING:
            value.m_tid   = m_input.Rcv.m_tid;
            writebackSize = 1;      // Write just one register
            // Fall-through

        case RST_PENDING:
            value.m_request   = m_input.Rcv.m_request;
            value.m_component = m_input.Rcv.m_component;
            break;

        case RST_FULL:
            // Compose register value
            switch (m_input.Rc.type)
            {
                case RT_INTEGER: value.m_integer = (Integer)writebackValue; break;
                case RT_FLOAT:   value.m_float.integer = (Integer)writebackValue; break;
            }
            // We do this in two steps; otherwise the compiler could complain
            writebackValue >>= 4 * sizeof(Integer);
            writebackValue >>= 4 * sizeof(Integer);
            break;

        default:
            // These states are never written from the pipeline
            assert(0);
        }
        
        int offset = writebackOffset;
#ifdef ARCH_BIG_ENDIAN
        offset = nRegs - 1 - offset;
#endif

        if (m_input.Rrc.fid != INVALID_GFID)
        {
            assert(m_input.Rcv.m_state == RST_FULL);

            // Also forward the shared to the next CPU.
            // If we're the last thread in the family, it writes to the parent thread.
			if (!m_network.SendShared(
			    m_input.Rrc.fid,
			    m_input.isLastThreadInFamily,
			    MAKE_REGADDR(m_input.Rrc.reg.type, m_input.Rrc.reg.index + offset),
			    value))
            {
                return PIPE_STALL;
            }
        }
        
        // Get the address of the register we're writing.
        const RegAddr addr = MAKE_REGADDR(m_input.Rc.type, m_input.Rc.index + offset);
        
        // We have something to write back          
        if (!m_regFile.p_pipelineW.Write(*this, addr))
        {
            return PIPE_STALL;
        }
        
        // If we're writing WAITING and the data is already present,
        // Rcv's state will be set to FULL
        if (!m_regFile.WriteRegister(addr, value, *this))
        {
            return PIPE_STALL;
        }

        suspend = (value.m_state == RST_WAITING);

        // Adjust after writing
        writebackSize--;
        writebackOffset++;
    }
    else if (m_input.suspend == SUSPEND_MEMORY_BARRIER)
    {
        // Memory barrier, check to make sure it hasn't changed by now
        suspend = (m_threadTable[m_input.tid].dependencies.numPendingWrites != 0);
        if (suspend)
        {
            COMMIT{ m_threadTable[m_input.tid].waitingForWrites = true; }
        }
    }

    if (writebackSize == 0 && m_input.swch)
    {
        // We're done writing and we've switched threads
        if (m_input.kill)
        {
            // Kill the thread
            assert(suspend == false);
            if (!m_allocator.KillThread(m_input.tid))
            {
                return PIPE_STALL;
            }
        }
        // We're not killing, suspend or reschedule
        else if (suspend)
        {
            // Suspend the thread
            if (!m_allocator.SuspendThread(m_input.tid, m_input.pc))
            {
                return PIPE_STALL;
            }
        }
        // Reschedule thread
        else if (!m_allocator.RescheduleThread(m_input.tid, m_input.pc, *this))
        {
            // We cannot reschedule, stall pipeline
            return PIPE_STALL;
        }
    }

    COMMIT {
        m_writebackValue  = writebackValue;
        m_writebackSize   = writebackSize;
        m_writebackOffset = writebackOffset;
    }
    
    return writebackSize == 0
        ? PIPE_CONTINUE     // We're done, continue
        : PIPE_DELAY;       // We still have data to write back next cycle
}

Pipeline::WritebackStage::WritebackStage(Pipeline& parent, MemoryWritebackLatch& input, RegisterFile& regFile, Network& network, Allocator& alloc, ThreadTable& threadTable)
  : Stage(parent, "writeback", &input, NULL),
    m_input(input),
    m_regFile(regFile),
    m_network(network),
    m_allocator(alloc),
    m_threadTable(threadTable),
    m_writebackSize(0)
{
}
