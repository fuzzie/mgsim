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
#ifndef COMA_CACHE_H
#define COMA_CACHE_H

#include "Node.h"
#include <queue>
#include <set>

class Config;

namespace Simulator
{

class COMA::Cache : public COMA::Node
{
public:
    enum LineState
    {
        LINE_EMPTY,     ///< Empty, can be used.
        LINE_LOADING,   ///< Allocated, read request sent.
        LINE_FULL,      ///< Allocated and data present.
    };
    
    struct Line
    {
        LineState    state;     ///< State of the line
        MemAddr      tag;       ///< Tag of the line
        char*        data;      ///< Data of the line
        CycleNo      access;    ///< Last access time (for LRU replacement)
        unsigned int tokens;    ///< Number of tokens in this line
        bool         dirty;     ///< Dirty: line has been written to
        unsigned int updating;  ///< Number of REQUEST_UPDATEs pending on this line
        bool         valid[MAX_MEMORY_OPERATION_SIZE]; ///< Validity bitmask
    };

private:    
    struct Request : public MemData
    {
        bool         write;
        MemAddr      address;
        unsigned int client;
        TID          tid;
    };

    size_t                        m_lineSize;
    size_t                        m_assoc;
    size_t                        m_sets;
    size_t                        m_numCaches;
    CacheID                       m_id;
    std::vector<IMemoryCallback*> m_clients;
    ArbitratedService<>           p_lines;
    std::vector<Line>             m_lines;
    std::vector<char>             m_data;
    
    // Statistics
    uint64_t                      m_numHits;
    uint64_t                      m_numMisses;

    // Processes
    Process p_Requests;
    Process p_In;

    // Incoming requests from the processors
    // First arbitrate, then buffer (models a bus)
    ArbitratedService<> p_bus;
    Buffer<Request>     m_requests;
    Buffer<MemData>     m_responses;
    
    Line* FindLine(MemAddr address);
    Line* AllocateLine(MemAddr address, bool empty_only);
    bool  EvictLine(Line* line, const Request& req);

    // Processes
    Result DoRequests();
    Result DoReceive();

    Result OnReadRequest(const Request& req);
    Result OnWriteRequest(const Request& req);
    bool OnMessageReceived(Message* msg);
    bool OnReadCompleted(MemAddr addr, const MemData& data);
public:
    Cache(const std::string& name, COMA& parent, Clock& clock, CacheID id, size_t numCaches, const Config& config);
    
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
    const Line* FindLine(MemAddr address) const;
    
    void RegisterClient  (PSize pid, IMemoryCallback& callback, const Process* processes[]);
    void UnregisterClient(PSize pid);
    bool Read (PSize pid, MemAddr address, MemSize size);
    bool Write(PSize pid, MemAddr address, const void* data, MemSize size, TID tid);
};

}
#endif
