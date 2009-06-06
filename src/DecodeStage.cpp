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
#include "Pipeline.h"
#include "Processor.h"
#include <cassert>
#include <sstream>
#include <iomanip>
using namespace Simulator;
using namespace std;

struct IllegalInstruction
{
    IllegalInstruction() {}
};

// This function translates the 32-registers based address into the proper
// register file address.
RegAddr Pipeline::DecodeStage::TranslateRegister(unsigned char reg, RegType type, unsigned int size, RemoteRegAddr* remoteReg, bool writing) const
{
    // We're always dealing with whole registers
    assert(size % sizeof(Integer) == 0);
    unsigned int nRegs = size / sizeof(Integer);
    
    // Default: no remote access
    remoteReg->fid = INVALID_LFID;
    
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
            
            if (!m_input.onParent || m_input.parent_pid != INVALID_GPID)
            {
                // Set up the remote register information, in case the global is not full
                remoteReg->pid  = (m_input.onParent) ? m_input.parent_pid : INVALID_GPID;
                remoteReg->fid  = (m_input.onParent) ? m_input.parent_fid : m_input.link_prev;
                remoteReg->type = RRT_GLOBAL;
                remoteReg->reg  = MAKE_REGADDR(type, reg);
            }
            
            return MAKE_REGADDR(type, family.globals + reg);
            
        case RC_SHARED:
            if (reg + nRegs > family.count.shareds)
            {
                throw IllegalInstruction();
            }
            
    		if (m_input.isLastThreadInFamily || m_input.isLastThreadInBlock)
    		{
	    	    if (m_input.isLastThreadInFamily && m_input.parent_pid != INVALID_GPID)
                {
                    // Last thread in a delegated create,
                    // we should send the shareds back remotely
                    remoteReg->pid  = m_input.parent_pid;
                    remoteReg->fid  = m_input.parent_fid;
                    remoteReg->type = RRT_PARENT_SHARED;
                    remoteReg->reg  = MAKE_REGADDR(type, reg);
                }
  		        else
  		        {
  		            if (m_input.isLastThreadInFamily && m_input.onParent)
                    {
                        // We should write back to the parent shareds on our own CPU
                        return MAKE_REGADDR(type, family.shareds + reg);
                    }

                    // This is the last thread in the block, set the remoteReg as well,
                    // because we need to forward the shared value to the next CPU
                    // Obviously, this isn't necessary for local families
		            if (writing && m_input.link_next != INVALID_LFID)
		            {
                        remoteReg->type = (m_input.isLastThreadInFamily) ? RRT_PARENT_SHARED : RRT_FIRST_DEPENDENT;
                        remoteReg->pid  = INVALID_GPID;
                        remoteReg->reg  = MAKE_REGADDR(type, reg);
                        remoteReg->fid  = m_input.link_next;
                    }
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
            
            if (thread.producer != INVALID_REG_INDEX && (!m_input.isFirstThreadInFamily || m_input.parent_pid == INVALID_GPID))
            {
                // It's a local dependency
                return MAKE_REGADDR(type, thread.producer + reg);
            }

            // It's a remote dependency
            remoteReg->pid  = (m_input.isFirstThreadInFamily)  ? m_input.parent_pid : INVALID_GPID;
            remoteReg->fid  = (remoteReg->pid != INVALID_GPID) ? m_input.parent_fid : m_input.link_prev;
            remoteReg->type = (m_input.isFirstThreadInFamily)  ? RRT_PARENT_SHARED  : RRT_LAST_SHARED;
            remoteReg->reg  = MAKE_REGADDR(type, reg);
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
        (Latch&)m_output  = m_input;
        m_output.regs     = m_input.regs;
        
        try
        {
            // Default cases are just naturally-sized operations
            m_output.RaSize = sizeof(Integer);
            m_output.RbSize = sizeof(Integer);
            m_output.RcSize = sizeof(Integer);

            DecodeInstruction(m_input.instr);
            
            // Translate registers from window to full register file
            m_output.Ra = TranslateRegister((unsigned char)m_output.Ra.index, m_output.Ra.type, m_output.RaSize, &m_output.Rra, false);
            m_output.Rb = TranslateRegister((unsigned char)m_output.Rb.index, m_output.Rb.type, m_output.RbSize, &m_output.Rrb, false);
            m_output.Rc = TranslateRegister((unsigned char)m_output.Rc.index, m_output.Rc.type, m_output.RcSize, &m_output.Rrc, true);
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

Pipeline::DecodeStage::DecodeStage(Pipeline& parent, FetchDecodeLatch& input, DecodeReadLatch& output, const Config& /*config*/)
  : Stage(parent, "decode", &input, &output),
    m_input(input),
    m_output(output)
{
}
