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
#ifndef THREADTABLE_H
#define THREADTABLE_H

#include "ports.h"

class Config;

namespace Simulator
{

class Processor;

struct Thread
{
    // Register information
    struct RegInfo
    {
        // The base to this thread's locals, dependents and shareds
        RegIndex locals;
        RegIndex dependents;
        RegIndex shareds;
    };
    
    // The dependencies that must be resolved before a thread can be cleaned up
    struct Dependencies
    {
        /*
         Obviously the thread must have terminated before it can be cleaned up.
        */
        bool killed;
        
        /*
         Threads can terminate out of index order. This leads to problems
         when figuring out which thread is the logical predecessor of the
         next created thread.
         For local creates, the family's lastAllocated field can be used
         to find the last allocated thread, which is always the predecessor
         of the next created thread. However, in group creates, threads must
         be linked up across cores. The family's firstInBlock field is
         used for this. However, if threads complete out of order,
         thise field can be overwritten with the next block's first
         thread, causing the border thread on the previous core to link
         up with the wrong thread.
         To counter this, sequential thread cleanup is enforced by introducing
         a dependency on the previous thread's cleanup event.
        */
        bool prevCleanedUp;
        
        /*
         All writes made by a thread are tagged with the thread's TID. Thus,
         the thread cannot be cleaned up until those writes have been
         confirmed by the memory system.
        */
        unsigned int numPendingWrites;
    };
    
    MemAddr      pc;
    RegInfo      regs[NUM_REG_TYPES];
    Dependencies dependencies;
    bool         isLastThreadInBlock;
    bool         isFirstThreadInFamily;
    bool         isLastThreadInFamily;
    bool         waitingForWrites;
    TID          nextInBlock;
    CID          cid;
    LFID         family;
    TID          nextState;
    TID          nextMember;

    // Architecture specific per-thread stuff
#if TARGET_ARCH == ARCH_ALPHA
	FPCR         fpcr;
#elif TARGET_ARCH == ARCH_SPARC
    PSR          psr;
    FSR          fsr;
    uint32_t     Y;
#endif    

    // Admin
    uint64_t    index;
    ThreadState state;
};

class ThreadTable : public Object
{
public:
    ThreadTable(const std::string& name, Processor& parent, const Config& config);

    TSize GetNumThreads() const { return m_threads.size(); }

    typedef Thread value_type;
          Thread& operator[](TID index)       { return m_threads[index]; }
    const Thread& operator[](TID index) const { return m_threads[index]; }

    TID   PopEmpty(ContextType type);
    void  PushEmpty(const ThreadQueue& queue, ContextType context);
    void  ReserveThread();
    void  UnreserveThread();
    TSize GetNumFreeThreads() const;
    
    bool IsEmpty() const;
    
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

private:
    ThreadQueue         m_empty;
    std::vector<Thread> m_threads;
    TSize               m_free[NUM_CONTEXT_TYPES];
};

}
#endif

