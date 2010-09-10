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

#include "kernel.h"
#include "Allocator.h"
#include "ICache.h"
#include "DCache.h"
#include "RegisterFile.h"
#include "Pipeline.h"
#include "Network.h"
#include "FamilyTable.h"
#include "ThreadTable.h"
#include "RAUnit.h"

class Config;

namespace Simulator
{

struct PlaceInfo
{
    PSize        m_size;            ///< Number of processors in the place
    CombinedFlag m_full_context;    ///< Place-global signal for free context identification
    MultiFlag    m_reserve_context; ///< Place-global flag for reserving a context
    CombinedFlag m_want_token;      ///< Place-global signal for token request

    PlaceInfo(Clock& clock, unsigned int size)
        : m_size(size),
          m_full_context(clock, size),
          m_reserve_context(clock, false),
          m_want_token(clock, size)
    {
    }
};

class FPU;

class Processor : public Object, public IMemoryCallback
{
public:
    Processor(const std::string& name, Object& parent, Clock& clock, GPID pid, LPID lpid, const std::vector<Processor*>& grid, PSize gridSize, PlaceInfo& place, IMemory& m_memory, FPU& fpu, const Config& config);
    ~Processor();
    
    void Initialize(Processor& prev, Processor& next, MemAddr runAddress, bool legacy);

    GPID    GetPID()       const { return m_pid; }
    PSize   GetPlaceSize() const { return m_place.m_size; }
    PSize   GetGridSize()  const { return m_gridSize; }
    bool    IsIdle()       const;


    Pipeline& GetPipeline() { return m_pipeline; }
    
    float GetRegFileAsyncPortActivity() const {
        return (float)m_registerFile.p_asyncW.GetBusyCycles() / (float)GetCycleNo();
    }
	
    CycleNo GetLocalFamilyCompletion() const { return m_localFamilyCompletion; }

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
    
    Integer GetProfileWord(unsigned int i) const;
	
    void WriteRegister(const RegAddr& addr, const RegValue& value) {
        m_registerFile.WriteRegister(addr, value);
    }
	
    void OnFamilyTerminatedLocally(MemAddr pc);

    // Configuration-dependent helpers
    MemAddr     GetTLSAddress(LFID fid, TID tid) const;
    MemSize     GetTLSSize() const;
    PlaceID     UnpackPlace(Integer id) const;
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
    GPID                           m_pid;
    IMemory&	                   m_memory;
    const std::vector<Processor*>& m_grid;
    PSize                          m_gridSize;
    PlaceInfo&                     m_place;
    FPU&                           m_fpu;
    
    // Bit counts for packing and unpacking configuration-dependent values
    struct
    {
        unsigned int pid_bits;  ///< Number of bits for a PID (Processor ID)
        unsigned int fid_bits;  ///< Number of bits for a LFID (Local Family ID)
        unsigned int tid_bits;  ///< Number of bits for a TID (Thread ID)
    } m_bits;
    
	
    // Statistics 
    CycleNo m_localFamilyCompletion; 

    // IMemoryCallback
    bool OnMemoryReadCompleted(MemAddr addr, const MemData& data);
    bool OnMemoryWriteCompleted(TID tid);
    bool OnMemoryInvalidated(MemAddr addr);
    bool OnMemorySnooped(MemAddr addr, const MemData& data);

    // The components on the chip
    Allocator       m_allocator;
    ICache          m_icache;
    DCache          m_dcache;
    RegisterFile    m_registerFile;
    Pipeline        m_pipeline;
    RAUnit          m_raunit;
    FamilyTable     m_familyTable;
    ThreadTable     m_threadTable;
    Network         m_network;
};

}
#endif

