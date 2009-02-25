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
#include <sstream>
#include <iomanip>
#include "Pipeline.h"
#include "Processor.h"
using namespace Simulator;
using namespace std;

struct IllegalInstruction
{
    IllegalInstruction() {}
};

// This function translates the 32-registers based address into the proper
// register file address.
RegAddr Pipeline::DecodeStage::TranslateRegister(uint8_t reg, RegType type, unsigned int size, RemoteRegAddr* remoteReg) const
{
    // We're always dealing with whole registers
    assert(size % sizeof(Integer) == 0);
    unsigned int nRegs = size / sizeof(Integer);
    
    // Default: no remote access
    remoteReg->fid = INVALID_GFID;
    
    const Family::RegInfo& family = m_input.regs.types[type].family;
    const Thread::RegInfo& thread = m_input.regs.types[type].thread;

    // Get register class and address within class
    RegClass rc;
    reg = GetRegisterClass(reg, family.count, &rc);
    switch (rc)
    {
        case RC_GLOBAL:
            if (reg + nRegs > family.count.globals)
            {
                throw IllegalInstruction();
            }
            
            return MAKE_REGADDR(type, family.globals + reg);
            
        case RC_SHARED:
            if (reg + nRegs > family.count.shareds)
            {
                throw IllegalInstruction();
            }
            
            if (m_input.isLastThreadInBlock || m_input.isLastThreadInFamily)
            {
                // Remote write
				if (m_input.isLastThreadInFamily && m_input.onParent)
                {
                    // We should write back to the parent shareds on our own CPU
                    return MAKE_REGADDR(type, family.shareds + reg);
                }

				if (m_input.gfid != INVALID_GFID)
				{
                    // This is the last thread in the block, set the remoteReg as well,
                    // because we need to forward the shared value to the next CPU
                    // Obviously, this isn't necessary for local families
                    remoteReg->reg = MAKE_REGADDR(type, reg);
                    remoteReg->fid = m_input.gfid;
                }
            }
            return MAKE_REGADDR(type, thread.base + reg);
            
        case RC_LOCAL:
            if (reg + nRegs > family.count.locals)
            {
                throw IllegalInstruction();
            }
            
            return MAKE_REGADDR(type, thread.base + family.count.shareds + reg);

        case RC_DEPENDENT:
            if (reg + nRegs > family.count.shareds)
            {
                throw IllegalInstruction();
            }
            
            if (thread.producer != INVALID_REG_INDEX)
            {
                // It's a local dependency
                return MAKE_REGADDR(type, thread.producer + reg);
            }

            // It's a remote dependency
            remoteReg->reg = MAKE_REGADDR(type, reg);
            remoteReg->fid = m_input.gfid;
            return MAKE_REGADDR(type, family.base + family.size - family.count.shareds + reg);

        case RC_RAZ:
            break;
    }
    return MAKE_REGADDR(type, INVALID_REG_INDEX);
}

Pipeline::PipeAction Pipeline::DecodeStage::read()
{
    // This stage has no external dependencies, so all the work is done
    // in the write phase
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::DecodeStage::write()
{
    COMMIT
    {
        // Copy common latch data
        (CommonLatch&)m_output = m_input;
        m_output.regs = m_input.regs;
        
        try
        {
            // Default cases are just naturally-sized operations
            m_output.RaSize = sizeof(Integer);
            m_output.RbSize = sizeof(Integer);
            m_output.RcSize = sizeof(Integer);

            if (!DecodeInstruction(m_input.instr))
            {
                throw IllegalInstruction();
            }
            
            // Translate registers from window to full register file
            m_output.Ra = TranslateRegister((uint8_t)m_output.Ra.index, m_output.Ra.type, m_output.RaSize, &m_output.Rra);
            m_output.Rb = TranslateRegister((uint8_t)m_output.Rb.index, m_output.Rb.type, m_output.RbSize, &m_output.Rrb);
            m_output.Rc = TranslateRegister((uint8_t)m_output.Rc.index, m_output.Rc.type, m_output.RcSize, &m_output.Rrc);
        }
        catch (IllegalInstruction&)
        {
            stringstream error;
            error << "Illegal instruction at 0x" << hex << setw(16) << setfill('0') << m_input.pc;
            throw IllegalInstructionException(*this, error.str());
        }
    }
    return PIPE_CONTINUE;
}

Pipeline::DecodeStage::DecodeStage(Pipeline& parent, FetchDecodeLatch& input, DecodeReadLatch& output)
  : Stage(parent, "decode", &input, &output),
    m_input(input),
    m_output(output)
{
}
