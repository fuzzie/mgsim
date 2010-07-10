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
#ifndef FAMILYTABLE_H
#define FAMILYTABLE_H

#include "ports.h"
#include <queue>

class Config;

namespace Simulator
{

class Processor;

struct Family
{
    enum Type
    {
        LOCAL,
        GROUP,
    };

    struct RegInfo
    {   
        RegsNo   count;            // Number of globals, locals and shareds
        RegIndex base;             // Base address of this family's register block
        RegSize  size;             // Size of the allocated registers (could be calculated from other values)
        RegIndex last_shareds;     // Address of the last allocated thread's shareds
        RegIndex first_dependents; // Address of the dependents of the first thread in the block
    };
    
    // Groups all dependencies that need to be resolved before termination and/or cleanup
    struct Dependencies
    {
        /*
         All threads in the family must have been allocated before the family is done
        */
        bool allocationDone;
        
        /*
         The parent threads needs to be notified of the termination of the
         family. This is done by having each family notify the family on the
         next core once itself and its predecessor has terminated. This flag
         indicates the family has terminated on all preceding cores.
         Eventually this will cause this flag to be set on the family on the
         parent core.
        */
        bool prevSynchronized;
        
        /*
         After synchronizing on the family's termination, the parent thread
         can still read the final shareds back from the last thread's context.
         Therefore, we cannot cleanup a family until the parent has
         explicitely detached from it.
        */
        bool detached;
        
        /*
         All allocated threads (0 <= allocated <= physBlockSize) must have
         been cleaned up before the family has terminated.
        */
        TSize numThreadsAllocated;
        
        /*
         FIXME:
         Maybe this can be generalized to a thread not being cleaned up
         before all pending operations made by it have completed. Then
         this dependency becomes implicit in the numThreadsAllocated
         dependency.
        */
        unsigned int numPendingReads;
	};

    Type         type;           // The type of the family
    FCapability  capability;     // Capability value for security
    MemAddr      pc;             // Initial PC for newly created threads
	bool         legacy;		  // Consists of a single thread of legacy code?
    Integer      virtBlockSize;  // Virtual block size
    TSize        physBlockSize;  // Physical block size, <= Virtual block size, depending on the amount of free registers
    SInteger     start;          // Start index of the family
    SInteger     step;           // Step size of the family
	PlaceType    place;		  // Place type of this family
	bool         infinite;       // Is this an infinite family?
	union {
	    Integer  nThreads;       // Number of threads we need to allocate
		SInteger limit;		     // Limit of the family
	};
    Integer      index;          // Index of the next to be allocated thread (0, 1, 2... nThreads-1)
    LPID         parent_lpid;    // Parent core in group
    bool         hasDependency;  // Does this family use shareds?
    Dependencies dependencies;   // The dependencies for termination and cleanup
    ThreadQueue  members;        // Queue of all threads in this family
    LFID         link_next;      // The LFID of the matching family on the next CPU
    LFID         link_prev;      // The LFID of the matching family on the previous CPU
    bool         hasLastThread;  // Does this core have the last thread of this family?
    bool         prevCleanedUp;  // Last thread has been cleaned up
    
    struct
    {
        ExitCode code;           // The exit code of the family
        GPID     pid;            // The core that's synchronising
        RegIndex reg;            // The exit code register on the core
    }            sync;           // Synchronisation information
    
    TID          lastAllocated;
    bool         lastAllocatedIsLastThreadInBlock;
    TID          firstThreadInBlock;

    RegInfo      regs[NUM_REG_TYPES];    // Register information

    // Admin
    FamilyState  state;          // Family state
};

class FamilyTable : public Object
{
public:
    FamilyTable(const std::string& name, Processor& parent, const Config& config);

    FSize GetNumFamilies() const { return m_families.size(); }

    typedef Family value_type;
          Family& operator[](LFID fid)       { return m_families[fid]; }
	const Family& operator[](LFID fid) const { return m_families[fid]; }

	LFID  AllocateFamily(ContextType type);
    void  FreeFamily(LFID fid, ContextType context);
    void  ReserveFamily();
    
    FSize GetNumFreeFamilies()  const;
    bool  IsEmpty()             const;
    bool  IsExclusive(LFID fid) const { return fid + 1 == m_families.size(); }
    bool  IsExclusiveUsed()     const { return m_free[CONTEXT_EXCLUSIVE] == 0; }
    
    // Admin functions
    const std::vector<Family>& GetFamilies() const { return m_families; }

    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

    // Stats
    FSize GetTotalAllocated() const { return m_totalalloc; }
    TSize GetMaxAllocated() const { return m_maxalloc; }

private:
    Processor&          m_parent;
    std::vector<Family> m_families;
    FSize               m_free[NUM_CONTEXT_TYPES];
    FSize               m_totalalloc;
    FSize               m_maxalloc;
    CycleNo             m_lastcycle;

    void UpdateStats();    
    void CheckStateSanity() const;
};

}
#endif

