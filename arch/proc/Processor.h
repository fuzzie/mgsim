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
#ifndef PROCESSOR_H
#define PROCESSOR_H

#include "arch/IOBus.h"
#include "arch/Memory.h"

class Config;

namespace Simulator
{

class FPU;

class Processor : public Object, public IMemoryCallback
{
public:
    class Allocator;

#include "FamilyTable.h"
#include "ThreadTable.h"
#include "RegisterFile.h"
#include "AncillaryRegisterFile.h"
#include "Network.h"
#include "ICache.h"
#include "IOMatchUnit.h"
#include "IOInterface.h"
#include "DebugChannel.h"
#include "DCache.h"
#include "Pipeline.h"
#include "RAUnit.h"
#include "Allocator.h"
#include "counters.h"

    Processor(const std::string& name, Object& parent, Clock& clock, PID pid, const std::vector<Processor*>& grid, IMemory& m_memory, FPU& fpu, IIOBus *iobus, const Config& config);
    ~Processor();
    
    void Initialize(Processor* prev, Processor* next, MemAddr runAddress, bool legacy);

    PID   GetPID()      const { return m_pid; }
    PSize GetGridSize() const { return m_grid.size(); }
    bool  IsIdle()      const;


    Pipeline& GetPipeline() { return m_pipeline; }
    IOMatchUnit& GetIOMatchUnit() { return m_mmio; }
    
    float GetRegFileAsyncPortActivity() const {
        return (float)m_registerFile.p_asyncW.GetBusyCycles() / (float)GetCycleNo();
    }
	
    TSize GetMaxThreadsAllocated() const { return m_threadTable.GetMaxAllocated(); }
    TSize GetTotalThreadsAllocated() { return m_threadTable.GetTotalAllocated(); }
    TSize GetThreadTableSize() const { return m_threadTable.GetNumThreads(); }
    float GetThreadTableOccupancy() { return (float)GetTotalThreadsAllocated() / (float)GetThreadTableSize() / (float)GetKernel()->GetCycleNo(); }
    FSize GetMaxFamiliesAllocated() const { return m_familyTable.GetMaxAllocated(); }
    FSize GetTotalFamiliesAllocated() { return m_familyTable.GetTotalAllocated(); }
    FSize GetFamilyTableSize() const { return m_familyTable.GetNumFamilies(); }
    float GetFamilyTableOccupancy() { return (float)GetTotalFamiliesAllocated() / (float)GetFamilyTableSize() / (float)GetKernel()->GetCycleNo(); }
    BufferSize GetMaxAllocateExQueueSize() { return m_allocator.GetMaxAllocatedEx(); }
    BufferSize GetTotalAllocateExQueueSize() { return m_allocator.GetTotalAllocatedEx(); }
    float GetAverageAllocateExQueueSize() { return (float)GetTotalAllocateExQueueSize() / (float)GetKernel()->GetCycleNo(); }

    unsigned int GetNumSuspendedRegisters() const;
    
    void WriteRegister(const RegAddr& addr, const RegValue& value) {
        m_registerFile.WriteRegister(addr, value);
    }
	
    // Configuration-dependent helpers
    MemAddr     GetTLSAddress(LFID fid, TID tid) const;
    MemSize     GetTLSSize() const;
    PlaceID     UnpackPlace(Integer id) const;
    Integer     PackPlace(const PlaceID& id) const;
    FID         UnpackFID(Integer id) const;
    Integer     PackFID(const FID& fid) const;
    FCapability GenerateFamilyCapability() const;

    // All memory requests from caches go through the processor.
    // No memory callback specified, the processor will use the tag to determine where it came from.
    void MapMemory(MemAddr address, MemSize size);
    void UnmapMemory(MemAddr address, MemSize size);
    bool ReadMemory (MemAddr address, MemSize size);
    bool WriteMemory(MemAddr address, const void* data, MemSize size, TID tid);
    bool CheckPermissions(MemAddr address, MemSize size, int access) const;
	
    Network& GetNetwork() { return m_network; }

private:
    PID                            m_pid;
    IMemory&	                   m_memory;
    const std::vector<Processor*>& m_grid;
    FPU&                           m_fpu;
    
    // Bit counts for packing and unpacking configuration-dependent values
    struct
    {
        unsigned int pid_bits;  ///< Number of bits for a PID (Processor ID)
        unsigned int fid_bits;  ///< Number of bits for a LFID (Local Family ID)
        unsigned int tid_bits;  ///< Number of bits for a TID (Thread ID)
    } m_bits;
    
    // IMemoryCallback
    bool OnMemoryReadCompleted(MemAddr addr, const MemData& data);
    bool OnMemoryWriteCompleted(TID tid);
    bool OnMemoryInvalidated(MemAddr addr);
    bool OnMemorySnooped(MemAddr addr, const MemData& data);

    // The components on the core
    Allocator             m_allocator;
    ICache                m_icache;
    DCache                m_dcache;
    RegisterFile          m_registerFile;
    Pipeline              m_pipeline;
    RAUnit                m_raunit;
    FamilyTable           m_familyTable;
    ThreadTable           m_threadTable;
    Network               m_network;

    // Local MMIO devices
    IOMatchUnit           m_mmio;
    PerfCounters          m_perfcounters;
    AncillaryRegisterFile m_ancillaryRegisterFile;
    DebugChannel          m_lpout;
    DebugChannel          m_lperr;

    // External I/O interface, optional
    IOInterface           *m_io_if;

    friend class PerfCounters;
};

}
#endif

