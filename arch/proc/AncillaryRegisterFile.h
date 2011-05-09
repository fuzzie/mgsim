/*
mgsim: Microgrid Simulator
Copyright (C) 2006,2007,2008,2009,2010,2011  The Microgrid Project.

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
#ifndef ANCILLARYREGISTERFILE_H
#define ANCILLARYREGISTERFILE_H


#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

/*
 * Access interface to ancillary registers. Ancillary registers
 * differ from regular (computational) registers in that
 * they are few and do not have state bits. 
 * FIXME:
 * This should really be a register file with ports and arbitration etc.
 * Accessed R/W from the memory stage, W from delegation (remote configure)
 */

typedef size_t ARAddr;

#define ASR_SYSTEM_VERSION    0
// the following must be updated whenever the
// list of ASRs below changes!
#define ASR_SYSTEM_VERSION_VALUE  1

#define ASR_CONFIG_CAPS       1
#define ASR_DELEGATE_CAPS     2
#define ASR_SYSCALL_BASE      3
#define ASR_NUM_PERFCOUNTERS  4
#define ASR_PERFCOUNTERS_BASE 5
#define ASR_IO_PARAMS         6
#define ASR_AIO_BASE          7
#define ASR_PNC_BASE          8
#define NUM_ASRS              9

// Suggested:
// #define APR_SEP                0
// #define APR_TLSTACK_FIRST_TOP  1
// #define APR_TLHEAP_BASE        2
// #define APR_TLHEAP_SIZE        3
// #define APR_GLHEAP             4


class AncillaryRegisterFile : public Object, public Inspect::Interface<Inspect::Read>
{
    const size_t                  m_numRegisters;
    std::vector<Integer>          m_registers;

public:
    AncillaryRegisterFile(const std::string& name, Processor& parent, Clock& clock, Config& config);
    
    size_t GetNumRegisters() const { return m_numRegisters; }

    Integer ReadRegister(ARAddr addr) const;
    void WriteRegister(ARAddr addr, Integer data);
    
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

};


#endif
