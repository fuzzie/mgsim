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
#include "Allocator.h"
#include "Processor.h"
#include "Pipeline.h"
#include "Network.h"
#include <cassert>
using namespace Simulator;
using namespace std;

RegAddr Allocator::getSharedAddress(SharedType stype, GFID fid, RegAddr addr) const
{
	const Family& family = m_familyTable[ m_familyTable.TranslateFamily(fid) ];
    RegIndex base = INVALID_REG_INDEX;
    
    if (family.state != FST_EMPTY)
    {
        switch (stype)
        {
            case ST_PARENT:
                // Return the shared address in the parent thread
                if (family.parent.tid != INVALID_TID)
                {
                    // This should only be used if the parent thread is on this CPU
                    assert(family.parent.pid == m_parent.getPID());

                    base = family.regs[addr.type].shareds;
                }
                break;

            case ST_FIRST:
                // Return the shared address for the first thread in the block
                if (family.firstThreadInBlock != INVALID_TID)
                {
					// Get base of remote dependency block
                    base = family.regs[addr.type].base + family.regs[addr.type].size - family.regs[addr.type].count.shareds;
                }
                break;
            
            case ST_LAST:
                // Return the shared address for the last thread in the block
                if (family.lastThreadInBlock != INVALID_TID)
                {
					// Get base of first thread's shareds
                    base = m_threadTable[family.lastThreadInBlock].regs[addr.type].base;
                }
                break;

            case ST_LOCAL:
                break;
        }
    }
    return MAKE_REGADDR(addr.type, (base != INVALID_REG_INDEX) ? base + addr.index : INVALID_REG_INDEX);
}

// Administrative function for getting a register's type and thread mapping
TID Allocator::GetRegisterType(LFID fid, RegAddr addr, RegGroup* group) const
{
    const Family& family = m_familyTable[fid];
    const Family::RegInfo& regs = family.regs[addr.type];

	if (addr.index >= regs.globals && addr.index < regs.globals + regs.count.globals)
	{
		// Globals
		*group = RG_GLOBAL;
		return INVALID_TID;
	}

	if (addr.index >= regs.base + regs.size - regs.count.shareds && addr.index < regs.base + regs.size)
	{
		// Remote dependency
		*group = RG_DEPENDENT;
		return INVALID_TID;
	}

	RegIndex index = (addr.index - regs.base) % (regs.count.locals + regs.count.shareds);
	RegIndex  base = addr.index - index;

	for (TID cur = family.members.head; cur != INVALID_TID;)
	{
		const Thread& thread = m_threadTable[cur];
		if (thread.regs[addr.type].base == base)
		{
			*group = (index < regs.count.shareds) ? RG_SHARED : RG_LOCAL;
			return cur;
		}
		cur = thread.nextMember;
	}
	*group = RG_LOCAL;
    return INVALID_TID;
}

//
// Adds the list of threads to the family's active queue.
// There is assumed to be a linked list between the @first
// and @last thread by Thread::nextState.
//
bool Allocator::queueActiveThreads(TID first, TID last)
{
    assert(first != INVALID_TID);
    assert(last  != INVALID_TID);

    COMMIT
    {
        // Append the waiting queue to the family's active queue
        if (m_activeThreads.head != INVALID_TID) {
            m_threadTable[m_activeThreads.tail].nextState = first;
        } else {
            m_activeThreads.head = first;
        }
        m_activeThreads.tail = last;

		assert(m_threadTable[m_activeThreads.tail].nextState == INVALID_TID);

        // Admin: Mark the threads as active
        for (TID cur = first; cur != INVALID_TID; cur = m_threadTable[cur].nextState)
        {
            m_threadTable[cur].state = TST_ACTIVE;
        }
    }
    return true;
}

//
// This is called by the Network to indicate that the first thread in
// a block has finished on the neighbouring processor.
//
bool Allocator::onRemoteThreadCompletion(LFID fid)
{
    Family& family = m_familyTable[fid];

    // The last thread in a family must exist for this notification
    assert(family.lastThreadInBlock != INVALID_TID);
    
    if (!markNextKilled(family.lastThreadInBlock))
    {
        return false;
    }
    
    COMMIT{ family.lastThreadInBlock = INVALID_TID; }
    return true;
}

bool Allocator::onRemoteThreadCleanup(LFID fid)
{
    Family& family = m_familyTable[fid];

    // The first thread in a block must exist for this notification
    if (family.state == FST_EMPTY || family.firstThreadInBlock == INVALID_TID)
    {
        return false;
    }
    assert(family.firstThreadInBlock != INVALID_TID);

    if (!markPrevCleanedUp(family.firstThreadInBlock))
    {
        return false;
    }
    
    COMMIT{ family.firstThreadInBlock = INVALID_TID; }
    return true;
}
//
// This is called by various components (RegisterFile, Pipeline, ...) to
// add the thread to the active queue. The component argument should point
// to the component making the request (for port privileges)
//
bool Allocator::activateThread(TID tid, const IComponent& component, MemAddr* pPC, LFID fid)
{
    // We need the port on the I-Cache to activate a thread
    if (!m_icache.p_request.invoke(component))
    {
        return false;
    }

    Thread& thread = m_threadTable[tid];

	// Check for overriden values
    MemAddr pc  = (pPC == NULL) ? thread.pc  : *pPC;
	if (fid == INVALID_LFID)
	{
		fid = thread.family;
	}

    // This thread doesn't have a Thread Instruction Buffer yet,
    // so try to get the cache line
    TID next = tid;
	CID cid;
	Result  result = SUCCESS;
    if ((result = m_icache.fetch(pc, sizeof(Instruction), next, cid)) == FAILED)
    {
        // We cannot fetch, abort activation
        return false;
    }

    // Save the (possibly overriden) program counter
    COMMIT
    {
        thread.pc  = pc;
	    thread.cid = cid;
	}

    if (result != SUCCESS)
    {
        COMMIT
        {
            // Request was delayed, link thread into waiting queue
            thread.nextState = next;
    
            // Mark the thread as waiting
            thread.state = TST_WAITING;
        }
    }
	else
	{
        COMMIT{ thread.nextState = INVALID_TID; }

        // The thread can be added to the family's active queue
        if (!queueActiveThreads(tid, tid))
        {
            return false;
        }
    }

	// Either way, the thread's in a queue now
	if (!increaseFamilyDependency(fid, FAMDEP_THREADS_QUEUED))
	{
		return false;
	}

    return true;
}

//
// Push the thread on the cleanup queue
//
bool Allocator::pushCleanup(TID tid)
{
    if (!m_cleanup.full())
    {
        COMMIT{ m_cleanup.push(tid); }
        return true;
    }
    return false;
}

bool Allocator::markNextKilled(TID tid)
{
    Thread& thread = m_threadTable[tid];
    if (thread.killed && thread.prevCleanedUp)
    {
        if (!pushCleanup(tid))
        {
            return false;
        }
    }
    
    COMMIT{ thread.nextKilled = true; }
    return true;
}

bool Allocator::markPrevCleanedUp(TID tid)
{
    Thread& thread = m_threadTable[tid];
    if (thread.killed && thread.nextKilled)
    {
        if (!pushCleanup(tid))
        {
            return false;
        }
    }
    
    COMMIT{ thread.prevCleanedUp = true; }
    return true;
}
//
// Kill the thread.
// This means it will be marked as killed and pushed to the
// cleanup queue should it also be marked for cleanup.
//
bool Allocator::killThread(TID tid)
{
    Thread& thread = m_threadTable[tid];
    Family& family = m_familyTable[thread.family];

    if (family.hasDependency && (family.gfid != INVALID_GFID || family.physBlockSize > 1))
    {
        // Mark 'next thread kill' on the previous thread
        if (thread.prevInBlock != INVALID_TID)
        {
            // Mark our predecessor for cleanup
            if (!markNextKilled(thread.prevInBlock))
            {
                return false;
            }
        }
        else if (!thread.isFirstThreadInFamily)
        {
            // Send remote notification 
            if (!m_network.sendThreadCompletion(family.gfid))
            {
                return false;
            }
        }
    }

    if (thread.nextKilled && thread.prevCleanedUp)
	{
		// This thread can be cleaned up
		if (!pushCleanup(tid))
		{
			return false;
		}
    }

    COMMIT
    {
        // Mark the thread as killed
        thread.cid    = INVALID_CID;
        thread.killed = true;
        thread.state  = TST_KILLED;
        family.nRunning--;
    }

    return true;
}

//
// Allocates a new thread to the specified thread slot.
// @isNew indicates if this a new thread for the family, or a recycled
// cleaned up one
//
bool Allocator::allocateThread(LFID fid, TID tid, bool isNewlyAllocated)
{
    // Work on a copy unless we're committing
    Family tmp_family; Family* family = &tmp_family;
    Thread tmp_thread; Thread* thread = &tmp_thread;
    if (committing())
    {
        family = &m_familyTable[fid];
        thread = &m_threadTable[tid];
    }
    else
    {
        tmp_family = m_familyTable[fid];
        tmp_thread = m_threadTable[tid];
    }
    
    // Initialize thread
    thread->isFirstThreadInFamily = (family->index == 0);
    thread->isLastThreadInFamily  = (family->step != 0 && family->index == family->nThreads - 1);
	thread->isLastThreadInBlock   = (family->gfid != INVALID_GFID && (family->index % family->virtBlockSize) == family->virtBlockSize - 1);

    thread->cid           = INVALID_CID;
    thread->killed        = false;
    thread->pc            = family->pc;
    thread->family        = fid;
    thread->index         = family->index;
    thread->prevInBlock   = INVALID_TID;
    thread->nextInBlock   = INVALID_TID;

    // These dependencies only hold for non-border threads in dependent families that are global or have more than one thread running.
    // So we already set them in the other cases.
	thread->nextKilled    = !family->hasDependency || thread->isLastThreadInFamily  || (family->gfid == INVALID_GFID && family->physBlockSize == 1);
    thread->prevCleanedUp = !family->hasDependency || thread->isFirstThreadInFamily || (family->gfid == INVALID_GFID && family->physBlockSize == 1);

    Thread* predecessor = NULL;
    if ((family->gfid == INVALID_GFID || family->index % family->virtBlockSize != 0) && family->physBlockSize > 1)
    {
        thread->prevInBlock = family->lastAllocated;
        if (thread->prevInBlock != INVALID_TID)
        {
            predecessor = &m_threadTable[thread->prevInBlock];
            COMMIT{ predecessor->nextInBlock = tid; }
        }
    }
	else if (family->physBlockSize == 1 && family->gfid == INVALID_GFID && thread->index > 0)
	{
		// We're not the first thread, in a family executing on one CPU, with a block size of one.
		// We are our own predecessor in terms of shareds.
		predecessor = &m_threadTable[tid];
	}

    // Set the register information for the new thread
    for (RegType i = 0; i < NUM_REG_TYPES; i++)
    {
        if (isNewlyAllocated)
        {
            thread->regs[i].base = family->regs[i].base + (RegIndex)family->allocated * (family->regs[i].count.locals + family->regs[i].count.shareds);
        }

        thread->regs[i].producer = (predecessor != NULL)
			? predecessor->regs[i].base		// Producer runs on the same processor (predecessor)
			: (family->gfid == INVALID_GFID
			   ? family->regs[i].shareds	// Producer runs on the same processor (parent)
			   : INVALID_REG_INDEX);		// Producer runs on the previous processor (parent or predecessor)

        if (family->regs[i].count.shareds > 0)
        {
            if (family->gfid != INVALID_GFID || family->physBlockSize > 1)
            {
                // Clear the thread's shareds
				RegValue value;
				value.m_state     = RST_PENDING;
				value.m_component = &m_pipeline.m_writeback;
                if (!m_registerFile.clear(MAKE_REGADDR(i, thread->regs[i].base), family->regs[i].count.shareds, value))
                {
                    return false;
                }
            }

            if (family->gfid != INVALID_GFID && thread->prevInBlock == INVALID_TID)
            {
                // Clear the thread's remote cache
				RegValue value;
				value.m_state     = RST_PENDING;
				value.m_component = &m_network;
                if (!m_registerFile.clear(MAKE_REGADDR(i, family->regs[i].base + family->regs[i].size - family->regs[i].count.shareds), family->regs[i].count.shareds, value))
                {
                    return false;
                }
            }
        }
    }

    // Write L0 to the register file
    if (family->regs[RT_INTEGER].count.locals > 0)
    {
        RegIndex L0   = thread->regs[RT_INTEGER].base + family->regs[RT_INTEGER].count.shareds;
        RegAddr  addr = MAKE_REGADDR(RT_INTEGER, L0);
        RegValue data;
        data.m_state   = RST_FULL;
        data.m_integer = family->start + family->index * family->step;

        if (!m_registerFile.p_asyncW.write(*this, addr))
        {
            return false;
        }

        if (!m_registerFile.writeRegister(addr, data, *this))
        {
            return false;
        }
    }

    if (isNewlyAllocated)
    {
        assert(family->allocated < family->physBlockSize);

		if (family->members.head == INVALID_TID && family->parent.pid == m_parent.getPID())
		{
			// We've created the first thread on the creating processor;
			// release the family's lock on the cache-line
			m_icache.releaseCacheLine(thread->cid);
		}

		// Add the thread to the family's member queue
        thread->nextMember = INVALID_TID;
        push(family->members, tid, &Thread::nextMember);

        // Increase the allocation count
        if (!increaseFamilyDependency(fid, FAMDEP_THREAD_COUNT))
        {
            return false;
        }
        family->nRunning++;
    }

    //
    // Update family information
    //
    if (thread->prevInBlock == INVALID_TID)
	{
		family->firstThreadInBlock = tid;
		DebugSimWrite("Set first thread in block for F%u to T%u (index %d)\n", fid, tid, thread->index);
	}
	
	if (thread->isLastThreadInBlock)
	{
		family->lastThreadInBlock  = tid;
	}

    family->lastAllocated = tid;

    // Increase the index and counts
    if (thread->isLastThreadInFamily)
    {
        // We've allocated the last thread
        family->allocationDone = true;
    }
    else if (++family->index % family->virtBlockSize == 0 && family->gfid != INVALID_GFID && m_procNo > 1)
    {
        // We've allocated the last in a block, skip to the next block
		family->index += (m_procNo - 1) * family->virtBlockSize;
		if (family->step != 0 && family->index >= family->nThreads)
        {
            // There are no more blocks for us
            family->allocationDone = true;
        }
    }

    if (!activateThread(tid, *this, &thread->pc, fid))
    {
        // Abort allocation
        return false;
    }

    DebugSimWrite("Allocated thread for F%u at T%u\n", fid, tid);
	if (family->allocationDone)
	{
		DebugSimWrite("Last thread for processor\n");
	}
    return true;
}

bool Allocator::killFamily(LFID fid, ExitCode code, RegValue value)
{
    Family& family = m_familyTable[fid];
    if (family.parent.tid != INVALID_TID)
    {
        if (family.parent.pid == m_parent.getPID())
        {
            // Write back the exit code
            if (family.exitCodeReg.valid())
            {
                COMMIT
                {
                    RegisterWrite write;
                    if (code != EXIT_NORMAL)
                    {
                        // Write the value first, if applicable
                        write.address = family.exitValueReg;
                        write.value   = value;
                        if (!m_registerWrites.push(write))
                        {
                            return false;
                        }
                    }
                    // Then the code
                    write.address = family.exitCodeReg;
                    write.value.m_state   = RST_FULL;
                    write.value.m_integer = code;
                    if (!m_registerWrites.push(write))
                    {
                        return false;
                    }
                }
            }
        }
        // Send completion to next processor
        else if (!m_network.sendFamilyCompletion(family.gfid))
        {
            return false;
        }
    }

    // Release registers
	RegIndex indices[NUM_REG_TYPES];
    for (RegType i = 0; i < NUM_REG_TYPES; i++)
    {
		indices[i] = family.regs[i].base;
	}

	if (!m_raunit.free(indices))
	{
		return false;
	}

    // Release member threads, if any
    if (family.members.head != INVALID_TID)
    {
        if (!m_threadTable.pushEmpty(family.members))
        {
            return false;
        }
    }

    COMMIT{ family.killed = true; }
    DebugSimWrite("Killed family %d (parent PID: %02x)\n", fid, family.parent.pid);
    return true;
}

bool Allocator::decreaseFamilyDependency(LFID fid, FamilyDependency dep)
{
    // We work on a copy unless we're committing
    Family  tmp_family;
    Family* family = &tmp_family;
    if (committing()) {
        family = &m_familyTable[fid];
    } else {
        tmp_family = m_familyTable[fid];
    }

    if (family->state == FST_EMPTY)
    {
        // We're trying to modify a family that �sn't yet allocated.
        // It is (hopefully) still in the create queue. Stall for a while.
        return false;
    }

    switch (dep)
    {
    case FAMDEP_THREAD_COUNT:        family->allocated--; break;
    case FAMDEP_OUTSTANDING_READS:   family->numPendingReads--; break;
    case FAMDEP_OUTSTANDING_WRITES:  family->numPendingWrites--; break;
    case FAMDEP_PREV_TERMINATED:     family->prevTerminated = true; break;
    case FAMDEP_OUTSTANDING_SHAREDS: family->numPendingShareds--; break;
	case FAMDEP_THREADS_QUEUED:		 family->numThreadsQueued--; break;
    case FAMDEP_THREADS_RUNNING:     family->numThreadsRunning--; break;
	case FAMDEP_CREATE_COMPLETED:    family->createCompleted = true; break;
    }

    switch (dep)
    {
    case FAMDEP_THREAD_COUNT:
    case FAMDEP_OUTSTANDING_WRITES:
    case FAMDEP_PREV_TERMINATED:
    case FAMDEP_OUTSTANDING_SHAREDS:
    case FAMDEP_THREADS_RUNNING:
	case FAMDEP_CREATE_COMPLETED:
        if (family->allocationDone && family->allocated == 0 && family->prevTerminated && family->numPendingWrites == 0 && family->numPendingShareds == 0 && family->numThreadsRunning == 0 && family->createCompleted)
        {
            // Family has terminated, 'kill' it
            RegValue value;
            value.m_state = RST_INVALID;
            if (!killFamily(fid, EXIT_NORMAL, value))
            {
                return false;
            }
        }

    case FAMDEP_OUTSTANDING_READS:
	case FAMDEP_THREADS_QUEUED:
		if (family->allocationDone && family->allocated == 0 && family->prevTerminated && family->numPendingWrites == 0 && family->numPendingShareds == 0 && family->numThreadsRunning == 0 && family->createCompleted &&
			family->numPendingReads == 0 && family->numThreadsQueued == 0)
        {
            // Family can be cleaned up, recycle family slot
            if (family->parent.pid == m_parent.getPID() && family->gfid != INVALID_GFID)
            {
                // We unreserve it if we're the processor that created it
                if (!m_network.sendFamilyUnreservation(family->gfid))
                {
                    return false;
                }

				if (!m_familyTable.UnreserveGlobal(family->gfid))
                {
                    return false;
                }
            }

            family->next = INVALID_LFID;
			if (!m_familyTable.FreeFamily(fid))
            {
                return false;
            }
            DebugSimWrite("Cleaned up family %d\n", fid);
        }
        break;
    }

    return true;
}

bool Allocator::increaseFamilyDependency(LFID fid, FamilyDependency dep)
{
    COMMIT
    {
        Family& family = m_familyTable[fid];
        switch (dep)
        {
        case FAMDEP_THREAD_COUNT:        family.allocated++; break;
        case FAMDEP_OUTSTANDING_READS:   family.numPendingReads++; break;
        case FAMDEP_OUTSTANDING_WRITES:  family.numPendingWrites++; break;
        case FAMDEP_OUTSTANDING_SHAREDS: family.numPendingShareds++; break;
        case FAMDEP_THREADS_RUNNING:     family.numThreadsRunning++; break;
		case FAMDEP_THREADS_QUEUED:		 family.numThreadsQueued++; break;
		case FAMDEP_CREATE_COMPLETED:
        case FAMDEP_PREV_TERMINATED:     break;
        }
    }
    return true;
}

Family& Allocator::GetWritableFamilyEntry(LFID fid, TID parent) const
{
	Family& family = m_familyTable[fid];
	if (family.created || family.parent.pid != m_parent.getPID() || family.parent.tid != parent)
	{
		// We only allow the parent thread (that allocated the entry) to fill it before creation
		throw InvalidArgumentException("Illegal family entry access");
	}
	return family;
}

void Allocator::SetDefaultFamilyEntry(LFID fid, TID parent) const
{
	assert(parent != INVALID_TID);

	COMMIT
	{
		Family&       family     = m_familyTable[fid];
		const Thread& thread     = m_threadTable[parent];
		const Family& parent_fam = m_familyTable[thread.family];

		family.created       = false;
		family.legacy        = false;
		family.start         = 0;
		family.end           = 0;
		family.step          = 1;
		family.virtBlockSize = 0;
		family.physBlockSize = 0;
		family.parent.pid    = m_parent.getPID();
		family.parent.tid    = parent;
		family.gfid          = 0; // Meaningless, but indicates group create
		family.place         = parent_fam.place; // Inherit place

		// By default, the globals and shareds are taken from the locals of the parent thread
		for (RegType i = 0; i < NUM_REG_TYPES; i++)
		{
			family.regs[i].globals = thread.regs[i].base + parent_fam.regs[i].count.shareds;
			family.regs[i].shareds = thread.regs[i].base + parent_fam.regs[i].count.shareds;
		}
	}
}

// Allocates a family entry. Returns the LFID (and SUCCESS) if one is available,
// DELAYED if none is available and the request is written to the buffer. FAILED is
// returned if the buffer was full.
Result Allocator::AllocateFamily(TID parent, RegIndex reg, LFID* fid)
{
	*fid = m_familyTable.AllocateFamily();
	if (*fid != INVALID_LFID)
	{
		// A family entry was free
		SetDefaultFamilyEntry(*fid, parent);
		return SUCCESS;
	}

	if (!m_allocations.full())
	{
		// The buffer is not full, place the request in the buffer
		COMMIT
		{
			AllocRequest request;
			request.parent = parent;
			request.reg    = reg;
			m_allocations.push(request);
		}
		return DELAYED;
	}
	return FAILED;
}

LFID Allocator::AllocateFamily(const CreateMessage& msg)
{
	LFID fid = m_familyTable.AllocateFamily(msg.fid);
	if (fid != INVALID_LFID)
	{
		// Copy the data
		Family& family = m_familyTable[fid];
		family.legacy        = false;
		family.start         = msg.start;
		family.step          = msg.step;
		family.nThreads      = msg.nThreads;
		family.parent        = msg.parent;
		family.virtBlockSize = msg.virtBlockSize;
		family.pc            = msg.address;
		for (RegType i = 0; i < NUM_REG_TYPES; i++)
		{
			family.regs[i].count = msg.regsNo[i];
		}

		// Initialize the family
		InitializeFamily(fid);

		// Allocate the registers
		if (!AllocateRegisters(fid))
		{
			return INVALID_LFID;
		}
	}
	return fid;
}

bool Allocator::ActivateFamily(LFID fid)
{
	const Family& family = m_familyTable[fid];
	if (!family.allocationDone)
	{
		push(m_alloc, fid);
	}

	if (!decreaseFamilyDependency(fid, FAMDEP_CREATE_COMPLETED))
	{
		return false;
	}
	return true;
}

void Allocator::InitializeFamily(LFID fid) const
{
	COMMIT
	{
		Family& family = m_familyTable[fid];

		bool global = (family.gfid != INVALID_GFID);

		family.state          = FST_IDLE;
		family.nRunning       = 0;
		family.members.head   = INVALID_TID;
		family.next           = INVALID_LFID;
		family.index          = (!global) ? 0 : ((m_procNo + m_parent.getPID() - (family.parent.pid + 1)) % m_procNo) * family.virtBlockSize;
		family.allocationDone = (family.index >= family.nThreads);

		// Dependencies
		family.allocated         = 0;
		family.killed            = false;
		family.createCompleted   = false;
		family.numPendingReads   = 0;
		family.numPendingWrites  = 0;
		family.numPendingShareds = 0;
		family.numThreadsRunning = 0;
		family.numThreadsQueued  = 0;
		family.prevTerminated    = (!global) || (m_parent.getPID() == (family.parent.pid + 1) % m_procNo);

		// Dependency information
		family.hasDependency      = false;
		family.lastThreadInBlock  = INVALID_TID;
		family.firstThreadInBlock = INVALID_TID;
		family.lastAllocated      = INVALID_TID;

		// Register bases
		bool remote = global && (m_parent.getPID() != family.parent.pid);
		for (RegType i = 0; i < NUM_REG_TYPES; i++)
		{
			family.regs[i].globals = (remote || family.regs[i].count.globals == 0) ? INVALID_REG_INDEX : family.regs[i].globals;
			family.regs[i].shareds = (remote || family.regs[i].count.shareds == 0) ? INVALID_REG_INDEX : family.regs[i].shareds + family.regs[i].count.globals;		
			family.numPendingShareds += family.regs[i].count.shareds;
		}

        // Calculate which CPU will run the last thread
        PID lastPID = (PID)(family.parent.pid + 1 + (family.nThreads - 1) / family.virtBlockSize) % m_procNo;
        PID pid     = m_parent.getPID();

        if (lastPID == family.parent.pid ||
            !((lastPID > family.parent.pid && (pid >= lastPID || pid < family.parent.pid)) ||
              (lastPID < family.parent.pid && (pid >= lastPID && pid < family.parent.pid))))
        {
             // Ignore the pending count if we're not a CPU that will send a parent shared
            family.numPendingShareds = 0;
        }
	}
}

bool Allocator::AllocateRegisters(LFID fid)
{
    // Try to allocate registers
	Family& family = m_familyTable[fid];

	bool global = (family.gfid != INVALID_GFID);

	for (FSize physBlockSize = min(m_threadTable.getNumThreads(), family.virtBlockSize); physBlockSize > 0; physBlockSize--)
    {
		// Calculate register requirements
		RegSize sizes[NUM_REG_TYPES];
        for (RegType i = 0; i < NUM_REG_TYPES; i++)
        {
            const Family::RegInfo& regs = family.regs[i];

            sizes[i] = (regs.count.locals + regs.count.shareds) * physBlockSize;
			if (regs.globals == INVALID_REG_INDEX) sizes[i] += regs.count.globals; // Add the cache for the globals
			if (global                           ) sizes[i] += regs.count.shareds; // Add the cache for the remote shareds
		}

		RegIndex indices[NUM_REG_TYPES];
		if (m_raunit.alloc(sizes, fid, indices))
		{
			// Success, we have registers for all types
			for (RegType i = 0; i < NUM_REG_TYPES; i++)
			{
				Family::RegInfo& regs = family.regs[i];
				COMMIT
				{
					regs.base            = INVALID_REG_INDEX;
					regs.size            = sizes[i];
					regs.latest          = INVALID_REG_INDEX;
					family.physBlockSize = physBlockSize;
					if (regs.count.shareds > 0)
					{
						family.hasDependency = true;
					}
				}

				if (sizes[i] > 0)
				{
					// Clear the allocated registers
					RegValue value;
					value.m_state = RST_EMPTY;
					m_registerFile.clear(MAKE_REGADDR(i, indices[i]), sizes[i], value);

					COMMIT
					{
						regs.base = indices[i];

						// Point globals and shareds to their local cache
						if (regs.globals == INVALID_REG_INDEX && regs.count.globals > 0) regs.globals = regs.base + regs.size - regs.count.shareds - regs.count.globals;
						if (regs.shareds == INVALID_REG_INDEX && regs.count.shareds > 0) regs.shareds = regs.base + regs.size - regs.count.shareds;
					}
				}
				DebugSimWrite("Allocated registers: %d at %04x\n", sizes[i], indices[i]);
			}
			return true;
        }
    }

    return false;
}

Result Allocator::onCycleReadPhase(int stateIndex)
{
    if (stateIndex == 0)
    {
        if (m_allocating == INVALID_LFID && m_alloc.head != INVALID_LFID)
        {
            // Get next family to allocate
			COMMIT{ m_allocating = pop(m_alloc); }
			return SUCCESS;
        }
    }
    return DELAYED;
}

bool Allocator::onCachelineLoaded(CID cid)
{
	assert(!m_creates.empty());
	COMMIT{
		m_createState = CREATE_LINE_LOADED;
		m_createLine  = cid;
	}
	return true;
}

bool Allocator::onReservationComplete()
{
	// The reservation has gone full circle, we can now resume the create
	assert(m_createState == CREATE_RESERVING_FAMILY);
	COMMIT{ m_createState = CREATE_BROADCASTING_CREATE; }
	DebugSimWrite("Reservation complete\n");
	return true;
}

Result Allocator::onCycleWritePhase(int stateIndex)
{
    switch (stateIndex)
    {
    case 0:
        //
        // Cleanup (reallocation) takes precedence over initial allocation
        //
        if (!m_cleanup.empty())
        {
            TID     tid    = m_cleanup.front();
            Thread& thread = m_threadTable[tid];
            LFID    fid    = thread.family;
            Family& family = m_familyTable[fid];

            assert(thread.state == TST_KILLED);

			if (family.hasDependency && !thread.isLastThreadInFamily && (family.gfid != INVALID_GFID || family.physBlockSize > 1))
            {
                // Mark 'previous thread cleaned up' on the next thread
                if (!thread.isLastThreadInBlock)
                {
                    assert(thread.nextInBlock != INVALID_TID);
                    if (!markPrevCleanedUp(thread.nextInBlock))
                    {
                        return FAILED;
                    }
                }
                else if (!m_network.sendThreadCleanup(family.gfid))
                {
                    return FAILED;
                }
            }

            if (family.allocationDone)
            {
                // With cleanup we don't do anything to the thread. We just forget about it.
                // It will be recycled once the family terminates.
                COMMIT{ thread.state = TST_UNUSED; }

                // Cleanup
                if (!decreaseFamilyDependency(fid, FAMDEP_THREAD_COUNT))
                {
                    return FAILED;
                }

				DebugSimWrite("Cleaned up T%u for F%u (index %lld)\n", tid, fid, thread.index);
			}
            else
            {
                // Reallocate thread
                if (!allocateThread(fid, tid, false))
                {
                    return FAILED;
                }

                if (family.allocationDone && m_allocating == fid)
                {
                    // Go to next family
                    COMMIT{ m_allocating = INVALID_LFID; }
                }
            }
            COMMIT{ m_cleanup.pop(); }
			return SUCCESS;
        }
        
		if (m_allocating != INVALID_LFID)
        {
            // Allocate an initial thread of a family
            Family& family = m_familyTable[m_allocating];

            TID tid = m_threadTable.popEmpty();
            if (tid == INVALID_TID)
            {
                return FAILED;
            }
            
            if (!allocateThread(m_allocating, tid))
            {
                return FAILED;
            }

            // Check if we're done with the initial allocation of this family
            if (family.allocated == family.physBlockSize || family.allocationDone)
            {
                // Yes, go to next family
                COMMIT{ m_allocating = INVALID_LFID; }
            }
			return SUCCESS;
        }
        break;

	case 1:
        if (!m_allocations.empty())
		{
			const AllocRequest& req = m_allocations.front();
			LFID fid = m_familyTable.AllocateFamily();
			if (fid == INVALID_LFID)
			{
				return FAILED;
			}
			
			// A family entry was free
			SetDefaultFamilyEntry(fid, req.parent);

            // Writeback the FID
            RegisterWrite write;
			write.address = MAKE_REGADDR(RT_INTEGER, req.reg);
			write.value.m_state   = RST_FULL;
			write.value.m_integer = fid;
            if (!m_registerWrites.push(write))
            {
                return FAILED;
            }
			COMMIT{m_allocations.pop();}
			return SUCCESS;
		}
		break;

	case 2:
		if (!m_creates.empty())
		{			
			LFID fid = m_creates.front();
			Family& family = m_familyTable[fid];

            if (m_createState == CREATE_INITIAL)
            {
				DebugSimWrite("Processing %s create for F%u\n", (family.gfid == INVALID_GFID) ? "local" : "group", fid);

				// Load the register counts from the family's first cache line
				Instruction counts;
				CID         cid;
				Result      result;
				if ((result = m_icache.fetch(family.pc - sizeof(Instruction), sizeof(counts), cid)) == FAILED)
				{
					return FAILED;
				}

				if (result == SUCCESS)
				{
					// Cache hit, process it
					onCachelineLoaded(cid);
				}
				else
				{
					// Cache miss, line is being fetched.
					// The I-Cache will notify us with onCachelineLoaded().
					COMMIT{ m_createState = CREATE_LOADING_LINE; }
				}
			}
			else if (m_createState == CREATE_LINE_LOADED)
			{
				// Read the cache-line
				Instruction counts;
				if (!m_icache.read(m_createLine, family.pc - sizeof(Instruction), &counts, sizeof(counts)))
				{
					return FAILED;
				}

				COMMIT
				{
				    counts = UnserializeInstruction(&counts);
					for (RegType i = 0; i < NUM_REG_TYPES; i++)
					{
					    Instruction c = counts >> (i * 16);
						family.regs[i].count.globals = (c >>  0) & 0x1F;
						family.regs[i].count.shareds = (c >>  5) & 0x1F;
						family.regs[i].count.locals  = (c >> 10) & 0x1F;
					}
				}
				if (family.gfid != INVALID_GFID)
				{
					// Global family, request the create token
					if (!m_network.requestToken())
					{
						return FAILED;
					}
					COMMIT{ m_createState = CREATE_GETTING_TOKEN; }
				}
				else
				{
					// Local family, skip straight to allocating registers
					COMMIT{ m_createState = CREATE_ALLOCATING_REGISTERS; }
				}

				InitializeFamily(fid);
				COMMIT{ family.gfid = INVALID_GFID; }
			}
			else if (m_createState == CREATE_HAS_TOKEN)
			{
				// We have the token (group creates only), pick a GFID and send around the reservation
				if ((family.gfid = m_familyTable.AllocateGlobal(fid)) == INVALID_GFID)
				{
					// No global ID available
					return FAILED;
				}

				if (!m_network.sendFamilyReservation(family.gfid))
				{
					// Send failed
					return FAILED;
				}

				COMMIT{ m_createState = CREATE_RESERVING_FAMILY; }
				// The network will notify us with onReservationComplete() and advanced to CREATE_BROADCASTING_CREATE
			}
			else if (m_createState == CREATE_BROADCASTING_CREATE)
			{
				// We have the token, and the family is globally reserved; broadcast the create
				if (!m_network.sendFamilyCreate(fid))
				{
					return FAILED;
				}

				// Advance to next stage
				COMMIT{ m_createState = CREATE_ALLOCATING_REGISTERS; }
			}
			else if (m_createState == CREATE_ALLOCATING_REGISTERS)
			{
				// Allocate the registers
				if (!AllocateRegisters(fid))
				{
					return FAILED;
				}

				// Family's now created, we can start creating threads
				if (!ActivateFamily(fid))
				{
					return FAILED;
				}

			    COMMIT{
			        m_creates.pop();
    				m_createState = CREATE_INITIAL;
    			}
            }
		    return SUCCESS;
		}
        break;

    case 3:
		if (!m_registerWrites.empty())
        {
            if (!m_registerFile.p_asyncW.write(*this, m_registerWrites.front().address))
            {
                return FAILED;
            }

            if (!m_registerFile.writeRegister(m_registerWrites.front().address, m_registerWrites.front().value, *this))
            {
                return FAILED;
            }

            COMMIT{ m_registerWrites.pop(); }
			return SUCCESS;
        }
        break;
    }

    return DELAYED;
}

bool Allocator::onTokenReceived()
{
    // The network told us we can create this family (group family, local create)
    assert(!m_creates.empty());
	assert(m_createState == CREATE_GETTING_TOKEN);
	COMMIT{ m_createState = CREATE_HAS_TOKEN; }
    return true;
}

// Local creates
bool Allocator::queueCreate(LFID fid, MemAddr address, TID parent, RegAddr exitCodeReg)
{
	assert(parent != INVALID_TID);

    if (m_creates.full())
    {
		return false;
	}

	COMMIT
    {
		Family& family = GetWritableFamilyEntry(fid, parent);

		// Store the information
		family.pc           = (address & -(int)m_icache.getLineSize()) + 2 * sizeof(Instruction); // Skip control and reg count words
		family.exitCodeReg  = exitCodeReg;
	    family.exitValueReg = INVALID_REG;

		if (m_procNo == 1)
		{
			// Force to local
			family.gfid = INVALID_GFID;
		}

		// Sanitize the family entry
		uint64_t blockSize = family.virtBlockSize;
		if (family.step != 0)
		{
		    // Finite family
			family.nThreads = (family.step < 0)
				? (((uint64_t)-(family.end - family.start) + 1 - family.step - 1) / -family.step)
				: (((uint64_t) (family.end - family.start) + 1 + family.step - 1) /  family.step);
			
			if (family.nThreads == 1)
			{
				blockSize = 1;
			}

			if (blockSize <= 0)
			{
				blockSize = (family.gfid == INVALID_GFID)
					? family.nThreads								// Local family, take as many as possible
					: (family.nThreads + m_procNo - 1) / m_procNo;	// Group family, divide threads evenly
			}
			else if (family.nThreads <= blockSize)
			{
				// Force to local
				family.gfid = INVALID_GFID;
			}
		}
		else
		{
			// Infinite family

			// As close as we can get to infinity
			family.nThreads = UINT64_MAX;
			if (blockSize <= 0)
			{
				blockSize = UINT64_MAX;
			}
		}
		family.virtBlockSize = (TSize)min(min(family.nThreads, blockSize), (uint64_t)m_threadTable.getNumThreads());
		
		// Lock the family
		family.created = true;

		// Push the create
		m_creates.push(fid);
    }
	DebugSimWrite("Queued local create by T%u at %08llx\n", parent, address);
    return true;
}

Allocator::Allocator(Processor& parent, const string& name,
    FamilyTable& familyTable, ThreadTable& threadTable, RegisterFile& registerFile, RAUnit& raunit, ICache& icache, Network& network, Pipeline& pipeline,
    PSize procNo, const Config& config)
:
    IComponent(&parent, parent.getKernel(), name, 4),
    p_cleanup(parent.getKernel()),
    m_parent(parent), m_familyTable(familyTable), m_threadTable(threadTable), m_registerFile(registerFile), m_raunit(raunit), m_icache(icache), m_network(network), m_pipeline(pipeline),
    m_procNo(procNo), m_creates(config.localCreatesSize), m_registerWrites(INFINITE), m_cleanup(config.cleanupSize),
	m_allocations(INFINITE), m_createState(CREATE_INITIAL)
{
    m_allocating   = INVALID_LFID;
    m_alloc.head   = INVALID_LFID;
    m_alloc.tail   = INVALID_LFID;
    m_activeThreads.head  = INVALID_TID;
    m_activeThreads.tail  = INVALID_TID;
}

void Allocator::allocateInitialFamily(MemAddr pc, bool legacy)
{
    static const int InitialRegisters[NUM_REG_TYPES] = {31, 31};

	LFID fid = m_familyTable.AllocateFamily();
	if (fid == INVALID_LFID)
	{
		throw Exception("Unable to create initial family");
	}

	Family& family = m_familyTable[fid];
	family.start         = 0;
    family.step          = 1;
	family.nThreads      = 1;
	family.virtBlockSize = 1;
	family.physBlockSize = 0;
	family.parent.tid    = INVALID_TID;
	family.parent.pid    = m_parent.getPID();
	family.exitCodeReg   = INVALID_REG;
    family.exitValueReg  = INVALID_REG;
	family.place         = 1; // Set initial place to the whole group
	family.created       = true;
	family.gfid          = INVALID_GFID;
	family.legacy        = legacy;
	family.pc            = pc;
	if (!family.legacy) {
		family.pc = (family.pc & -(int)m_icache.getLineSize()) + sizeof(Instruction); // Align and skip control word
	}

	for (RegType i = 0; i < NUM_REG_TYPES; i++)
    {
		family.regs[i].count.locals  = InitialRegisters[i];
        family.regs[i].count.globals = 0;
        family.regs[i].count.shareds = 0;
        family.regs[i].globals = INVALID_REG_INDEX;
        family.regs[i].shareds = INVALID_REG_INDEX;
    };

	InitializeFamily(fid);

	// Allocate the registers
	if (!AllocateRegisters(fid))
	{
		throw Exception("Unable to create initial family");
	}

	if (!ActivateFamily(fid))
	{
		throw Exception("Unable to create initial family");
	}
}

bool Allocator::idle() const
{
    return (m_creates.size() == 0 &&
            m_registerWrites.size() == 0 &&
			m_activeThreads.head == INVALID_TID && 
            m_cleanup.size() == 0);
}

void Allocator::push(FamilyQueue& q, LFID fid)
{
    COMMIT
    {
        if (q.head == INVALID_LFID) {
            q.head = fid;
        } else {
            m_familyTable[q.tail].next = fid;
        }
        q.tail = fid;
    }
}

void Allocator::push(ThreadQueue& q, TID tid, TID Thread::*link)
{
    COMMIT
    {
        if (q.head == INVALID_TID) {
            q.head = tid;
        } else {
            m_threadTable[q.tail].*link = tid;
        }
        q.tail = tid;
    }
}

LFID Allocator::pop(FamilyQueue& q)
{
    LFID fid = q.head;
    if (q.head != INVALID_LFID)
    {
        q.head = m_familyTable[fid].next;
    }
    return fid;
}

TID Allocator::pop(ThreadQueue& q, TID Thread::*link)
{
    TID tid = q.head;
    if (q.head != INVALID_TID)
    {
        q.head = m_threadTable[tid].*link;
    }
    return tid;
}
