/*
mgsim: Microgrid Simulator
Copyright (C) 2006,2007,2008,2009,2010  The Microgrid Project.

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

#include "ThreadTable.h"
#include "FamilyTable.h"
#include "storage.h"
#include <queue>

class Config;

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
struct DelegateMessage;
struct PlaceInfo;

// A list of dependencies that prevent a family from being
// terminated or cleaned up
enum FamilyDependency
{
    FAMDEP_THREAD_COUNT,        // Number of allocated threads
    FAMDEP_OUTSTANDING_READS,   // Number of outstanding memory reads
    FAMDEP_OUTSTANDING_SHAREDS, // Number of outstanding parent shareds
    FAMDEP_PREV_SYNCHRONIZED,   // Family has synchronized on the previous processor
    FAMDEP_NEXT_TERMINATED,     // Family has terminated on the next processor
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

class Allocator : public Object
{
public:
    typedef LinkedList< TID, ThreadTable, &Thread::nextState> ThreadList;
    typedef LinkedList<LFID, FamilyTable, &Family::next>      FamilyList;
    
    struct RegisterBases
    {
        RegIndex globals;
        RegIndex shareds;
    };

	struct AllocRequest
	{
		TID           parent;               // Thread performing the allocation (for security)
		PlaceID       place;                // Place that the create should go to
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
		CREATE_ACQUIRING_TOKEN,     // Requesting the token from the network
		CREATE_BROADCASTING_CREATE, // Broadcasting the create
		CREATE_ALLOCATING_REGISTERS,// Allocating register space
	};

    Allocator(const std::string& name, Processor& parent,
        FamilyTable& familyTable, ThreadTable& threadTable, RegisterFile& registerFile, RAUnit& raunit, ICache& icache, Network& network, Pipeline& pipeline,
        PlaceInfo& place, LPID lpid, const Config& config);

    // Allocates the initial family consisting of a single thread on the first CPU.
    // Typically called before tha actual simulation starts.
    void AllocateInitialFamily(MemAddr pc, bool legacy);

    // Returns the physical register address for a logical register in a certain family.
    RegAddr GetRemoteRegisterAddress(const RemoteRegAddr& addr) const;

    // This is used in all TCB instructions to index the family table with additional checks.
	Family& GetWritableFamilyEntry(LFID fid, TID parent) const;

    /*
     * Thread management
     */
    bool ActivateThreads(const ThreadQueue& threads);   // Activates the specified threads
    bool RescheduleThread(TID tid, MemAddr pc);         // Reschedules a thread from the pipeline
    bool SuspendThread(TID tid, MemAddr pc);            // Suspends a thread at the specified PC
    bool KillThread(TID tid);                           // Kills a thread
    
    bool   SynchronizeFamily(LFID fid, Family& family, ExitCode code);
	Result AllocateFamily(TID parent, RegIndex reg, LFID* fid, const RegisterBases bases[], Integer place);
    bool   SanitizeFamily(Family& family, bool hasDependency);
    bool   QueueCreate(LFID fid, MemAddr address, TID parent, RegIndex exitCodeReg);
	bool   ActivateFamily(LFID fid);
	
	LFID   OnGroupCreate(const CreateMessage& msg, LFID link_next);
    Result OnDelegatedCreate(const DelegateMessage& msg);
    bool   OnDelegationFailed(LFID fid);
    
    bool   QueueActiveThreads(const ThreadQueue& threads);
    bool   QueueThreads(ThreadList& list, const ThreadQueue& threads, ThreadState state);
    
    bool   SetupFamilyPrevLink(LFID fid, LFID link_prev);
    bool   SetupFamilyNextLink(LFID fid, LFID link_next);
    
    bool   OnMemoryRead(LFID fid);
    
    bool   DecreaseFamilyDependency(LFID fid, FamilyDependency dep);
    bool   DecreaseFamilyDependency(LFID fid, Family& family, FamilyDependency dep);
    bool   IncreaseThreadDependency(          TID tid, ThreadDependency dep);
    bool   DecreaseThreadDependency(LFID fid, TID tid, ThreadDependency dep);
    
    TID    PopActiveThread();
    
    // External events
	bool OnCachelineLoaded(CID cid);
    bool OnTokenReceived();
    bool OnRemoteThreadCompletion(LFID fid);
    bool OnRemoteThreadCleanup(LFID fid);
    bool OnRemoteSync(LFID fid, ExitCode code);
    void ReserveContext(bool self);

    // Helpers
	TID     GetRegisterType(LFID fid, RegAddr addr, RegClass* group) const;
    MemAddr CalculateTLSAddress(LFID fid, TID tid) const;
    MemSize CalculateTLSSize() const;
    
	// Debugging
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

private:
    // A queued register write
    struct RegisterWrite
    {
        RegAddr  address;   // Where to write
        RegValue value;     // What to write
    };

	void SetDefaultFamilyEntry(LFID fid, TID parent, const RegisterBases bases[], const PlaceID& place) const;
	void InitializeFamily(LFID fid, Family::Type type) const;
	bool AllocateRegisters(LFID fid, ContextType type);
	bool WriteExitCode(RegIndex reg, ExitCode code);

    bool AllocateThread(LFID fid, TID tid, bool isNewlyAllocated = true);
    bool PushCleanup(TID tid);
    
    bool IsContextAvailable() const;
    void UpdateContextAvailability();

    // Thread queue manipulation
    void Push(ThreadQueue& queue, TID tid, TID Thread::*link = &Thread::nextState);
    TID  Pop (ThreadQueue& queue, TID Thread::*link = &Thread::nextState);

    Processor&    m_parent;
    FamilyTable&  m_familyTable;
    ThreadTable&  m_threadTable;
    RegisterFile& m_registerFile;
    RAUnit&       m_raunit;
    ICache&       m_icache;
    Network&      m_network;
	Pipeline&	  m_pipeline;
	PlaceInfo&    m_place;
    LPID          m_lpid;

    FamilyList            m_alloc;          ///< This is the queue of families waiting for initial allocation
    Buffer<LFID>          m_creates;        ///< Create queue
    Buffer<RegisterWrite> m_registerWrites; ///< Register write queue
    Buffer<TID>           m_cleanup;        ///< Cleanup queue
	Buffer<AllocRequest>  m_allocations;	///< Family allocation queue
	Buffer<AllocRequest>  m_allocationsEx;  ///< Exclusive family allocation queue
	Register<LFID>        m_createFID;      ///< Family ID of the current create
	CreateState           m_createState;	///< State of the current state;
	CID                   m_createLine;	   	///< Cache line that holds the register info
    ThreadList            m_readyThreads;   ///< Queue of the threads can be activated
    
    Result DoThreadAllocate();
    Result DoFamilyAllocate();
    Result DoFamilyCreate();
    Result DoThreadActivation();
    Result DoRegWrites();
    
public:
    // Processes
    Process p_ThreadAllocate;
    Process p_FamilyAllocate;
    Process p_FamilyCreate;
    Process p_ThreadActivation;
    Process p_RegWrites;

    ArbitratedService     p_allocation;     ///< Arbitrator for FamilyTable::AllocateFamily
    ArbitratedService     p_alloc;          ///< Arbitrator for m_alloc
    ArbitratedService     p_readyThreads;   ///< Arbitrator for m_readyThreads
    ArbitratedService     p_activeThreads;  ///< Arbitrator for m_activeThreads
    ThreadList            m_activeThreads;  ///< Queue of the active threads
};

}
#endif

