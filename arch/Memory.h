/*
mgsim: Microgrid Simulator
Copyright (C) 2006,2007,2008,2009,2010,2011,2012  The Microgrid Project.

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
#ifndef MEMORY_H
#define MEMORY_H

#include "simtypes.h"
#include "sim/ports.h"
#include "sim/storage.h"

namespace Simulator
{

// Maximum memory operation (read/write) size, in bytes. This is used to
// allocate fixed-size arrays in request buffers.
static const size_t MAX_MEMORY_OPERATION_SIZE = 64;

struct MemData
{
    char    data[MAX_MEMORY_OPERATION_SIZE];
    MemSize size;
};

class IMemory;

class IMemoryCallback
{
public:
    virtual bool OnMemoryReadCompleted(MemAddr addr, const MemData& data) = 0;
    virtual bool OnMemoryWriteCompleted(TID tid) = 0;
    virtual bool OnMemoryInvalidated(MemAddr addr) = 0;
    virtual bool OnMemorySnooped(MemAddr /* addr */, const MemData& /* data */) { return true; }

    virtual ~IMemoryCallback() {}

    // For topology detection.
    virtual Object& GetMemoryPeer() = 0;
};

typedef size_t MCID;    ///< Memory Client ID

class IMemory
{
public:
    enum Permissions {
        PERM_EXECUTE   = 1,
	PERM_WRITE     = 2,
	PERM_READ      = 4,
        PERM_DCA_READ  = 8,
        PERM_DCA_WRITE = 16
    };

    virtual MCID RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool grouped = false) = 0;
    virtual void UnregisterClient(MCID id) = 0;
    virtual bool Read (MCID id, MemAddr address, MemSize size) = 0;
    virtual bool Write(MCID id, MemAddr address, const void* data, MemSize size, TID tid) = 0;
    
    virtual void Initialize() {}

    virtual ~IMemory() {}

    virtual void GetMemoryStatistics(uint64_t& nreads, uint64_t& nwrites, 
                                     uint64_t& nread_bytes, uint64_t& nwrite_bytes,
                                     uint64_t& nreads_ext, uint64_t& nwrites_ext) const = 0;
};

class IMemoryAdmin : public IMemory
{
public:
    virtual void Reserve(MemAddr address, MemSize size, int perm) = 0;
    virtual void Unreserve(MemAddr address) = 0;
    virtual bool Allocate(MemSize size, int perm, MemAddr& address) = 0;
    virtual bool CheckPermissions(MemAddr address, MemSize size, int access) const = 0;

    virtual void Read (MemAddr address, void* data, MemSize size) = 0;
    virtual void Write(MemAddr address, const void* data, MemSize size) = 0;

    virtual ~IMemoryAdmin() {}
};

}
#endif

