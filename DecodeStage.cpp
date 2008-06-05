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
#include "ISA.h"
#include "Pipeline.h"
#include "Processor.h"
using namespace Simulator;
using namespace std;

static const int A_OPCODE_SHIFT   = 26;
static const int A_RA_SHIFT       = 21;
static const int A_RB_SHIFT       = 16;
static const int A_INT_FUNC_SHIFT = 5;
static const int A_FLT_FUNC_SHIFT = 5;
static const int A_RC_SHIFT       = 0;
static const int A_FD_SHIFT       = 11;	// Only used for IFORMAT_SPECIAL
static const int A_FE_SHIFT       = 5;	// Only used for IFORMAT_SPECIAL
static const int A_MEMDISP_SHIFT  = 0;
static const int A_BRADISP_SHIFT  = 0;
static const int A_PALCODE_SHIFT  = 0;
static const int A_LITERAL_SHIFT  = 13;

static const uint32_t A_OPCODE_MASK   = 0x3F;
static const uint32_t A_REG_MASK      = 0x1F;
static const uint32_t A_MEMDISP_MASK  = 0x000FFFF;
static const uint32_t A_BRADISP_MASK  = 0x01FFFFF;
static const uint32_t A_PALCODE_MASK  = 0x3FFFFFF;
static const uint32_t A_INT_FUNC_MASK = 0x7F;
static const uint32_t A_FLT_FUNC_MASK = 0x7FF;
static const uint32_t A_LITERAL_MASK  = 0xFF;

InstrFormat Pipeline::DecodeStage::getInstrFormat(uint8_t opcode)
{
    if (opcode <= 0x3F)
    {
        switch (opcode >> 4)
        {
            case 0x0:
                switch (opcode & 0xF) {
                    case 0x0: return IFORMAT_PAL;
					case 0x1: return IFORMAT_OP;
					case 0x2: return IFORMAT_SPECIAL;
					case 0x3: return IFORMAT_JUMP;
					case 0x4: return IFORMAT_BRA;
					case 0x5: return IFORMAT_OP;
					case 0x6:
					case 0x7: return IFORMAT_SPECIAL;
                    case 0x8:
                    case 0x9:
                    case 0xA:
                    case 0xB:
                    case 0xC: return IFORMAT_MEM_LOAD;
                    case 0xD:
                    case 0xE:
                    case 0xF: return IFORMAT_MEM_STORE;
                }
                break;

            case 0x1:
                switch (opcode & 0xF) {
                    case 0x0:
                    case 0x1:
                    case 0x2:
                    case 0x3:
                    case 0x4:
                    case 0x5:
                    case 0x6:
                    case 0x7: return IFORMAT_OP;
                    case 0x8: return IFORMAT_MISC;
                    case 0xA: return IFORMAT_JUMP;
                    case 0xC: return IFORMAT_OP;
                }
                break;

            case 0x2: return ((opcode % 8) < 4 ? IFORMAT_MEM_LOAD : IFORMAT_MEM_STORE);
            case 0x3: return IFORMAT_BRA;
        }
    }
    return IFORMAT_INVALID;
}

// This function translates the 32-registers based address into the proper
// register file address.
RegAddr Pipeline::DecodeStage::translateRegister(uint8_t reg, RegType type, RemoteRegAddr* remoteReg) const
{
    // Default: no remote access
    remoteReg->fid = INVALID_GFID;

    if (reg < 31)
    {
        const Family::RegInfo& family = m_input.familyRegs[type];
        const Thread::RegInfo& thread = m_input.threadRegs[type];

        // Check if it's a global
        if (reg < family.count.globals)
        {
            return MAKE_REGADDR(type, family.globals + reg);
        }
        reg -= family.count.globals;
        RegIndex base = thread.base;

        // Check if it's a shared
        if (reg < family.count.shareds)
        {
            if (m_input.isLastThreadInBlock || m_input.isLastThreadInFamily)
            {
                // Remote write
				if (m_input.isLastThreadInFamily && m_input.onParent)
                {
                    // We should write back to the parent shareds on our own CPU
                    return MAKE_REGADDR(type, m_input.familyRegs[type].shareds + reg);
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
            return MAKE_REGADDR(type, base + reg);
        }
        reg  -= family.count.shareds;
        base += family.count.shareds;

        // Check if it's a local
        if (reg < family.count.locals)
        {
            return MAKE_REGADDR(type, base + reg);
        }
        reg  -= family.count.locals;
        base += family.count.locals;

        // Check if it's a dependent
        if (reg < family.count.shareds)
        {
            if (thread.producer != INVALID_REG_INDEX)
            {
                // It's a local dependency
                return MAKE_REGADDR(type, thread.producer + reg);
            }

            // It's a remote dependency
            remoteReg->reg = MAKE_REGADDR(type, reg);
            remoteReg->fid = m_input.gfid;
            return MAKE_REGADDR(type, family.base + family.size - family.count.shareds + reg);
        }
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
        m_output.isLastThreadInFamily  = m_input.isLastThreadInFamily;
		m_output.isFirstThreadInFamily = m_input.isFirstThreadInFamily;
        m_output.fid     = m_input.fid;
        m_output.tid     = m_input.tid;
        m_output.pc      = m_input.pc;
        m_output.swch    = m_input.swch;
        m_output.kill    = m_input.kill;
		m_output.fpcr    = m_input.fpcr;
        m_output.opcode  = (m_input.instr >> A_OPCODE_SHIFT) & A_OPCODE_MASK;
        m_output.format  = getInstrFormat(m_output.opcode);
        RegIndex Ra      = (m_input.instr >> A_RA_SHIFT) & A_REG_MASK;
        RegIndex Rb      = (m_input.instr >> A_RB_SHIFT) & A_REG_MASK;
        RegIndex Rc      = (m_input.instr >> A_RC_SHIFT) & A_REG_MASK;
        RegIndex Fd      = (m_input.instr >> A_FD_SHIFT) & A_REG_MASK;
        RegIndex Fe      = (m_input.instr >> A_FE_SHIFT) & A_REG_MASK;

        for (RegType i = 0; i < NUM_REG_TYPES; i++)
        {
            m_output.familyRegs[i] = m_input.familyRegs[i];
            m_output.threadRegs[i] = m_input.threadRegs[i];
        }

        // It is important that this is zero by default. If Rb is $R31, this value
        // will be used instead of RegFile[Rb]
        m_output.literal = 0;

        switch (m_output.format)
        {
        case IFORMAT_PAL:
            m_output.literal = (m_input.instr >> A_PALCODE_SHIFT) & A_PALCODE_MASK;
            m_output.Ra      = MAKE_REGADDR(RT_INTEGER, 31);
            m_output.Rb      = MAKE_REGADDR(RT_INTEGER, 31);
            m_output.Rc      = MAKE_REGADDR(RT_INTEGER, 31);
            break;

        case IFORMAT_MEM_LOAD:
        case IFORMAT_MEM_STORE:
        {
            uint32_t disp  = (m_input.instr >> A_MEMDISP_SHIFT) & A_MEMDISP_MASK;
            RegType  type  = (m_output.opcode >= 0x20 && m_output.opcode <= 0x27) ? RT_FLOAT : RT_INTEGER;

            m_output.Rc           = MAKE_REGADDR(type, (m_output.format == IFORMAT_MEM_LOAD) ? Ra : 31);
            m_output.Ra           = MAKE_REGADDR(type, (m_output.format == IFORMAT_MEM_LOAD) ? 31 : Ra);
            m_output.Rb           = MAKE_REGADDR(RT_INTEGER, Rb);
            m_output.displacement = (m_output.opcode == A_OP_LDAH) ? (int32_t)(disp << 16) : (int16_t)disp;
            m_output.function     = (uint16_t)disp;
            break;
        }

        case IFORMAT_BRA:
		{
			uint32_t disp = (m_input.instr >> A_BRADISP_SHIFT) & A_BRADISP_MASK;
			RegType  type = (m_output.opcode > 0x30 && m_output.opcode < 0x38 && m_output.opcode != 0x34) ? RT_FLOAT : RT_INTEGER;
			if (m_output.opcode != A_OP_CREATE_D)
			{
				// Unconditional branches write the PC back to Ra.
				// Conditional branches test Ra.
				bool conditional = (m_output.opcode != A_OP_BR && m_output.opcode != A_OP_BSR);

				m_output.Ra = MAKE_REGADDR(type, (conditional) ? Ra : 31);
				m_output.Rc = MAKE_REGADDR(type, (conditional) ? 31 : Ra);
			}
			else
			{
				// Create reads from and writes to Ra
				m_output.Ra = MAKE_REGADDR(RT_INTEGER, Ra);
				m_output.Rc = MAKE_REGADDR(RT_INTEGER, Ra);
			}
			m_output.displacement = (int32_t)(disp << 11) >> 11;    // Sign-extend it
			m_output.Rb = MAKE_REGADDR(type, 31);
            break;
        }

        case IFORMAT_JUMP: {
            // Jumps read the target from Rb and write the current PC back to Ra.
            // Microthreading doesn't need branch prediction, so we ignore the hints.
            m_output.Ra = MAKE_REGADDR(RT_INTEGER, 31);
            m_output.Rb = MAKE_REGADDR(RT_INTEGER, Rb);
            m_output.Rc = MAKE_REGADDR(RT_INTEGER, Ra);
            break;
        }

        case IFORMAT_OP:
            if (m_output.opcode >= 0x14 || m_output.opcode == A_OP_UTHREADF)
            {
                // Floating point operate instruction
                m_output.function = (m_input.instr >> A_FLT_FUNC_SHIFT) & A_FLT_FUNC_MASK;
                m_output.Ra = MAKE_REGADDR((m_output.opcode == A_OP_ITFP) ? RT_INTEGER : RT_FLOAT, Ra);
                m_output.Rc = MAKE_REGADDR((m_output.opcode == A_OP_FPTI) ? RT_INTEGER : RT_FLOAT, Rc);
                m_output.Rb = MAKE_REGADDR(RT_FLOAT, Rb);
            }
            else
            {
                // Integer operate instruction
                m_output.function = (m_input.instr >> A_INT_FUNC_SHIFT) & A_INT_FUNC_MASK;
                m_output.Ra       = MAKE_REGADDR(RT_INTEGER, Ra);
                m_output.Rb       = MAKE_REGADDR(RT_INTEGER, Rb);
                m_output.Rc       = MAKE_REGADDR(RT_INTEGER, Rc);
                if (m_input.instr & 0x0001000)
                {
                    // Use literal instead of Rb
                    m_output.Rb      = MAKE_REGADDR(RT_INTEGER, 31);
                    m_output.literal = (m_input.instr >> A_LITERAL_SHIFT) & A_LITERAL_MASK;
                }
            }
            break;
		
		case IFORMAT_SPECIAL:
			// We encode the register specifiers in the literal
			m_output.literal = ((uint64_t)Rb << 0) | ((uint64_t)Rc << 16) | ((uint64_t)Fd << 32) | ((uint64_t)Fe << 48);
            m_output.Ra = MAKE_REGADDR(RT_INTEGER, Ra);
            m_output.Rb = MAKE_REGADDR(RT_INTEGER, 31);
            m_output.Rc = MAKE_REGADDR(RT_INTEGER, 31);
			break;

        case IFORMAT_MISC:
        default:
            {
                stringstream error;
                error << "Illegal opcode in instruction at " << hex << setw(16) << setfill('0') << m_input.pc;
                throw IllegalInstructionException(error.str());
            }
        }

        // Translate registers from 32-window to full register window
        m_output.Ra = translateRegister((uint8_t)m_output.Ra.index, m_output.Ra.type, &m_output.Rra);
        m_output.Rb = translateRegister((uint8_t)m_output.Rb.index, m_output.Rb.type, &m_output.Rrb);
        m_output.Rc = translateRegister((uint8_t)m_output.Rc.index, m_output.Rc.type, &m_output.Rrc);
    }
    return PIPE_CONTINUE;
}

Pipeline::DecodeStage::DecodeStage(Pipeline& parent, FetchDecodeLatch& input, DecodeReadLatch& output)
  : Stage(parent, "decode", &input, &output),
    m_input(input),
    m_output(output)
{
}
