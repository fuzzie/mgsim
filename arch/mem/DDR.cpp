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
#include "DDR.h"
#include "VirtualMemory.h"
#include "sim/config.h"
#include "sim/log2.h"
#include <sstream>
#include <limits>
#include <cstdio>

namespace Simulator
{

template <typename T>
static T GET_BITS(const T& value, unsigned int offset, unsigned int size)
{
    return (value >> offset) & ((T(1) << size) - 1);
}

static const unsigned long INVALID_ROW = std::numeric_limits<unsigned long>::max();

bool DDRChannel::Read(MemAddr address, MemSize size)
{
    if (m_busy.IsSet())
    {
        // We're still busy
        return false;
    }
    
    // Accept request
    COMMIT
    {
        m_request.address   = address;
        m_request.offset    = 0;
        m_request.data.size = size;
        m_request.write     = false;     
        m_next_command = 0;
    }

    // Get the actual data
    m_memory.Read(address, m_request.data.data, size);
    
    if (!m_busy.Set())
    {
        return false;
    }
    return true;
}

bool DDRChannel::Write(MemAddr address, const void* data, MemSize size)
{
    if (m_busy.IsSet())
    {
        // We're still busy
        return false;
    }
    
    // Accept request
    COMMIT
    {
        m_request.address   = address;
        m_request.offset    = 0;
        m_request.data.size = size;
        m_request.write     = true;
        
        m_next_command = 0;
    }

    // Write the actual data
    m_memory.Write(address, data, size);
    
    if (!m_busy.Set())
    {
        return false;
    }
    return true;
}

// Main process for timing the current active request
Result DDRChannel::DoRequest()
{
    assert(m_busy.IsSet());
    
    const CycleNo now = GetClock().GetCycleNo();
    if (now < m_next_command)
    {
        // Can't continue yet
        return SUCCESS;
    }
    
    // We read from m_nDevicesPerRank devices, each providing m_nBurstLength bytes in the burst.
    const unsigned burst_size = m_ddrconfig.m_nBurstSize;
    
    // Decode the burst address and offset-within-burst
    const MemAddr      address = (m_request.address + m_request.offset) / burst_size;
    const unsigned int offset  = (m_request.address + m_request.offset) % burst_size;
    const unsigned int rank    = GET_BITS(address, m_ddrconfig.m_nRankStart, m_ddrconfig.m_nRankBits),
                       row     = GET_BITS(address, m_ddrconfig.m_nRowStart,  m_ddrconfig.m_nRowBits);

    if (m_currentRow[rank] != row)
    {
        if (m_currentRow[rank] != INVALID_ROW)
        {
            // Precharge (close) the currently active row
            COMMIT
            {
                m_next_command = std::max(m_next_precharge, now) + m_ddrconfig.m_tRP;
                m_currentRow[rank] = INVALID_ROW;
            }
            return SUCCESS;
        }
        
        // Activate (open) the desired row
        COMMIT
        {
            m_next_command   = now + m_ddrconfig.m_tRCD;
            m_next_precharge = now + m_ddrconfig.m_tRAS;
            
            m_currentRow[rank] = row;
        }
        return SUCCESS;
    }
    
    // Process a single burst
    unsigned int remainder = m_request.data.size - m_request.offset;
    unsigned int size      = std::min(burst_size - offset, remainder);

    if (m_request.write)
    {
        COMMIT
        {
            // Update address to reflect written portion
            m_request.offset += size;

            m_next_command   = now + m_ddrconfig.m_tCWL;
            m_next_precharge = now + m_ddrconfig.m_tWR;
        }

        if (size < remainder)
        {
            // We're not done yet
            return SUCCESS;
        }
    }
    else
    {
        COMMIT
        {
            // Update address to reflect read portion
            m_request.offset += size;
            m_request.done    = now + m_ddrconfig.m_tCL;
            
            // Schedule next read
            m_next_command = now + m_ddrconfig.m_tCCD;
        }
    
        if (size < remainder)
        {
            // We're not done yet
            return SUCCESS;
        }
        
        // We're done with this read; queue it into the pipeline
        if (!m_pipeline.Push(m_request))
        {
            // The read pipeline should be big enough
            assert(false);
            return FAILED;
        }
    }

    // We've completed this request
    if (!m_busy.Clear())
    {
        return FAILED;
    }
    return SUCCESS;
}

Result DDRChannel::DoPipeline()
{
    assert(!m_pipeline.Empty());
    const CycleNo  now     = GetClock().GetCycleNo();
    const Request& request = m_pipeline.Front();
    if (now >= request.done)
    {
        // The last burst has completed, send the assembled data back
        assert(!request.write);
        if (!m_callback->OnReadCompleted(request.address, request.data))
        {
            return FAILED;
        }
        m_pipeline.Pop();
    }
    return SUCCESS;
}

DDRChannel::DDRConfig::DDRConfig(const std::string& name, Object& parent, Clock& clock, Config& config)
    : Object(name, parent, clock)
{
    // DDR 3
    m_nBurstLength = config.getValue<size_t> (*this, "BurstLength");
    if (m_nBurstLength != 8)
        throw SimulationException(*this, "This implementation only supports m_nBurstLength = 8");
    size_t cellsize = config.getValue<size_t> (*this, "CellSize");
    if (cellsize != 8)
        throw SimulationException(*this, "This implementation only supports CellSize = 8");

    m_tCL  = config.getValue<unsigned> (*this, "tCL");
    m_tRCD = config.getValue<unsigned> (*this, "tRCD");
    m_tRP  = config.getValue<unsigned> (*this, "tRP");
    m_tRAS = config.getValue<unsigned> (*this, "tRAS");
    m_tCWL = config.getValue<unsigned> (*this, "tCWL");
    m_tCCD = config.getValue<unsigned> (*this, "tCCD");
    
    // tWR is expressed in DDR specs in nanoseconds, see 
    // http://www.samsung.com/global/business/semiconductor/products/dram/downloads/applicationnote/tWR.pdf
    // Frequency is in MHz.
    m_tWR = config.getValue<unsigned> (*this, "tWR") / 1e3 * clock.GetFrequency();

    // Address bit mapping.
    // One row bit added for a 4GB DIMM with ECC.
    m_nDevicesPerRank = config.getValue<size_t> (*this, "DevicesPerRank");
    m_nRankBits = ilog2(config.getValue<size_t> (*this, "Ranks"));
    m_nRowBits = config.getValue<size_t> (*this, "RowBits");
    m_nColumnBits = config.getValue<size_t> (*this, "ColumnBits");
    m_nBurstSize = m_nDevicesPerRank * m_nBurstLength;

    // ordering of bits in address:
    m_nColumnStart = 0;
    m_nRowStart = m_nColumnStart + m_nColumnBits;
    m_nRankStart = m_nRowStart + m_nRowBits;

}

DDRChannel::DDRChannel(const std::string& name, Object& parent, Clock& clock, VirtualMemory& memory, Config& config)
    : Object(name, parent, clock),
      m_registry(config),
      m_ddrconfig("config", *this, clock, config),
      // Initialize each rank at 'no row selected'
      m_currentRow(1 << m_ddrconfig.m_nRankBits, INVALID_ROW),

      m_memory(memory),
      m_callback(0),
      m_pipeline("b_pipeline", *this, clock, m_ddrconfig.m_tCL),
      m_busy("f_busy", *this, clock, false),
      m_next_command(0),
      m_next_precharge(0),
    
      p_Request (*this, "request",  delegate::create<DDRChannel, &DDRChannel::DoRequest >(*this)),
      p_Pipeline(*this, "pipeline", delegate::create<DDRChannel, &DDRChannel::DoPipeline>(*this))
{
    m_busy.Sensitive(p_Request);
    m_pipeline.Sensitive(p_Pipeline);

    config.registerObject(*this, "ddr");
    config.registerProperty(*this, "CL", (uint32_t)m_ddrconfig.m_tCL);
    config.registerProperty(*this, "RCD", (uint32_t)m_ddrconfig.m_tRCD);
    config.registerProperty(*this, "RP", (uint32_t)m_ddrconfig.m_tRP);
    config.registerProperty(*this, "RAS", (uint32_t)m_ddrconfig.m_tRAS);
    config.registerProperty(*this, "CWL", (uint32_t)m_ddrconfig.m_tCWL);
    config.registerProperty(*this, "CCD", (uint32_t)m_ddrconfig.m_tCCD);
    config.registerProperty(*this, "WR", (uint32_t)m_ddrconfig.m_tWR);
    config.registerProperty(*this, "chips/rank", (uint32_t)m_ddrconfig.m_nDevicesPerRank);
    config.registerProperty(*this, "ranks", (uint32_t)(1UL<<m_ddrconfig.m_nRankBits));
    config.registerProperty(*this, "rows", (uint32_t)(1UL<<m_ddrconfig.m_nRowBits));
    config.registerProperty(*this, "columns", (uint32_t)(1UL<<m_ddrconfig.m_nColumnBits));
    config.registerProperty(*this, "freq", (uint32_t)clock.GetFrequency());
}

void DDRChannel::SetClient(ICallback& cb, StorageTraceSet& sts, const StorageTraceSet& storages)
{
    if (m_callback != NULL)
    {
        throw InvalidArgumentException(*this, "DDR channel can be connected to at most one root directory.");
    }
    m_callback = &cb;

    sts = m_busy;
    p_Request.SetStorageTraces(opt(m_pipeline));
    p_Pipeline.SetStorageTraces(opt(storages));

    m_registry.registerBidiRelation(cb, *this, "ddr");
}

DDRChannel::~DDRChannel()
{
}

DDRChannelRegistry::DDRChannelRegistry(const std::string& name, Object& parent, VirtualMemory& memory, Config& config)
    : Object(name, parent)
{
    size_t numChannels = config.getValueOrDefault<size_t>(*this, "NumChannels", 
                                                          config.getValue<size_t>(parent, "NumRootDirectories"));

    for (size_t i = 0; i < numChannels; ++i)
    {
        std::stringstream ss;
        ss << "channel" << i;
        Clock &ddrclock = GetKernel()->CreateClock(config.getValue<size_t>(*this, ss.str(), "Freq"));
        this->push_back(new DDRChannel(ss.str(), *this, ddrclock, memory, config));
    }
}

DDRChannelRegistry::~DDRChannelRegistry()
{
    for (size_t i = 0; i < size(); ++i)
        delete (*this)[i];
}

}


