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
#include "Processor.h"
#include "sim/config.h"
#include <sstream>

namespace Simulator
{

Processor::IOInterruptMultiplexer::IOInterruptMultiplexer(const std::string& name, Object& parent, Clock& clock, RegisterFile& rf, IOBusInterface& iobus, const Config& config)
    : Object(name, parent, clock),
      m_regFile(rf),
      m_iobus(iobus),
      m_lastNotified(0),
      p_IncomingInterrupts("received-interrupts", delegate::create<IOInterruptMultiplexer, &Processor::IOInterruptMultiplexer::DoReceivedInterrupts>(*this))
{
    size_t numDevices = config.getValue<size_t>("AsyncIONumDeviceSlots", 16);

    m_writebacks.resize(numDevices, 0);
    m_interrupts.resize(numDevices, 0);
    for (size_t i = 0; i < numDevices; ++i)
    {
        {
            std::stringstream ss;
            ss << "wb" << i;
            m_writebacks[i] = new Register<RegAddr>(ss.str(), *this, clock);
            m_writebacks[i]->Sensitive(p_IncomingInterrupts);
        }
        {
            std::stringstream ss;
            ss << "int" << i;
            m_interrupts[i] = new SingleFlag(ss.str(), *this, clock, false);
            m_interrupts[i]->Sensitive(p_IncomingInterrupts);
        }
    }

}

bool Processor::IOInterruptMultiplexer::SetWriteBackAddress(IODeviceID dev, const RegAddr& addr)
{
    assert(dev < m_writebacks.size());

    if (!m_writebacks[dev]->Empty())
    {
        // some thread is already waiting, do not
        // allow another one to wait as well!
        return false;
    }

    m_writebacks[dev]->Write(addr);
    return true;
}

bool Processor::IOInterruptMultiplexer::OnInterruptRequestReceived(IODeviceID from)
{
    assert(from < m_interrupts.size());
    
    return m_interrupts[from]->Set();
}

Result Processor::IOInterruptMultiplexer::DoReceivedInterrupts()
{
    size_t i;
    bool   found = false;

    /* Search for an interrupt to signal back to the processor */
    for (i = m_lastNotified; i < m_interrupts.size(); ++i)
    {
        if (m_interrupts[i]->IsSet() && !m_writebacks[i]->Empty())
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        for (i = 0; i < m_lastNotified; ++i)
        {
            if (m_interrupts[i]->IsSet() && !m_writebacks[i]->Empty())
            {
                found = true;
                break;
            }
        }
    }
    
    if (!found)
    {
        // Nothing to do
        return SUCCESS;
    }

    /* An interrupt was found, try to notify the processor */
    
    const RegAddr& addr = m_writebacks[i]->Read();

    if (!m_regFile.p_asyncW.Write(addr))
    {
        DeadlockWrite("Unable to acquire port to write back %s", addr.str().c_str());
        return FAILED;
    }

    RegValue regvalue;
    if (!m_regFile.ReadRegister(addr, regvalue))
    {
        DeadlockWrite("Unable to read register %s", addr.str().c_str());
        return FAILED;
    }
    
    if (regvalue.m_state == RST_FULL || regvalue.m_memory.size == 0)
    {
        // Rare case: the request info is still in the pipeline, stall!
        DeadlockWrite("Register %s is not yet written for read completion", addr.str().c_str());
        return FAILED;
    }

    if (regvalue.m_state != RST_PENDING && regvalue.m_state != RST_WAITING)
    {
        // We're too fast, wait!
        DeadlockWrite("I/O interrupt delivered before register %s was cleared", addr.str().c_str());
        return FAILED;
    }
    
    // Now write
    regvalue.m_state = RST_FULL;

    switch (addr.type) {
        case RT_INTEGER: regvalue.m_integer       = i; break;
        case RT_FLOAT:   regvalue.m_float.integer = i; break;
    }

    if (!m_regFile.WriteRegister(addr, regvalue, false))
    {
        DeadlockWrite("Unable to write register %s", addr.str().c_str());
        return FAILED;
    }
    
    if (!m_iobus.SendInterruptAck(i))
    {
        DeadlockWrite("Unable to send interrupt acknowledgement to device %zu", i);
        return FAILED;
    }

    DebugIOWrite("Completed interrupt notification from device %zu to %s",
                 i, addr.str().c_str());

    m_writebacks[i]->Clear();
    m_interrupts[i]->Clear();
    m_lastNotified = i;

    return SUCCESS;
}

    
}
