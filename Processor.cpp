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
#include <cassert>
#include "Processor.h"
using namespace Simulator;
using namespace std;

//
// Processor implementation
//
Processor::Processor(Object* parent, Kernel& kernel, PID pid, PSize numProcs, const std::string& name, IMemory& memory, const Config& config, MemAddr runAddress, bool legacy)
:   IComponent(parent, kernel, name, 0),
    m_pid(pid), m_kernel(kernel), m_memory(memory), m_numProcs(numProcs),
    m_localFamilyCompletion(0),
	m_allocator   (*this, "alloc",    m_familyTable, m_threadTable, m_registerFile, m_raunit, m_icache, m_network, m_pipeline, numProcs, config.allocator),
    m_icache      (*this, "icache",   m_allocator, config.icache),
    m_dcache      (*this, "dcache",   m_allocator, m_familyTable, m_registerFile, config.dcache),
	m_registerFile(*this,             m_icache, m_dcache, m_allocator, config.registerFile),
    m_pipeline    (*this, "pipeline", m_registerFile, m_network, m_allocator, m_familyTable, m_threadTable, m_icache, m_dcache, m_fpu, config.pipeline),
	m_fpu         (*this, "fpu",      m_registerFile, config.fpu),
    m_raunit      (*this, "rau",      m_registerFile, config.raunit),
	m_familyTable (*this,             config.familyTable),
	m_threadTable (*this,             config.threadTable),
    m_network     (*this, "network",  m_allocator, m_registerFile, m_familyTable)
{
    //
    // Set port priorities and connections on all components
    //
	m_registerFile.p_asyncW.setPriority(m_fpu,       0);
    m_registerFile.p_asyncW.setPriority(m_dcache,    1);
    m_registerFile.p_asyncW.setPriority(m_network,   2);
    m_registerFile.p_asyncW.setPriority(m_allocator, 3);
    m_registerFile.p_asyncR.setPriority(m_network,   0);
    m_registerFile.p_pipelineR1.setComponent(m_pipeline.m_read);
    m_registerFile.p_pipelineR2.setComponent(m_pipeline.m_read);
    m_registerFile.p_pipelineW.setComponent(m_pipeline.m_writeback);
    
    // Set thread activation (cache-line query) order.
    // Primary concern is keeping the pipeline flowing. So order from back to front.
    m_icache.p_request.setPriority(m_pipeline.m_writeback, 0);  // Writebacks
    m_icache.p_request.setPriority(m_pipeline.m_execute,   1);  // Reschedules
    m_icache.p_request.setPriority(m_fpu,                  2);  // FPU writeback
    m_icache.p_request.setPriority(m_dcache,               3);  // Memory read writebacks
    m_icache.p_request.setPriority(m_network,              4);  // Remote shareds writebacks
    m_icache.p_request.setPriority(m_allocator,            5);  // Family completion, etc

    m_allocator.p_cleanup.setPriority(m_pipeline.m_read, 0);

    m_memory.registerListener(*this);

    if (pid == 0)
    {
        // Allocate the startup family on the first processor
        m_allocator.allocateInitialFamily(runAddress,legacy);
    }
}

void Processor::initialize(Processor& prev, Processor& next)
{
    m_network.initialize(prev.m_network, next.m_network);
}

Result Processor::readMemory(MemAddr address, void* data, MemSize size, MemTag tag)
{
	return m_memory.read(*this, address, data, size, tag);
}

Result Processor::writeMemory(MemAddr address, void* data, MemSize size, MemTag tag)
{
	return m_memory.write(*this, address, data, size, tag);
}

bool Processor::checkPermissions(MemAddr address, MemSize size, int access) const
{
	return m_memory.checkPermissions(address, size, access);
}

bool Processor::onMemoryReadCompleted(const MemData& data)
{
	// Dispatch result to I-Cache, D-Cache or Create Process, depending on tag
	assert(data.tag.cid != INVALID_CID);
	if (data.tag.data)
	{
		return m_dcache.onMemoryReadCompleted(data);
	}
	return m_icache.onMemoryReadCompleted(data);
}

bool Processor::onMemoryWriteCompleted(const MemTag& tag)
{
	// Dispatch result to D-Cache
	assert(tag.fid != INVALID_LFID);
	return m_dcache.onMemoryWriteCompleted(tag);
}

bool Processor::onMemorySnooped(MemAddr addr, const MemData& data)
{
	return m_dcache.onMemorySnooped(addr, data);
}

void Processor::OnFamilyTerminatedLocally(MemAddr pc)
{
    if (pc == 0x88)
    {
        CycleNo cycle = getKernel().getCycleNo();
        m_localFamilyCompletion = cycle;
    }
}
