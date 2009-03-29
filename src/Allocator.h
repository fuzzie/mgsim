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
#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "functions.h"
#include "Memory.h"
#include "ThreadTable.h"
#include <queue>

namespace Simulator
{

class Processor;
class FamilyTable;
class ThreadTable;
class RegisterFile;
class RAUnit;
class ICache;
class Network;
class Pipeline;
struct Family;
struct CreateMessage;

// A list of dependencies that prevent a family from being
// terminated or cleaned up
enum FamilyDependency
{
    FAMDEP_THREAD_COUNT,        // Number of allocated threads
    FAMDEP_OUTSTANDING_READS,   // Number of outstanding memory reads
    FAMDEP_OUTSTANDING_SHAREDS, // Number of outstanding parent shareds
    FAMDEP_PREV_TERMINATED,     // Family is terminated on the previous processor
	FAMDEP_ALLOCATION_DONE,     // Thread allocation is done
};

// A list of dependencies that prevent a thread from being
// terminated or cleaned up
enum ThreadDependency
{
    THREADDEP_OUTSTANDING_WRITES,   // Number of outstanding memory writes
    THREADDEP_PREV_CLEANED_UP,      // Predecessor has been cleaned up
    THREADDEP_NEXT_TERMINATED,      // Successor has terminated
    THREADDEP_TERMINATED,           // Thread has terminated
};

class Allocator : public IComponent
{
public:
	struct Config
	{
		BufferSize localCreatesSize;
		BufferSize remoteCreatesSize;
		BufferSize cleanupSize;
	};

    struct RegisterBases
    {
        RegIndex globals;
        RegIndex shareds;
    };

	struct AllocRequest
	{
		TID           parent;               // Thread performing the allocation (for security)
		RegIndex      reg;                  // Register that will receive the LFID
        RegisterBases bases[NUM_REG_TYPES]; // Bases of parent registers
	};

    // These are the different states in the state machine for
    // family creation
	enum CreateState
	{
		CREATE_INITIAL,             // Waiting for a family to create
		CREATE_LOADING_LINE,        // Waiting until the cache-line is loaded
		CREATE_LINE_LOADED,         // The line has been loaded
		CREATE_GETTING_TOKEN,       // Requesting the token from the network
		CREATE_HAS_TOKEN,           // Got the token
		CREATE_RESERVING_FAMILY,    // Reserving the family across the CPU group
		CREATE_BROADCASTING_CREATE, // Broadcasting the create
		CREATE_ALLOCATING_REGISTERS,// Allocating register space
	};

    Allocator(Processor& parent, const std::string& name,
        FamilyTable& familyTable, ThreadTable& threadTable, RegisterFile& registerFile, RAUnit& raunit, ICache& icache, Network& network, Pipeline& pipeline,
        PSize procNo, const Config& config);

    // Allocates the initial family consisting of a single thread on the first CPU.
    // Typically called before tha actual simulation starts.
    void AllocateInitialFamily(MemAddr pc);

    // Returns the physical register address for a logical shared in a certain family.
    RegAddr GetSharedAddress(SharedType stype, GFID fid, RegAddr addr) const;

    // This is used in all TCB instructions to index the family table with additional checks.
	Family& GetWritableFamilyEntry(LFID fid, TID parent) const;

    /*
     * Thread management
     */
    
    // Activates the specified thread, belonging to family @fid, with PC of @pc.
    // The component is used for arbitration in accesses to resources.
    bool ActivateThread(TID tid, const IComponent& component, MemAddr pc, LFID fid);
    bool ActivateThread(TID tid, const IComponent& component); // Reads PC and FID from thread table
    
    // Reschedules a thread from the pipeline. Manages cache-lines and calls ActivateThread
    bool RescheduleThread(TID tid, MemAddr pc, const IComponent& component);
    // Suspends a thread at the specified PC.
    bool SuspendThread(TID tid, MemAddr pc);
    // Kills a thread
    bool KillThread(TID tid);
    
    uint64_t GetTotalActiveQueueSize() const { return m_totalActiveQueueSize; }
    uint64_t GetMaxActiveQueueSize() const { return m_maxActiveQueueSize; }
    uint64_t GetMinActiveQueueSize() const { return m_minActiveQueueSize; }
    
    bool   KillFamily(LFID fid, ExitCode code, RegValue value);
	Result AllocateFamily(TID parent, RegIndex reg, LFID* fid, const RegisterBases bases[]);
	LFID   AllocateFamily(const CreateMessage& msg);
    GFID   SanitizeFamily(Family& family, bool hasDependency);
	bool   ActivateFamily(LFID fid);
    bool   QueueCreate(LFID fid, MemAddr address, TID parent, RegAddr exitCodeReg);
    bool   QueueActiveThreads(TID first, TID last);
    TID    PopActiveThread(TID tid);
    
    bool   IncreaseFamilyDependency(LFID fid, FamilyDependency dep);
    bool   DecreaseFamilyDependency(LFID fid, FamilyDependency dep);
    bool   IncreaseThreadDependency(          TID tid, ThreadDependency dep);
    bool   DecreaseThreadDependency(LFID fid, TID tid, ThreadDependency dep, const IComponent& component);
    
    // External events
	bool OnCachelineLoaded(CID cid);
	bool OnReservationComplete();
    bool OnTokenReceived();
    bool OnRemoteThreadCompletion(LFID fid);
    bool OnRemoteThreadCleanup(LFID fid);

    /* Component */
    Result OnCycleReadPhase(unsigned int stateIndex);
    Result OnCycleWritePhase(unsigned int stateIndex);
    void   UpdateStatistics();

    /* Admin functions */
	TID                          GetRegisterType(LFID fid, RegAddr addr, RegClass* group) const;
    const Buffer<LFID>&          GetCreateQueue()     const { return m_creates;     }
	CreateState                  GetCreateState()     const { return m_createState; }
	const Buffer<AllocRequest>&  GetAllocationQueue() const { return m_allocations; }
    const Buffer<TID>&           GetCleanupQueue()    const { return m_cleanup;     }

    //
    // Functions/Ports
    //
    ArbitratedWriteFunction     p_cleanup;

private:
    // A queued register write
    struct RegisterWrite
    {
        RegAddr  address;   // Where to write
        RegValue value;     // What to write
    };

    // Private functions
    MemAddr CalculateTLSAddress(LFID fid, TID tid) const;
    MemSize CalculateTLSSize() const;
    
	void SetDefaultFamilyEntry(LFID fid, TID parent, const RegisterBases bases[]) const;
	void InitializeFamily(LFID fid) const;
	bool AllocateRegisters(LFID fid);

    bool AllocateThread(LFID fid, TID tid, bool isNewlyAllocated = true);
    bool PushCleanup(TID tid);

    // Thread and family queue manipulation
    void Push(FamilyQueue& queue, LFID fid);
    void Push(ThreadQueue& queue, TID tid, TID Thread::*link = &Thread::nextState);
    LFID Pop (FamilyQueue& queue);
    TID  Pop (ThreadQueue& queue, TID Thread::*link = &Thread::nextState);

    Processor&          m_parent;
    FamilyTable&        m_familyTable;
    ThreadTable&        m_threadTable;
    RegisterFile&       m_registerFile;
    RAUnit&             m_raunit;
    ICache&             m_icache;
    Network&            m_network;
	Pipeline&			m_pipeline;
    PSize               m_procNo;

    uint64_t             m_activeQueueSize;
    uint64_t             m_totalActiveQueueSize;
    uint64_t             m_maxActiveQueueSize;
    uint64_t             m_minActiveQueueSize;

    // Initial allocation
    LFID                 m_allocating; // This family we're initially allocating from
    FamilyQueue          m_alloc;      // This is the queue of families waiting for initial allocation

    // Buffers
    Buffer<LFID>          m_creates;        // Local create queue
    Buffer<RegisterWrite> m_registerWrites; // Register write queue
    Buffer<TID>           m_cleanup;        // Cleanup queue
	Buffer<AllocRequest>  m_allocations;	// Family allocation queue
	CreateState           m_createState;	// State of the current state;
	CID                   m_createLine;		// Cache line that holds the register info

public:
    ThreadQueue			  m_activeThreads;  // Queue of the active threads
};

}
#endif
