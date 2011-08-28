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

Processor::IONotificationMultiplexer::IONotificationMultiplexer(const std::string& name, Object& parent, Clock& clock, RegisterFile& rf, Allocator& alloc, size_t numChannels, Config& config)
    : Object(name, parent, clock),
      m_regFile(rf),
      m_allocator(alloc),
      m_lastNotified(0),
      p_IncomingNotifications(*this, "received-notifications", delegate::create<IONotificationMultiplexer, &Processor::IONotificationMultiplexer::DoReceivedNotifications>(*this))
{
    m_writebacks.resize(numChannels, 0);
    m_mask.resize(numChannels, false);
    m_interrupts.resize(numChannels, 0);
    m_notifications.resize(numChannels, 0);

    BufferSize nqs = config.getValue<BufferSize>(*this, "NotificationQueueSize");

    for (size_t i = 0; i < numChannels; ++i)
    {
        {
            std::stringstream ss;
            ss << "wb" << i;
            m_writebacks[i] = new Register<RegAddr>(ss.str(), *this, clock);
            m_writebacks[i]->Sensitive(p_IncomingNotifications);
        }
        {
            std::stringstream ss;
            ss << "latch" << i;
            m_interrupts[i] = new SingleFlag(ss.str(), *this, clock, false);
            m_interrupts[i]->Sensitive(p_IncomingNotifications);
        }
        {
            std::stringstream ss;
            ss << "b_notification" << i;
            m_notifications[i] = new Buffer<Integer>(ss.str(), *this, clock, nqs);
            m_notifications[i]->Sensitive(p_IncomingNotifications);
        }
    }

}

Processor::IONotificationMultiplexer::~IONotificationMultiplexer()
{
    for (size_t i = 0; i < m_writebacks.size(); ++i)
    {
        delete m_writebacks[i];
        delete m_interrupts[i];
        delete m_notifications[i];
    }
}

bool Processor::IONotificationMultiplexer::ConfigureChannel(IONotificationChannelID which, Integer mode)
{
    assert(which < m_writebacks.size());

    m_mask[which] = !!mode;

    DebugIOWrite("Configuring channel %u to state: %s", (unsigned)which, m_mask[which] ? "enabled" : "disabled");

    return true;
}


bool Processor::IONotificationMultiplexer::SetWriteBackAddress(IONotificationChannelID which, const RegAddr& addr)
{
    assert(which < m_writebacks.size());

    if (!m_writebacks[which]->Empty())
    {
        // some thread is already waiting, do not
        // allow another one to wait as well!
        return false;
    }

    m_writebacks[which]->Write(addr);
    return true;
}

bool Processor::IONotificationMultiplexer::OnInterruptRequestReceived(IONotificationChannelID from)
{
    assert(from < m_interrupts.size());

    if (!m_mask[from])
    {
        DebugIONetWrite("Ignoring interrupt for disabled channel %u", (unsigned)from);
        return true;
    }
    else
    {
        DebugIONetWrite("Activating interrupt status for channel %u", (unsigned)from);
        return m_interrupts[from]->Set();
    }
}

bool Processor::IONotificationMultiplexer::OnNotificationReceived(IONotificationChannelID from, Integer tag)
{
    assert(from < m_notifications.size());

    if (!m_mask[from])
    {
        DebugIONetWrite("Ignoring notification for disabled channel %u (tag %#016llx)", (unsigned)from, (unsigned long long)tag);
        return true;
    }
    else
    {
        DebugIONetWrite("Queuing notification for channel %u (tag %#016llx)", (unsigned)from, (unsigned long long)tag);
        return m_notifications[from]->Push(tag);
    }
}


Result Processor::IONotificationMultiplexer::DoReceivedNotifications()
{
    size_t i;
    bool   notification_ready = false;
    bool   pending_notifications = false;

    /* Search for a notification to signal back to the processor.
     The following two loops implement a circular lookup through
    all devices -- round robin delivery to ensure fairness. */
    for (i = m_lastNotified; i < m_interrupts.size(); ++i)
    {
        if (m_interrupts[i]->IsSet() || !m_notifications[i]->Empty())
        {
            pending_notifications = true;
            if (!m_writebacks[i]->Empty())
            {
                notification_ready = true;
                break;
            }
        }
        else if (!m_mask[i] && !m_writebacks[i]->Empty())
        {
            // channel was disabled + no pending interrupt/notification + still a listener active,
            // so we need to release the listener otherwise it will deadlock.
            notification_ready = true;
            break;
        }
    }
    if (!notification_ready)
    {
        for (i = 0; i < m_lastNotified; ++i)
        {
            if (m_interrupts[i]->IsSet() || !m_notifications[i]->Empty())
            {
                pending_notifications = true;
                if (!m_writebacks[i]->Empty())
                {
                    notification_ready = true;
                    break;
                }
            }
            else if (!m_mask[i] && !m_writebacks[i]->Empty())
            {
                // channel was disabled + no pending interrupt/notification + still a listener active,
                // so we need to release the listener otherwise it will deadlock.
                notification_ready = true;
                break;
            }
        }
    }
    
    if (!notification_ready)
    {
        if (pending_notifications)
        {
            DebugIOWrite("Some notification is ready but no active listener exists.");
            p_IncomingNotifications.Deactivate();
        }

        // Nothing to do
        return SUCCESS;
    }

    /* A notification was found, try to notify the processor */
    
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
    
    if (regvalue.m_state == RST_FULL)
    {
        // Rare case: the request info is still in the pipeline, stall!
        DeadlockWrite("Register %s is not yet written for read completion", addr.str().c_str());
        return FAILED;
    }

    if (regvalue.m_state != RST_PENDING && regvalue.m_state != RST_WAITING)
    {
        // We're too fast, wait!
        DeadlockWrite("I/O notification delivered before register %s was cleared", addr.str().c_str());
        return FAILED;
    }
    
    // Now write
    LFID fid = regvalue.m_memory.fid;
    regvalue.m_state = RST_FULL;
    Integer value;
    const char * type;
    if (m_interrupts[i]->IsSet())
    {
        // Interrupts have priority over notifications
        value = 0;
        type = "interrupt";
    }
    else if (!m_notifications[i]->Empty())
    {
        value = m_notifications[i]->Front();
        type = "notification";
    }
    else
    {
        value = 0;
        type = "draining disabled channel";
    }

    switch (addr.type) {
        case RT_INTEGER: regvalue.m_integer       = value; break;
        case RT_FLOAT:   regvalue.m_float.integer = value; break;
    }

    if (!m_regFile.WriteRegister(addr, regvalue, true))
    {
        DeadlockWrite("Unable to write register %s", addr.str().c_str());
        return FAILED;
    }
    
    if (!m_allocator.DecreaseFamilyDependency(fid, FAMDEP_OUTSTANDING_READS))
    {
        DeadlockWrite("Unable to decrement outstanding reads on F%u", (unsigned)fid);
        return FAILED;
    }

    DebugIOWrite("Completed notification from channel %zu (%s) to %s",
                 i, type, addr.str().c_str());

    m_writebacks[i]->Clear();

    if (m_interrupts[i]->IsSet())
    {
        m_interrupts[i]->Clear();
    }
    else if (!m_notifications[i]->Empty())
    {
        m_notifications[i]->Pop();
    }

    COMMIT {
        m_lastNotified = i;
    }

    return SUCCESS;
}

    
}
