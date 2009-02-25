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

// Memory reads and writes will be of at most this many bytes
static const size_t MAX_MEMORY_OPERATION_SIZE = 8;

Pipeline::PipeAction Pipeline::MemoryStage::read()
{
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::MemoryStage::write()
{
    PipeValue rcv = m_input.Rcv;
    
    if (m_input.size > 0)
    {
        // It's a new memory operation!
        assert(m_input.size <= MAX_MEMORY_OPERATION_SIZE);
        assert(MAX_MEMORY_OPERATION_SIZE <= sizeof(uint64_t));

        Result result = SUCCESS;
        if (rcv.m_state == RST_FULL)
        {
            // Memory write

            // Serialize and store data
            char data[MAX_MEMORY_OPERATION_SIZE];

            uint64_t value = 0;
            switch (m_input.Rc.type) {
                case RT_INTEGER: value = m_input.Rcv.m_integer.get(m_input.Rcv.m_size); break;
                case RT_FLOAT:   value = m_input.Rcv.m_float.toint(m_input.Rcv.m_size); break;
                default: assert(0);
            }
            
            SerializeRegister(m_input.Rc.type, value, data, (size_t)m_input.size);

            if ((result = m_dcache.Write(m_input.address, data, m_input.size, m_input.fid, m_input.tid)) == FAILED)
            {
                // Stall
                return PIPE_STALL;
            }

            // Clear the register state so it won't get written to the register file
            rcv.m_state = RST_INVALID;
        }
        // Memory read
        else if (m_input.address == numeric_limits<MemAddr>::max())
        {
            // Invalid address; don't send request, just clear register
            rcv.m_state = RST_EMPTY;
        }
        else if (m_input.Rc.valid())
        {
            // Memory read
            char data[MAX_MEMORY_OPERATION_SIZE];
			RegAddr reg = m_input.Rc;
            if ((result = m_dcache.Read(m_input.address, data, m_input.size, m_input.fid, &reg)) == FAILED)
            {
                // Stall
                return PIPE_STALL;
            }

            rcv.m_size = m_input.Rcv.m_size;
            if (result == SUCCESS)
            {
                // Unserialize and store data
                uint64_t value = UnserializeRegister(m_input.Rc.type, data, (size_t)m_input.size);

                if (m_input.sign_extend)
                {
                    // Sign-extend the value
                    size_t shift = (sizeof(value) - m_input.size) * 8;
                    value = (int64_t)(value << shift) >> shift;
                }
                
                rcv.m_state = RST_FULL;
                switch (m_input.Rc.type)
                {
                case RT_INTEGER: rcv.m_integer.set(value, rcv.m_size); break;
                case RT_FLOAT:   rcv.m_float.fromint(value, rcv.m_size); break;
                default:         assert(0);
                }
            }
			else
			{
				// Remember request data
	            rcv.m_state               = RST_PENDING;
				rcv.m_request.fid         = m_input.fid;
				rcv.m_request.next        = reg;
				rcv.m_request.offset      = (unsigned int)(m_input.address % m_dcache.GetLineSize());
				rcv.m_request.size        = (size_t)m_input.size;
				rcv.m_request.sign_extend = m_input.sign_extend;
				rcv.m_component           = &m_dcache;
			}
        }

        if (result == DELAYED)
        {
            // Increase the oustanding memory count for the family
            if (m_input.Rcv.m_state == RST_FULL)
            {
                if (!m_allocator.IncreaseThreadDependency(m_input.tid, THREADDEP_OUTSTANDING_WRITES))
                {
                    return PIPE_STALL;
                }
            }
            else if (!m_allocator.IncreaseFamilyDependency(m_input.fid, FAMDEP_OUTSTANDING_READS))
            {
                return PIPE_STALL;
            }
        }
    }

    COMMIT
    {
        // Copy common latch data
        (CommonLatch&)m_output = m_input;
        
        m_output.suspend = m_input.suspend;
        m_output.Rc      = m_input.Rc;
        m_output.Rrc     = m_input.Rrc;
        m_output.Rcv     = rcv;
    }
    
    return PIPE_CONTINUE;
}

Pipeline::MemoryStage::MemoryStage(Pipeline& parent, ExecuteMemoryLatch& input, MemoryWritebackLatch& output, DCache& dcache, Allocator& alloc, RegisterFile& regFile, FamilyTable& familyTable)
  : Stage(parent, "memory", &input, &output),
    m_input(input),
    m_output(output),
    m_allocator(alloc),
    m_dcache(dcache),
    m_regFile(regFile),
    m_familyTable(familyTable)
{
}
