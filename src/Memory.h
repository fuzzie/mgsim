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
#ifndef MEMORY_H
#define MEMORY_H

#include "simtypes.h"
#include "ports.h"

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
};

class IMemory
{
public:
	static const int PERM_EXECUTE = 1;
	static const int PERM_WRITE   = 2;
	static const int PERM_READ    = 4;

    virtual void Reserve(MemAddr address, MemSize size, int perm) = 0;
    virtual void Unreserve(MemAddr address) = 0;
    virtual void RegisterClient  (PSize pid, IMemoryCallback& callback, const Process* processes[]) = 0;
    virtual void UnregisterClient(PSize pid) = 0;
    virtual bool Read (PSize pid, MemAddr address, MemSize size) = 0;
    virtual bool Write(PSize pid, MemAddr address, const void* data, MemSize size, TID tid) = 0;
	virtual bool CheckPermissions(MemAddr address, MemSize size, int access) const = 0;

    virtual ~IMemory() {}

    virtual void GetMemoryStatistics(uint64_t& nreads, uint64_t& nwrites, 
                                     uint64_t& nread_bytes, uint64_t& nwrite_bytes) const = 0;
};

class IMemoryAdmin : public IMemory
{
public:
    virtual bool Allocate(MemSize size, int perm, MemAddr& address) = 0;
    virtual void Read (MemAddr address, void* data, MemSize size) = 0;
    virtual void Write(MemAddr address, const void* data, MemSize size) = 0;

    virtual ~IMemoryAdmin() {}
};

}
#endif

