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
#include "SerialMemory.h"
#include "config.h"
#include <cassert>
#include <cstring>
#include <iomanip>
using namespace std;

namespace Simulator
{

void SerialMemory::RegisterClient(PSize pid, IMemoryCallback& callback, const Process* processes[])
{
    assert(m_clients[pid] == NULL);
    m_clients[pid] = &callback;
    
    for (size_t i = 0; processes[i] != NULL; ++i)
    {
        p_requests.AddProcess(*processes[i]);
    }
}

void SerialMemory::UnregisterClient(PSize pid)
{
    assert(m_clients[pid] != NULL);
    m_clients[pid] = NULL;
}

bool SerialMemory::Read(PSize pid, MemAddr address, MemSize size)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }
    
    if (!p_requests.Invoke())
    {
        return false;
    }

    Request request;
    request.callback  = m_clients[pid];
    request.address   = address;
    request.data.size = size;
    request.write     = false;

    if (!m_requests.Push(request))
    {
        return false;
    }
    
    return true;
}

bool SerialMemory::Write(PSize pid, MemAddr address, const void* data, MemSize size, TID tid)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    if (!p_requests.Invoke())
    {
        return false;
    }

    Request request;
    request.callback  = m_clients[pid];
    request.address   = address;
    request.data.size = size;
    request.tid       = tid;
    request.write     = true;
    memcpy(request.data.data, data, (size_t)size);

    if (!m_requests.Push(request))
    {
        return false;
    }

    // Broadcast the snoop data
    for (vector<IMemoryCallback*>::const_iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (*p != NULL && !(*p)->OnMemorySnooped(request.address, request.data))
        {
            return false;
        }
    }

    return true;
}

void SerialMemory::Reserve(MemAddr address, MemSize size, int perm)
{
    return VirtualMemory::Reserve(address, size, perm);
}

void SerialMemory::Unreserve(MemAddr address)
{
    return VirtualMemory::Unreserve(address);
}

bool SerialMemory::Allocate(MemSize size, int perm, MemAddr& address)
{
    return VirtualMemory::Allocate(size, perm, address);
}

void SerialMemory::Read(MemAddr address, void* data, MemSize size)
{
    return VirtualMemory::Read(address, data, size);
}

void SerialMemory::Write(MemAddr address, const void* data, MemSize size)
{
    return VirtualMemory::Write(address, data, size);
}

bool SerialMemory::CheckPermissions(MemAddr address, MemSize size, int access) const
{
    return VirtualMemory::CheckPermissions(address, size, access);
}

Result SerialMemory::DoRequests()
{
    assert(!m_requests.Empty());

    const Request& request = m_requests.Front();
    const CycleNo  now     = GetCycleNo();
        
    if (m_nextdone > 0)
    {
        // There is already a request active
        if (now >= m_nextdone)
        {
            // The current request has completed
            if (request.write) {
                VirtualMemory::Write(request.address, request.data.data, request.data.size);
                if (!request.callback->OnMemoryWriteCompleted(request.tid))
                {
                    return FAILED;
                }
                COMMIT {
                    ++m_nwrites;
                    m_nwrite_bytes += request.data.size;
                }
            } else {
                MemData data(request.data);
                VirtualMemory::Read(request.address, data.data, data.size);
                if (!request.callback->OnMemoryReadCompleted(request.address, data))
                {
                    return FAILED;
                }
                COMMIT {
                    ++m_nreads;
                    m_nread_bytes += request.data.size;
                }
            }
            m_requests.Pop();
            COMMIT{ m_nextdone = 0; }
        }
    }
    else
    {
        // A new request is ready to be handled
        COMMIT
        {
            // Time the request
            CycleNo requestTime = m_baseRequestTime + m_timePerLine * (request.data.size + m_sizeOfLine - 1) / m_sizeOfLine;
            m_nextdone = now + requestTime;
        }
    }
    return SUCCESS;
}

SerialMemory::SerialMemory(const std::string& name, Object& parent, Clock& clock, const Config& config) :
    Object(name, parent, clock),
    m_clock          (clock),
    m_requests       ("b_requests", *this, clock, config.getInteger<BufferSize>("MemoryBufferSize", INFINITE)),
    p_requests       (*this, clock, "m_requests"),
    m_baseRequestTime(config.getInteger<CycleNo>   ("MemoryBaseRequestTime", 1)),
    m_timePerLine    (config.getInteger<CycleNo>   ("MemoryTimePerLine", 1)),
    m_sizeOfLine     (config.getInteger<CycleNo>   ("MemorySizeOfLine", 8)),
    m_nextdone(0),
    m_nreads(0),
    m_nread_bytes(0),
    m_nwrites(0),
    m_nwrite_bytes(0),
    
    p_Requests("requests", delegate::create<SerialMemory, &SerialMemory::DoRequests>(*this) )
{
    m_requests.Sensitive( p_Requests );

    // Get number of processors
    const vector<PSize> places = config.getIntegerList<PSize>("NumProcessors");
    PSize numProcs = 0;
    for (size_t i = 0; i < places.size(); ++i) {
        numProcs += places[i];
    }
    m_clients.resize(numProcs, NULL);
}

void SerialMemory::Cmd_Help(ostream& out, const vector<string>& /*arguments*/) const
{
    out <<
    "The Serial Memory is a simplified memory implementation that has no contention\n"
    "on accesses and simply queues all requests in a single queue.\n\n"
    "Supported operations:\n"
    "- info <component>\n"
    "  Displays the currently reserved and allocated memory ranges\n\n"
    "- read <component> <start> <size>\n"
    "  Reads the specified number of bytes of raw data from memory from the\n"
    "  specified address\n\n"
    "- read <component> requests\n"
    "  Reads the requests buffer\n";
}

void SerialMemory::Cmd_Read(ostream& out, const vector<string>& arguments) const
{
    if (arguments.empty() || arguments[0] != "requests")
    {
        return VirtualMemory::Cmd_Read(out, arguments);
    }
    
    out << "      Address       | Size | Type  | Source" << endl;
    out << "--------------------+------+-------+---------------------" << endl;

    for (Buffer<Request>::const_iterator p = m_requests.begin(); p != m_requests.end(); ++p)
    {
        out << hex << setfill('0') << right 
            << " 0x" << setw(16) << p->address << " | "
            << setfill(' ') << setw(4) << dec << p->data.size << " | ";

        if (p->write) {
            out << "Write";
        } else {
            out << "Read ";
        }
        out << " | ";

        Object* obj = dynamic_cast<Object*>(p->callback);
        if (obj == NULL) {
            out << "???";
        } else {
            out << obj->GetFQN();
        }

        out << endl;
    }

    out << endl << "First request done at: ";
    if (m_nextdone == 0) {
        out << "N/A";
    } else {
        out << dec << m_nextdone;
    }
    out << endl << endl;
}

}
