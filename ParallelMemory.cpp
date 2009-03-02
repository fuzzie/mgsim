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
#include "ParallelMemory.h"
#include <cassert>
#include <iostream>
using namespace Simulator;
using namespace std;

void ParallelMemory::Request::release()
{
    if (refcount != NULL && --*refcount == 0) {
        delete[] (char*)data.data;
        delete refcount;
    }
}

ParallelMemory::Request& ParallelMemory::Request::operator =(const Request& req)
{
    release();
    refcount  = req.refcount;
    write     = req.write;
    address   = req.address;
    data      = req.data;
    callback  = req.callback;
    ++*refcount;
    return *this;
}

ParallelMemory::Request::Request(const Request& req) : refcount(NULL) { *this = req; }
ParallelMemory::Request::Request() { refcount = new unsigned long(1); data.data = NULL; }
ParallelMemory::Request::~Request() { release(); }

                                    
void ParallelMemory::RegisterListener(IMemoryCallback& callback)
{
    m_caches.insert(&callback);
}

void ParallelMemory::UnregisterListener(IMemoryCallback& callback)
{
    m_caches.erase(&callback);
}

void ParallelMemory::AddRequest(Request& request)
{
	Port* port;
	map<IMemoryCallback*, Port*>::iterator p = m_portmap.find(request.callback);
	if (p == m_portmap.end())
	{
		// Port mapping doesn't exist yet, create it
		size_t index = m_portmap.size();
		assert(index < m_ports.size());
		port = &m_ports[index];

		m_portmap.insert(make_pair(request.callback, port));
	}
	else
	{
		port = p->second;
	}

	port->m_requests.push_back(request);
	m_numRequests++;
	m_statMaxRequests = max(m_statMaxRequests, m_numRequests);
}

Result ParallelMemory::Read(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag)
{
#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }
#endif

    if (m_config.bufferSize == INFINITE || m_numRequests < m_config.bufferSize)
    {
        COMMIT
        {
            Request request;
            request.callback  = &callback;
            request.address   = address;
            request.data.data = new char[ (size_t)size ];
            request.data.size = size;
            request.data.tag  = tag;
            request.write     = false;
            AddRequest(request);
        }
        return DELAYED;
    }
    return FAILED;
}

Result ParallelMemory::Write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag)
{
#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }
#endif

    if (m_config.bufferSize == INFINITE || m_numRequests < m_config.bufferSize)
    {
        Request request;
        request.callback  = &callback;
        request.address   = address;
        request.data.data = new char[ (size_t)size ];
        request.data.size = size;
        request.data.tag  = tag;
        request.write     = true;
        memcpy(request.data.data, data, size);

        // Broadcast the snoop data
        for (set<IMemoryCallback*>::iterator p = m_caches.begin(); p != m_caches.end(); p++)
        {
            if (!(*p)->OnMemorySnooped(request.address, request.data))
            {
                return FAILED;
            }
        }

        COMMIT{ AddRequest(request); }
        return DELAYED;
    }
    return FAILED;
}

bool ParallelMemory::CheckPermissions(MemAddr address, MemSize size, int access) const
{
	return VirtualMemory::CheckPermissions(address, size, access);
}

Result ParallelMemory::OnCycleWritePhase(unsigned int stateIndex)
{
	CycleNo now         = GetKernel()->GetCycleNo();
	Port&   port        = m_ports[stateIndex];
	size_t  nAvailable  = m_config.width - port.m_inFlight.size();
	Result  result      = DELAYED;

	// Check if the first pending request has completed
	if (!port.m_inFlight.empty())
	{
		multimap<CycleNo, Request>::iterator p = port.m_inFlight.begin();
		if (now >= p->first)
		{
			// Yes, do the callback
			const Request& request = p->second;
			
			if (request.write)
			{
				VirtualMemory::Write(request.address, request.data.data, request.data.size);
    			if (!request.callback->OnMemoryWriteCompleted(request.data.tag))
	    		{
		    		return FAILED;
			    }
			}
			else
			{
				VirtualMemory::Read(request.address, request.data.data, request.data.size);
			    if (!request.callback->OnMemoryReadCompleted(request.data))
    			{
    				return FAILED;
    			}
			}
			
			COMMIT
			{
				port.m_inFlight.erase(p);
				m_numRequests--;
			}

			nAvailable++;
		}

		// A request is still active
		result = SUCCESS;
	}

	size_t nDispatched = 0;
	for (deque<Request>::const_iterator p = port.m_requests.begin(); p != port.m_requests.end() && nDispatched < nAvailable; p++, nDispatched++)
	{
		// A new request can be handled
  		COMMIT
   		{
   			// Time the request
    		CycleNo delay = m_config.baseRequestTime + m_config.timePerLine * (p->data.size + m_config.sizeOfLine - 1) / m_config.sizeOfLine;
	    	port.m_inFlight.insert(make_pair(now + delay, *p));
    	}
	}

	COMMIT
	{
		// Remove the dispatched requests
		for (size_t i = 0; i < nDispatched; i++)
		{
			port.m_requests.pop_front();
		}

		// Update stats
		m_statMaxInFlight = max(m_statMaxInFlight, port.m_inFlight.size());
	}
	return (nDispatched > 0) ? SUCCESS : result;
}

void ParallelMemory::Read (MemAddr address, void* data, MemSize size)
{
	return VirtualMemory::Read(address, data, size);
}

void ParallelMemory::Write(MemAddr address, const void* data, MemSize size, int perm)
{
	return VirtualMemory::Write(address, data, size, perm);
}

ParallelMemory::ParallelMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config, PSize numProcs ) :
    IComponent(parent, kernel, name, (int)numProcs),
    m_ports(numProcs),
    m_config(config),
    m_numRequests(0),
    m_statMaxRequests(0),
    m_statMaxInFlight(0)
{
}

ParallelMemory::~ParallelMemory()
{
}
