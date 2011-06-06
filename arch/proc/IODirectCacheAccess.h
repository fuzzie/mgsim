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
#ifndef IO_DCA_H
#define IO_DCA_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class IODirectCacheAccess : public Object, public IMemoryCallback
{
public:

    enum RequestType
    {
        READ = 0,
        WRITE = 1,
        FLUSH = 2
    };

    struct Request 
    {
        IODeviceID  client;
        MemAddr     address;
        RequestType type;
        MemData     data;
    };

private:

    struct Response
    {
        MemAddr address;
        MemData data;
    };

    Processor&           m_cpu;
    IMemory&             m_memory;
    MCID                 m_mcid;
    IOBusInterface&      m_busif;
    const MemSize        m_lineSize; 

public:
    Buffer<Request>      m_requests; // from bus

private:
    Buffer<Response>     m_responses; // from memory

    bool                 m_has_outstanding_request;
    IODeviceID           m_outstanding_client;
    MemAddr              m_outstanding_address;
    MemSize              m_outstanding_size;

    bool                 m_flushing;
    size_t               m_pending_writes;

public:
    IODirectCacheAccess(const std::string& name, Object& parent, Clock& clock, Processor& proc, IMemory& memory, IOBusInterface& busif, Config& config);
    ~IODirectCacheAccess();

    bool QueueRequest(const Request& req);
    
    Process p_MemoryOutgoing;
    Process p_BusOutgoing;

    ArbitratedService<> p_service;

    Result DoMemoryOutgoing();
    Result DoBusOutgoing();

    bool OnMemoryReadCompleted(MemAddr addr, const MemData& data) ;
    bool OnMemoryWriteCompleted(TID tid);
    bool OnMemorySnooped(MemAddr addr, const MemData& data) { return true; }
    bool OnMemoryInvalidated(MemAddr addr) { return true; }

};

#endif
