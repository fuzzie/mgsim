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
#ifndef ICACHE_H
#define ICACHE_H

#include "kernel.h"
#include "functions.h"
#include "Memory.h"

namespace Simulator
{

class Processor;
class Allocator;

class ICache : public IComponent
{
public:
	// Instruction Cache Configuration
	struct Config
	{
		size_t assoc;
		size_t sets;
		size_t lineSize;
	};

	// A Cache-line
    struct Line
    {
		bool		  used;			// Line used or empty?
        MemAddr       tag;			// Address tag
        char*         data;			// The line data
        CycleNo       access;		// Last access time (for LRU replacement)
		bool          creation;		// Is the family creation process waiting on this line?
        ThreadQueue	  waiting;		// Threads waiting on this line
		unsigned long references;	// Number of references to this line
		bool          fetched;		// To verify that we're actually reading a fetched line
	};

    ICache(Processor& parent, const std::string& name, Allocator& allocator, const Config& config);
    ~ICache();

    Result Fetch(MemAddr address, MemSize size, CID& cid);				// Initial family line fetch
    Result Fetch(MemAddr address, MemSize size, TID& tid, CID& cid);	// Thread code fetch
    bool   Read(CID cid, MemAddr address, void* data, MemSize size) const;
    bool   ReleaseCacheLine(CID bid);

    bool   OnMemoryReadCompleted(const MemData& data);

    // Ports
    ArbitratedWriteFunction p_request;

    // Admin information
    size_t      GetAssociativity() const { return m_assoc; }
    size_t      GetLineSize()      const { return m_lineSize; }
    size_t      GetNumLines()      const { return m_lines.size(); }
    size_t      GetNumSets()       const { return m_lines.size() / m_assoc; }
    const Line& GetLine(size_t i)  const { return m_lines[i];   }
    uint64_t    GetNumHits()       const { return m_numHits; }
    uint64_t    GetNumMisses()     const { return m_numMisses; }

private:
    Result Fetch(MemAddr address, MemSize size, TID* tid, CID* cid);
    Result FindLine(MemAddr address, Line* &line);

    Processor&          m_parent;
	Allocator&          m_allocator;
    size_t              m_lineSize;
    std::vector<Line>   m_lines;
	char*               m_data;
    uint64_t            m_numHits;
    uint64_t            m_numMisses;
    size_t              m_assoc;
    size_t              m_assocLog;
};

}
#endif

