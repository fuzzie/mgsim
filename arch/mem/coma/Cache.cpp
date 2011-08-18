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
#include "Cache.h"
#include "sim/config.h"
#include "sim/sampling.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <iomanip>
using namespace std;

namespace Simulator
{

// When we insert a message into the ring, we want at least one slots
// available in the buffer to avoid deadlocking the ring network. This
// is not necessary for forwarding messages.
static const size_t MINSPACE_INSERTION = 2;
static const size_t MINSPACE_FORWARD   = 1;

MCID COMA::Cache::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage)
{
    MCID index = m_clients.size();
    m_clients.resize(index + 1);

    m_clients[index] = &callback;
    
    p_bus.AddCyclicProcess(process);
    traces = m_requests;
    
    m_storages *= opt(storage);
    p_Requests.SetStorageTraces(m_storages ^ GetOutgoingTrace());
    p_In.SetStorageTraces(opt(m_storages ^ GetOutgoingTrace()));

    return index;
}

void COMA::Cache::UnregisterClient(MCID id)
{
    assert(m_clients[id] != NULL);
    m_clients[id] = NULL;
}

// Called from the processor on a memory read (typically a whole cache-line)
// Just queues the request.
bool COMA::Cache::Read(MCID id, MemAddr address, MemSize size)
{
    if (size != m_lineSize)
    {
        throw InvalidArgumentException("Read size is not a single cache-line");
    }

    assert(size <= MAX_MEMORY_OPERATION_SIZE);
    assert(address % m_lineSize == 0);
    assert(size == m_lineSize);
    
    if (address % m_lineSize != 0)
    {
        throw InvalidArgumentException("Read address is not aligned to a cache-line");
    }
    
    // We need to arbitrate between the different processes on the cache,
    // and then between the different clients. There are 2 arbitrators for this.
    if (!p_bus.Invoke())
    {
        // Arbitration failed
        DeadlockWrite("Unable to acquire bus for read");
        return false;
    }
    
    Request req;
    req.address = address;
    req.write   = false;
    req.size    = size;
    
    // Client should have been registered
    assert(m_clients[id] != NULL);

    if (!m_requests.Push(req))
    {
        // Buffer was full
        DeadlockWrite("Unable to push read request into buffer");
        return false;
    }
    
    return true;
}

// Called from the processor on a memory write (can be any size with write-through/around)
// Just queues the request.
bool COMA::Cache::Write(MCID id, MemAddr address, const void* data, MemSize size, TID tid)
{
    if (size > m_lineSize || size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Write size is too big");
    }

    if (address / m_lineSize != (address + size - 1) / m_lineSize)
    {
        throw InvalidArgumentException("Write request straddles cache-line boundary");
    }

    // We need to arbitrate between the different processes on the cache,
    // and then between the different clients. There are 2 arbitrators for this.
    if (!p_bus.Invoke())
    {
        // Arbitration failed
        DeadlockWrite("Unable to acquire bus for write");
        return false;
    }
    
    Request req;
    req.address = address;
    req.write   = true;
    req.size    = size;
    req.client  = id;
    req.tid     = tid;
    memcpy(req.data, data, (size_t)size);
    
    // Client should have been registered
    assert(m_clients[req.client] != NULL);
    
    if (!m_requests.Push(req))
    {
        // Buffer was full
        DeadlockWrite("Unable to push write request into buffer");
        return false;
    }
    
    // Snoop the write back to the other clients
    for (size_t i = 0; i < m_clients.size(); ++i)
    {
        IMemoryCallback* client = m_clients[i];
        if (client != NULL && i != req.client)
        {
            if (!client->OnMemorySnooped(req.address, req))
            {
                DeadlockWrite("Unable to snoop data to cache clients");
                return false;
            }
        }
    }
    
    return true;
}

// Attempts to find a line for the specified address.
COMA::Cache::Line* COMA::Cache::FindLine(MemAddr address)
{
    MemAddr tag;
    size_t  setindex;
    m_selector.Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

    // Find the line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line& line = m_lines[set + i];
        if (line.state != LINE_EMPTY && line.tag == tag)
        {
            // The wanted line was in the cache
            return &line;
        }
    }
    return NULL;
}

// Attempts to find a line for the specified address.
const COMA::Cache::Line* COMA::Cache::FindLine(MemAddr address) const
{
    MemAddr tag;
    size_t  setindex;
    m_selector.Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

    // Find the line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        const Line& line = m_lines[set + i];
        if (line.state != LINE_EMPTY && line.tag == tag)
        {
            // The wanted line was in the cache
            return &line;
        }
    }
    return NULL;
}

// Attempts to allocate a line for the specified address.
// If empty_only is true, only empty lines will be considered.
COMA::Cache::Line* COMA::Cache::AllocateLine(MemAddr address, bool empty_only, MemAddr* ptag)
{
    MemAddr tag;
    size_t  setindex;
    m_selector.Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

    // Find the line
    Line* empty   = NULL;
    Line* replace = NULL;
    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line& line = m_lines[set + i];
        if (line.state == LINE_EMPTY)
        {
            // Empty, unused line, remember this one
            DeadlockWrite("New line, tag %#016llx: allocating empty line %zu from set %zu", (unsigned long long)tag, i, setindex);
            empty = &line;
        }
        else if (!empty_only)
        {
            // We're also considering non-empty lines; use LRU
            assert(line.tag != tag);
            DeadlockWrite("New line, tag %#016llx: considering busy line %zu from set %zu, tag %#016llx, state %u, updating %u, access %llu",
                          (unsigned long long)tag,
                          i, setindex,
                          (unsigned long long)line.tag, (unsigned)line.state, (unsigned)line.updating, (unsigned long long)line.access);
            if (line.state != LINE_LOADING && line.updating == 0 && (replace == NULL || line.access < replace->access))
            {
                // The line is available to be replaced and has a lower LRU rating,
                // remember it for replacing
                replace = &line;
            }
        }
    }
    
    // The line could not be found, allocate the empty line or replace an existing line
    if (ptag) *ptag = tag;
    return (empty != NULL) ? empty : replace;
}

bool COMA::Cache::EvictLine(Line* line, const Request& req)
{
    // We never evict loading or updating lines
    assert(line->state != LINE_LOADING);
    assert(line->updating == 0);

    size_t setindex = (line - &m_lines[0]) / m_assoc;
    MemAddr address = m_selector.Unmap(line->tag, setindex) * m_lineSize;
    
    TraceWrite(address, "Evicting with %u tokens due to miss for address %#016llx", line->tokens, (unsigned long long)req.address);
    
    Message* msg = NULL;
    COMMIT
    {
        msg = new Message;
        msg->type      = Message::EVICTION;
        msg->address   = address;
        msg->ignore    = false;
        msg->sender    = m_id;
        msg->tokens    = line->tokens;
        msg->data.size = m_lineSize;
        msg->dirty     = line->dirty;
        memcpy(msg->data.data, line->data, m_lineSize);
    }
    
    if (!SendMessage(msg, MINSPACE_INSERTION))
    {
        DeadlockWrite("Unable to buffer eviction request for next node");
        return false;
    }
    
    // Send line invalidation to caches
    if (!p_bus.Invoke())
    {
        DeadlockWrite("Unable to acquire the bus for sending invalidation");
        return false;
    }

    for (std::vector<IMemoryCallback*>::const_iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (*p != NULL && !(*p)->OnMemoryInvalidated(address))
        {
            DeadlockWrite("Unable to send invalidation to clients");
            return false;
        }
    }
    
    COMMIT{ line->state = LINE_EMPTY; }
    return true;
}

// Called when a message has been received from the previous node in the chain
bool COMA::Cache::OnMessageReceived(Message* msg)
{
    assert(msg != NULL);

    // We need to grab p_lines because it also arbitrates access to the
    // outgoing ring buffer.    
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Unable to acquire lines");
        return false;
    }
    
    if (msg->ignore || (msg->type == Message::REQUEST_DATA_TOKEN && msg->sender != m_id))
    {
        // This is either
        // * a message that should ignored, or
        // * a read response that has not reached its origin yet
        // Just forward it
        if (!SendMessage(msg, MINSPACE_FORWARD))
        {
            DeadlockWrite("Unable to buffer forwarded request for next node");
            return false;
        }
        return true;
    }
    
    Line* line = FindLine(msg->address);   
    switch (msg->type)
    {
    case Message::REQUEST:
    case Message::REQUEST_DATA:
        // Some cache had a read miss. See if we have the line.
        assert(msg->data.size == m_lineSize);
        
        if (line != NULL && line->state == LINE_FULL)
        {
            // We have a copy of the line
            if (line->tokens > 1)
            {
                // We can give the request data and tokens
                TraceWrite(msg->address, "Received Read Request; Attaching data and tokens");
                        
                COMMIT
                {
                    msg->type   = Message::REQUEST_DATA_TOKEN;
                    msg->tokens = 1;
                    msg->dirty  = line->dirty;
                    memcpy(msg->data.data, line->data, msg->data.size);

                    line->tokens -= msg->tokens;
                                
                    // Also update last access time.
                    line->access = GetKernel()->GetCycleNo();
                }
            }
            else if (msg->type == Message::REQUEST)
            {
                // We can only give the request data, not tokens
                TraceWrite(msg->address, "Received Read Request; Attaching data");

                COMMIT
                {
                    msg->type  = Message::REQUEST_DATA;
                    msg->dirty = line->dirty;
                    memcpy(msg->data.data, line->data, msg->data.size);
                }
            }

            // Statistics
            COMMIT{ ++m_numNetworkRHits; }
        }

        // Forward the message.
        if (!SendMessage(msg, MINSPACE_FORWARD))
        {
            DeadlockWrite("Unable to buffer request for next node");
            return false;
        }
        break;

    case Message::REQUEST_DATA_TOKEN:
    {
        // We received a line for a previous read miss on this cache
        assert(line != NULL);
        assert(line->state == LINE_LOADING);
        assert(msg->tokens > 0);
        assert(line->tokens == 0);

        TraceWrite(msg->address, "Received Read Response with %u tokens", msg->tokens);
        
        COMMIT
        {
            // Store the data, masked by the already-valid bitmask
            for (size_t i = 0; i < msg->data.size; ++i)
            {
                if (!line->valid[i])
                {
                    line->data[i] = msg->data.data[i];
                    line->valid[i] = true;
                }
                else
                {
                    // This byte has been overwritten by processor.
                    // Update the message. This will ensure the response
                    // gets the latest value, and other processors too
                    // (which is fine, according to non-determinism).
                    msg->data.data[i] = line->data[i];
                }
            }
            line->state  = LINE_FULL;
            line->tokens = msg->tokens;
            line->dirty  = msg->dirty || line->dirty;
        }
        
        /*
         Put the data on the bus for the processors.
         Merge with pending writes first so we don't accidentally give some
         other processor on the bus old data after its write.
         This is kind of a hack; it's feasibility in hardware in a single cycle
         is questionable.
        */
        MemData data(msg->data);
        COMMIT
        {
            for (Buffer<Request>::const_iterator p = m_requests.begin(); p != m_requests.end(); ++p)
            {
                unsigned int offset = p->address % m_lineSize;
                if (p->write && p->address - offset == msg->address)
                {
                    // This is a write to the same line, merge it
                    std::copy(p->data, p->data + p->size, data.data + offset);
                }
            }
        }

        if (!OnReadCompleted(msg->address, data))
        {
            DeadlockWrite("Unable to notify clients of read completion");
            return false;
        }
        
        COMMIT{ delete msg; }
        break;
    }
    
    case Message::EVICTION:
        if (line != NULL)
        {
            if (line->state == LINE_FULL)
            {
                // We have the line, merge it.
                TraceWrite(msg->address, "Merging Evict Request with %u tokens into line with %u tokens", msg->tokens, line->tokens);
            
                // Just add the tokens count to the line.
                assert(msg->tokens > 0);
                COMMIT
                {
                    line->tokens += msg->tokens;
                
                    // Combine the dirty flags
                    line->dirty = line->dirty || msg->dirty;
                
                    delete msg;

                    // Statistics
                    ++m_numMergedEvictions;
                }
                break;
            }
        }
        // We don't have the line, see if we have an empty spot
        else
        {
            MemAddr tag;
            if ((line = AllocateLine(msg->address, true, &tag)) != NULL)
            {
                // Yes, place the line there
                TraceWrite(msg->address, "Storing Evict Request for line %#016llx locally with %u tokens", (unsigned long long)msg->address, msg->tokens);
                
                COMMIT
                {
                    line->state    = LINE_FULL;
                    line->tag      = tag;
                    line->tokens   = msg->tokens;
                    line->dirty    = msg->dirty;
                    line->updating = 0;
                    line->access   = GetKernel()->GetCycleNo();
                    std::fill(line->valid, line->valid + MAX_MEMORY_OPERATION_SIZE, true);
                    memcpy(line->data, msg->data.data, msg->data.size);

                    delete msg; 

                    // Statistics
                    ++m_numInjectedEvictions;
                }
                break;
            }
        }

        // Just forward it
        if (!SendMessage(msg, MINSPACE_FORWARD))
        {
            DeadlockWrite("Unable to buffer forwarded eviction request for next node");
            return false;
        }
        break;

    case Message::UPDATE:
        if (msg->sender == m_id)
        {
            // The update has come full circle.
            // Notify the sender of write consistency.
            assert(line != NULL);
            assert(line->updating > 0);
            
            if (!m_clients[msg->client]->OnMemoryWriteCompleted(msg->tid))
            {
                return false;
            }

            COMMIT
            {
                line->updating--;
                delete msg;
            }
        }
        else
        {
            // Update the line, if we have it, and forward the message
            if (line != NULL)
            {
                COMMIT
                {
                    unsigned int offset = msg->address % m_lineSize;
                    memcpy(line->data + offset, msg->data.data, msg->data.size);
                    std::fill(line->valid + offset, line->valid + offset + msg->data.size, true);

                    // Statistics
                    ++m_numNetworkWHits;
                }
            
                // Send the write as a snoop to the processors
                for (size_t i = 0; i < m_clients.size(); ++i)
                {
                    if (m_clients[i] != NULL)
                    {
                        if (!m_clients[i]->OnMemorySnooped(msg->address, msg->data))
                        {
                            DeadlockWrite("Unable to snoop update to cache clients");
                            return false;
                        }
                    }
                }
            }
            
            if (!SendMessage(msg, MINSPACE_FORWARD))
            {
                DeadlockWrite("Unable to buffer forwarded update request for next node");
                return false;
            }
        }
        break;
        
    default:
        assert(false);
        break;    
    }
    return true;
}
    
bool COMA::Cache::OnReadCompleted(MemAddr addr, const MemData& data)
{
    // Send the completion on the bus
    if (!p_bus.Invoke())
    {
        DeadlockWrite("Unable to acquire the bus for sending read completion");
        return false;
    }
    
    for (std::vector<IMemoryCallback*>::const_iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (*p != NULL && !(*p)->OnMemoryReadCompleted(addr, data))
        {
            DeadlockWrite("Unable to send read completion to clients");
            return false;
        }
    }
    
    return true;
}

// Handles a write request from below
// FAILED  - stall
// DELAYED - repeat next cycle
// SUCCESS - advance
Result COMA::Cache::OnWriteRequest(const Request& req)
{
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Lines busy, cannot process bus write request");
        return FAILED;
    }
    
    // Note that writes may not be of entire cache-lines
    unsigned int offset = req.address % m_lineSize;
    MemAddr tag;
    
    Line* line = FindLine(req.address);
    if (line == NULL)
    {
        // Write miss; write-allocate
        line = AllocateLine(req.address, false, &tag);
        if (line == NULL)
        {
            ++m_numHardWConflicts;
            DeadlockWrite("Unable to allocate line for bus write request");
            return FAILED;
        }

        if (line->state != LINE_EMPTY)
        {
            // We're overwriting another line, evict the old line
            TraceWrite(req.address, "Processing Bus Write Request: Miss; Evicting line with tag %#016llx",
                       (unsigned long long)line->tag);

            if (!EvictLine(line, req))
            {
                ++m_numStallingWMisses;
                DeadlockWrite("Unable to evict line for bus write request");
                return FAILED;
            }

            COMMIT { ++m_numResolvedWConflicts; }
            return DELAYED;
        }

        TraceWrite(req.address, "Processing Bus Write Request: Miss; Sending Read Request");
        
        // Reset the line
        COMMIT
        {
            line->state    = LINE_LOADING;
            line->tag      = tag;
            line->tokens   = 0;
            line->dirty    = false;
            line->updating = 0;
            std::fill(line->valid, line->valid + MAX_MEMORY_OPERATION_SIZE, false);

            // Statistics
            ++m_numEmptyWMisses;
        }
        
        // Send a request out for the cache-line
        Message* msg = NULL;
        COMMIT
        {
            msg = new Message;
            msg->type      = Message::REQUEST;
            msg->address   = req.address - offset;
            msg->ignore    = false;
            msg->data.size = m_lineSize;
            msg->tokens    = 0;
            msg->sender    = m_id;
        }
            
        if (!SendMessage(msg, MINSPACE_INSERTION))
        {
            ++m_numStallingWMisses;
            DeadlockWrite("Unable to buffer read request for next node");
            return FAILED;
        }
        
        // Now try against next cycle
        return DELAYED;
    }

    // Write hit
    // Although we may hit a loading line
    if (line->state == LINE_FULL && line->tokens == m_parent.GetTotalTokens())
    {
        // We have all tokens, notify the sender client immediately
        TraceWrite(req.address, "Processing Bus Write Request: Exclusive Hit");
        
        if (!m_clients[req.client]->OnMemoryWriteCompleted(req.tid))
        {
            ++m_numStallingWHits;
            DeadlockWrite("Unable to process bus write completion for client %u", (unsigned)req.client);
            return FAILED;
        }

        // Statistics
        COMMIT{ ++m_numWHits; }
    }
    else
    {
        // There are other copies out there, send out an update message
        TraceWrite(req.address, "Processing Bus Write Request: Shared Hit; Sending Update");
        
        Message* msg = NULL;
        COMMIT
        {
            msg = new Message;
            msg->address   = req.address;
            msg->type      = Message::UPDATE;
            msg->sender    = m_id;
            msg->ignore    = false;
            msg->client    = req.client;
            msg->tid       = req.tid;
            msg->data.size = req.size;
            memcpy(msg->data.data, req.data, req.size);
                
            // Lock the line to prevent eviction
            line->updating++;

            // Statistics
            COMMIT{ ++m_numPartialWMisses; }
        }
            
        if (!SendMessage(msg, MINSPACE_INSERTION))
        {
            ++m_numStallingWMisses;
            DeadlockWrite("Unable to buffer update request for next node");
            return FAILED;
        }
    }
    
    // Either way, at this point we have a line, so we
    // write the data into it.
    COMMIT
    {
        memcpy(line->data + offset, req.data, req.size);
        
        // Mark the written area valid
        std::fill(line->valid + offset, line->valid + offset + req.size, true);
        
        // The line is now dirty
        line->dirty = true;
        
        // Also update last access time.
        line->access = GetKernel()->GetCycleNo();                    
    }
    return SUCCESS;
}

// Handles a read request from below
// FAILED  - stall
// DELAYED - repeat next cycle
// SUCCESS - advance
Result COMA::Cache::OnReadRequest(const Request& req)
{
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Lines busy, cannot process bus read request");
        return FAILED;
    }

    Line* line = FindLine(req.address);
    MemAddr tag;

    if (line == NULL)
    {
        // Read miss, allocate a line
        line = AllocateLine(req.address, false, &tag);
        if (line == NULL)
        {
            ++m_numHardRConflicts;
            DeadlockWrite("Unable to allocate line for bus read request");
            return FAILED;
        }

        if (line->state != LINE_EMPTY)
        {
            // We're overwriting another line, evict the old line
            TraceWrite(req.address, "Processing Bus Read Request: Miss; Evicting line with tag %#016llx",
                       (unsigned long long)line->tag);
            
            if (!EvictLine(line, req))
            {
                ++m_numStallingRMisses;
                DeadlockWrite("Unable to evict line for bus read request");
                return FAILED;
            }

            COMMIT { ++m_numResolvedRConflicts; }
            return DELAYED;
        }

        TraceWrite(req.address, "Processing Bus Read Request: Miss; Sending Read Request");

        // Reset the line
        COMMIT
        {
            line->state    = LINE_LOADING;
            line->tag      = tag;
            line->tokens   = 0;
            line->dirty    = false;
            line->updating = 0;
            line->access   = GetKernel()->GetCycleNo();
            std::fill(line->valid, line->valid + MAX_MEMORY_OPERATION_SIZE, false);

            ++m_numEmptyRMisses;
        }
        
        // Send a request out
        Message* msg = NULL;
        COMMIT
        {
            msg = new Message;
            msg->type      = Message::REQUEST;
            msg->address   = req.address;
            msg->ignore    = false;
            msg->data.size = req.size;
            msg->tokens    = 0;
            msg->sender    = m_id;
        }
            
        if (!SendMessage(msg, MINSPACE_INSERTION))
        {
            ++m_numStallingRMisses;
            DeadlockWrite("Unable to buffer read request for next node");
            return FAILED;
        }
    }
    // Read hit
    else if (line->state == LINE_FULL)
    {
        // Line is present and full
        TraceWrite(req.address, "Processing Bus Read Request: Full Hit");

        // Return the data
        MemData data;
        data.size = req.size;
        COMMIT
        {
            memcpy(data.data, line->data, data.size);

            // Update LRU information
            line->access = GetKernel()->GetCycleNo();
            
            ++m_numRHits;
        }

        if (!OnReadCompleted(req.address, data))
        {
            ++m_numStallingRHits;
            DeadlockWrite("Unable to notify clients of read completion");
            return FAILED;
        }
    }
    else
    {
        // The line is already being loaded.
        TraceWrite(req.address, "Processing Bus Read Request: Loading Hit");

        // We can ignore this request; the completion of the earlier load
        // will put the data on the bus so this requester will also get it.
        assert(line->state == LINE_LOADING);
        
        // Counts as a miss because we have to wait
        COMMIT{ m_numLoadingRMisses; }
    }
    return SUCCESS;
}

Result COMA::Cache::DoRequests()
{
    // Handle incoming requests from below
    assert(!m_requests.Empty());
    const Request& req = m_requests.Front();
    Result result = (req.write) ? OnWriteRequest(req) : OnReadRequest(req);
    if (result == SUCCESS)
    {
        m_requests.Pop();
    }
    return (result == FAILED) ? FAILED : SUCCESS;
}
    
Result COMA::Cache::DoReceive()
{
    // Handle received message from prev
    assert(!m_incoming.Empty());
    if (!OnMessageReceived(m_incoming.Front()))
    {
        return FAILED;
    }
    m_incoming.Pop();
    return SUCCESS;
}

COMA::Cache::Cache(const std::string& name, COMA& parent, Clock& clock, CacheID id, Config& config) :
    Simulator::Object(name, parent),
    Node(name, parent, clock, config),
    m_selector (parent.GetBankSelector()),
    m_lineSize (config.getValue<size_t>("CacheLineSize")),
    m_assoc    (config.getValue<size_t>(parent, "L2CacheAssociativity")),
    m_sets     (m_selector.GetNumBanks()),
    m_id       (id),
    p_lines    (*this, clock, "p_lines"),
    m_numEmptyRMisses(0),
    m_numEmptyWMisses(0),
    m_numHardRConflicts(0),
    m_numHardWConflicts(0),
    m_numInjectedEvictions(0),
    m_numLoadingRMisses(0),
    m_numMergedEvictions(0),
    m_numNetworkRHits(0),
    m_numNetworkWHits(0),
    m_numPartialWMisses(0),
    m_numRHits(0),
    m_numResolvedRConflicts(0),
    m_numResolvedWConflicts(0),
    m_numStallingRHits(0),
    m_numStallingRMisses(0),
    m_numStallingWHits(0),
    m_numStallingWMisses(0),
    m_numWHits(0),
    p_Requests (*this, "requests", delegate::create<Cache, &Cache::DoRequests>(*this)),
    p_In       (*this, "incoming", delegate::create<Cache, &Cache::DoReceive>(*this)),
    p_bus      (*this, clock, "p_bus"),
    m_requests ("b_requests", *this, clock, config.getValue<BufferSize>(*this, "RequestBufferSize")),
    m_responses("b_responses", *this, clock, config.getValue<BufferSize>(*this, "ResponseBufferSize"))
{
    RegisterSampleVariableInObject(m_numEmptyRMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numEmptyWMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numHardRConflicts, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numHardWConflicts, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numInjectedEvictions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numLoadingRMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numMergedEvictions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numNetworkRHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numNetworkWHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numPartialWMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numRHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numResolvedRConflicts, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numResolvedWConflicts, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingRHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingRMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingWHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingWMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numWHits, SVC_CUMULATIVE);

    // Create the cache lines
    m_lines.resize(m_assoc * m_sets);
    m_data.resize(m_lines.size() * m_lineSize);
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        Line& line = m_lines[i];
        line.state = LINE_EMPTY;
        line.data  = &m_data[i * m_lineSize];
    }

    m_requests.Sensitive(p_Requests);   
    m_incoming.Sensitive(p_In);
    
    p_lines.AddProcess(p_In);
    p_lines.AddProcess(p_Requests);

    p_bus.AddPriorityProcess(p_In);                   // Update triggers write completion
    p_bus.AddPriorityProcess(p_Requests);             // Read or write hit

    config.registerObject(*this, "cache");
    config.registerProperty(*this, "assoc", (uint32_t)m_assoc);
    config.registerProperty(*this, "sets", (uint32_t)m_sets);
    config.registerProperty(*this, "lsz", (uint32_t)m_lineSize);
    config.registerProperty(*this, "freq", (uint32_t)clock.GetFrequency());
}

void COMA::Cache::Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const
{
    out <<
    "The L2 Cache in a COMA system is connected to the processors with a bus and to\n"
    "the rest of the COMA system via a ring network.\n\n"
    "Supported operations:\n"
    "- inspect <component>\n"
    "  Print global information such as hit-rate\n"
    "  and cache configuration.\n"
    "- inspect <component> lines [fmt [width [address]]]\n"
    "  Read the cache lines themselves.\n"
    "  * fmt can be b/w/c and indicates formatting by bytes, words, or characters.\n"
    "  * width indicates how many bytes are printed on each line (default: entire line).\n"
    "  * address if specified filters the output to the specified cache line.\n" 
    "- inspect <component> buffers\n"
    "  Reads and displays the buffers in the cache\n";
}

void COMA::Cache::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
{
    if (!arguments.empty() && arguments[0] == "buffers")
    {
        // Read the buffers
        out << "Bus requests:" << endl << endl
            << "      Address      | Size | Type  |" << endl
            << "-------------------+------+-------+" << endl;
        for (Buffer<Request>::const_iterator p = m_requests.begin(); p != m_requests.end(); ++p)
        {
            out << hex << "0x" << setw(16) << setfill('0') << p->address << " | "
                << dec << setw(4) << right << setfill(' ') << p->size << " | "
                << (p->write ? "Write" : "Read ") << " | "
                << endl;
        }
        
        out << endl << "Ring interface:" << endl << endl;
        Print(out);        
        return;
    }
    else if (arguments.empty())
    {
        out << "Cache type:       ";
        if (m_assoc == 1) {
            out << "Direct mapped" << endl;
        } else if (m_assoc == m_lines.size()) {
            out << "Fully associative" << endl;
        } else {
            out << dec << m_assoc << "-way set associative" << endl;
        }
        
        out << "L2 bank mapping:  " << m_selector.GetName() << endl
            << "Cache size:       " << dec << (m_lineSize * m_lines.size()) << " bytes" << endl
            << "Cache line size:  " << dec << m_lineSize << " bytes" << endl
            << endl;

        uint64_t numRMisses = m_numEmptyRMisses + m_numLoadingRMisses + m_numHardRConflicts + m_numResolvedRConflicts;
        uint64_t numRAccesses = m_numRHits + numRMisses;
        if (numRAccesses == 0)
            out << "No read accesses so far, cannot compute read hit/miss/conflict rates." << endl;
        else
        {
            float factor = 100.0f / numRAccesses;

            out << "Number of reads from downstream: " << numRAccesses << endl
                << "Read hits:   " << dec << m_numRHits << " (" << setprecision(2) << fixed << m_numRHits * factor << "%)" << endl
                << "Read misses: " << dec << numRMisses << " (" << setprecision(2) << fixed << numRMisses * factor << "%)" << endl
                << "Breakdown of read misses:" << endl
                << "  (true misses)" << endl
                << "- to an empty line (async):                    " 
                << dec << m_numEmptyRMisses << " (" << setprecision(2) << fixed << m_numEmptyRMisses * factor << "%)" << endl
                << "- to a loading line with same tag (async):     " 
                << dec << m_numLoadingRMisses << " (" << setprecision(2) << fixed << m_numLoadingRMisses * factor << "%)" << endl
                << "  (conflicts)" << endl
                << "- to a non-empty, reusable line with different tag (async):        " 
                << dec << m_numResolvedRConflicts << " (" << setprecision(2) << fixed << m_numResolvedRConflicts * factor << "%)" << endl
                << "- to a non-empty, non-reusable line with different tag (stalling): " 
                << dec << m_numHardRConflicts << " (" << setprecision(2) << fixed << m_numHardRConflicts * factor << "%)" << endl
                << endl
                << "Read hit stalls by upstream/snoop:  " << dec << m_numStallingRHits << " cycles" << endl
                << "Read miss stalls by upstream/snoop: " << dec << m_numStallingRMisses << " cycles" << endl
                << endl;
        }

        out << "Number of read hits from other caches:  " << dec << m_numNetworkRHits << endl
            << endl;

        uint64_t numWMisses = m_numEmptyWMisses + m_numPartialWMisses + m_numHardWConflicts + m_numResolvedWConflicts;
        uint64_t numWAccesses = m_numWHits + numWMisses;
        if (numWAccesses == 0)
            out << "No write accesses so far, cannot compute read hit/miss/conflict rates." << endl;
        else
        {
            float factor = 100.0f / numWAccesses;

            out << "Number of writes from downstream: " << numWAccesses << endl
                << "Write hits:   " << dec << m_numWHits << " (" << setprecision(2) << fixed << m_numWHits * factor << "%)" << endl
                << "Write misses: " << dec << numWMisses << " (" << setprecision(2) << fixed << numWMisses * factor << "%)" << endl
                << "Breakdown of write misses:" << endl
                << "  (true misses)" << endl
                << "- to an empty line (async):                               " 
                << dec << m_numEmptyWMisses << " (" << setprecision(2) << fixed << m_numEmptyWMisses * factor << "%)" << endl
                << "- to a non-empty line, loading or missing tokens (async): "
                << dec << m_numPartialWMisses << " (" << setprecision(2) << fixed << m_numPartialWMisses * factor << "%)" << endl
                << "  (conflicts)" << endl
                << "- to a non-empty, reusable line with different tag (async):        " 
                << dec << m_numResolvedWConflicts << " (" << setprecision(2) << fixed << m_numResolvedWConflicts * factor << "%)" << endl
                << "- to a non-empty, non-reusable line with different tag (stalling): " 
                << dec << m_numHardWConflicts << " (" << setprecision(2) << fixed << m_numHardWConflicts * factor << "%)" << endl
                << endl
                << "Write hit stalls by upstream/snoop:  " << dec << m_numStallingWHits << " cycles" << endl
                << "Write miss stalls by upstream/snoop: " << dec << m_numStallingWMisses << " cycles" << endl
                << endl;
        }

        out << "Number of injections to empty lines:    " << dec << m_numInjectedEvictions << endl
            << "Number of merged injections:            " << dec << m_numMergedEvictions << endl
            << "Number of write hits from other caches: " << dec << m_numNetworkWHits << endl
            << endl;


    }
    else if (arguments[0] == "lines")
    {
        enum { fmt_bytes, fmt_words, fmt_chars } fmt = fmt_words;
        size_t bytes_per_line = m_lineSize;
        bool specific = false;
        MemAddr seladdr = 0;
        if (arguments.size() > 1)
        {
            if (arguments[1] == "b") fmt = fmt_bytes;
            else if (arguments[1] == "w") fmt = fmt_words;
            else if (arguments[1] == "c") fmt = fmt_chars;
            else
            {
                out << "Invalid format: " << arguments[1] << ", expected b/w/c" << endl;
                return;
            }
        }
        if (arguments.size() > 2)
        {
            bytes_per_line = strtoumax(arguments[2].c_str(), 0, 0);
        }
        if (arguments.size() > 3)
        {
            errno = 0;
            seladdr = strtoumax(arguments[3].c_str(), 0, 0); 
            if (errno != EINVAL)
                specific = true;
            seladdr = (seladdr / m_lineSize) * m_lineSize;
        }
        
        out << "Set |       Address      | LDU   | Tokens |                       Data" << endl;
        out << "----+--------------------+-------+--------+--------------------------------------------------" << endl;
        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            const size_t set = i / m_assoc;
            const Line& line = m_lines[i];
            MemAddr lineaddr = m_selector.Unmap(line.tag, set) * m_lineSize;
            if (specific && lineaddr != seladdr)
                continue;
            
            out << setw(3) << setfill(' ') << dec << right << set;
            
            if (line.state == LINE_EMPTY) {
                out << " |                    |       |        |";
            } else {
                out << " | "
                    << hex << "0x" << setw(16) << setfill('0') << lineaddr
                    << " | "
                    << ((line.state == LINE_LOADING) ? "L" : " ")
                    << (line.dirty ? "D" : " ")
                    << setw(3) << setfill(' ') << dec << line.updating
                    << " | "
                    << setfill(' ') << setw(6) << line.tokens << " |";
                
                // Print the data
                out << hex << setfill('0');
                static const int BYTES_PER_LINE = 64;
                for (size_t y = 0; y < m_lineSize; y += bytes_per_line)
                {
                    for (size_t x = y; x < y + bytes_per_line; ++x) {
                        if ((fmt == fmt_bytes) || ((fmt == fmt_words) && (x % sizeof(Integer) == 0))) 
                            out << " ";
                        
                        if (line.valid[x]) {
                            char byte = line.data[x];
                            if (fmt == fmt_chars)
                                out << (isprint(byte) ? byte : '.');
                            else
                                out << setw(2) << (unsigned)(unsigned char)byte;
                        } else {
                            out << ((fmt == fmt_chars) ? " " : "  ");
                        }
                    }
                    
                    if (y + bytes_per_line < m_lineSize) {
                        // This was not yet the last line
                        out << endl << "    |                    |       |        |";
                    }
                }
            }
            out << endl;
        }
    }
}
    
}
