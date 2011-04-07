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
#include <iomanip>
#include "MMIO.h"
#include "kernel.h"
#include "Processor.h"

namespace Simulator
{

Processor& MMIOInterface::GetProcessor() const
{
    return *static_cast<Processor*>(GetParent());
}

void
MMIOInterface::RegisterComponent(MemAddr address, MemSize size, AccessMode mode, MMIOComponent& component)
{
    assert(size > 0);

    // Check that there is no overlap
    RangeMap::iterator p = m_ranges.lower_bound(address);
    if (p != m_ranges.end())
    {
        if (p->first == address || (address < p->first && address + size > p->first))
        {
            // The range overlaps with an existing range after it
            throw exceptf<InvalidArgumentException>("Overlap in I/O reservation (%#016llx, %zd)",
                                                    (unsigned long long)address, (size_t)size);
        }
        
        if (p != m_ranges.begin())
        {
            RangeMap::iterator q = p; --q;
            if (q->first < address && q->first > address - q->second.size)
            {
                // The range overlaps with an existing range before it
                throw exceptf<InvalidArgumentException>("Overlap in I/O reservation (%#016llx, %zd)",
                                                        (unsigned long long)address, (size_t)size);
            }
            
        }
    }

    ComponentInterface ci;
    ci.size = size;
    ci.mode = mode;
    ci.component = &component;
    m_ranges.insert(p, std::make_pair(address, ci));
}

MMIOInterface::RangeMap::const_iterator
MMIOInterface::FindInterface(MemAddr address, MemSize size) const
{
    RangeMap::const_iterator p = m_ranges.lower_bound(address);
    if (p != m_ranges.begin() && (p == m_ranges.end() || p->first > address))
    {
        --p;
    }
    if (p != m_ranges.end() &&
        address >= p->first && p->second.size >= size &&
        address <= p->first + (p->second.size - size))
    {
        return p;
    }
    else
    {
        return m_ranges.end();
    }
}

bool MMIOInterface::IsRegisteredReadAddress(MemAddr address, MemSize size) const
{
    RangeMap::const_iterator interface = FindInterface(address, size);
    return (interface != m_ranges.end() && 
            ((int)interface->second.mode & 1) != 0);
}

bool MMIOInterface::IsRegisteredWriteAddress(MemAddr address, MemSize size) const
{
    RangeMap::const_iterator interface = FindInterface(address, size);
    return (interface != m_ranges.end() && 
            ((int)interface->second.mode & 2) != 0);
}

Result MMIOInterface::Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid)
{
    RangeMap::const_iterator interface = FindInterface(address, size);
    assert(interface != m_ranges.end());
    assert(interface->second.mode == AccessMode::READ || interface->second.mode == AccessMode::READWRITE);
    
    MemAddr base = interface->first;
    MemAddr offset = address - base;

    return interface->second.component->Read(offset, data, size, fid, tid);
}

Result MMIOInterface::Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid)
{
    RangeMap::const_iterator interface = FindInterface(address, size);
    assert(interface != m_ranges.end());
    assert(interface->second.mode == AccessMode::WRITE || interface->second.mode == AccessMode::READWRITE);
    
    MemAddr base = interface->first;
    MemAddr offset = address - base;

    return interface->second.component->Write(offset, data, size, fid, tid);
}

void MMIOInterface::Cmd_Help(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
        "The memory-mapped I/O interface intercepts memory operations at the memory stage\n"
        "in the pipeline and redirects them to a processor-local I/O bus.\n\n"
        "Supported operations:\n"
        "- info <component>\n"
        "  Lists the registered I/O components.\n";
}

void MMIOInterface::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out << "Memory-mapped I/O components:" << std::endl
        << "-----------------------------" << std::endl;

    RangeMap::const_iterator p = m_ranges.begin();

    if (p == m_ranges.end())
    {
        out << "(no component defined)" << std::endl;
    }
    else
    {
        out << std::hex << std::setfill('0');
        
        for ( ;
             p != m_ranges.end();
             ++p)
        {
            MemAddr begin = p->first;
            MemAddr size = p->second.size;
            AccessMode mode = p->second.mode;
            MMIOComponent &component = *p->second.component;
            
            out << std::setw(16) << begin 
                << " | " 
                << std::setw(16) << begin + size - 1 
                << " | "
                << (mode & READ  ? "R" : ".") 
                << (mode & WRITE ? "W" : ".")
                << " | "
                << component.GetFQN()
                << std::endl;       
        }
    }
}
    
MMIOInterface::MMIOInterface(const std::string& name, Processor& parent, Clock& clock, const Config& config)
    : Object(name, parent, clock)
{
    // config not yet used here
}

MMIOComponent::MMIOComponent(const std::string& name, MMIOInterface& parent, Clock& clock)
    : Object(name, parent, clock)
{ }

}
