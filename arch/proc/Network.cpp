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
#include "FamilyTable.h"
#include "sim/config.h"
#include "sim/log2.h"
#include <cassert>
#include <iostream>
#include <sstream>

using namespace std;

namespace Simulator
{

Processor::Network::Network(
    const std::string&        name,
    Processor&                parent,
    Clock&                    clock,
    const vector<Processor*>& grid,
    Allocator&                alloc,
    RegisterFile&             regFile,
    FamilyTable&              familyTable,
    Config&                   config
) :
    Object(name, parent, clock),
    
    m_parent     (parent),
    m_regFile    (regFile),
    m_familyTable(familyTable),
    m_allocator  (alloc),
    
    m_prev(NULL),
    m_next(NULL),
    m_grid(grid),
    
    m_loadBalanceThreshold(config.getValue<unsigned>(*this, "LoadBalanceThreshold")),

#define CONSTRUCT_REGISTER(name) name(*this, #name)
    CONSTRUCT_REGISTER(m_delegateOut),
    CONSTRUCT_REGISTER(m_delegateIn),
    CONSTRUCT_REGISTER(m_link),
    CONSTRUCT_REGISTER(m_allocResponse),
#undef CONTRUCT_REGISTER
    m_syncs ("b_syncs", *this, clock, familyTable.GetNumFamilies(), 3 ),

    p_DelegationOut(*this, "delegation-out", delegate::create<Network, &Processor::Network::DoDelegationOut>(*this)),
    p_DelegationIn (*this, "delegation-in",  delegate::create<Network, &Processor::Network::DoDelegationIn >(*this)),
    p_Link         (*this, "link",           delegate::create<Network, &Processor::Network::DoLink         >(*this)),
    p_AllocResponse(*this, "alloc-response", delegate::create<Network, &Processor::Network::DoAllocResponse>(*this)),
    p_Syncs        (*this, "syncs",          delegate::create<Network, &Processor::Network::DoSyncs        >(*this))
{
    m_delegateOut.Sensitive(p_DelegationOut);
    m_delegateIn .Sensitive(p_DelegationIn);
    
    m_link.in.Sensitive(p_Link);
    m_syncs.Sensitive(p_Syncs);
    
    m_allocResponse.in.Sensitive(p_AllocResponse);
}

void Processor::Network::Initialize(Network* prev, Network* next)
{
    m_prev = prev;
    m_next = next;

#define INITIALIZE(object,dest) object.Initialize(dest->object)
    if (next != NULL) {
        INITIALIZE(m_link, next);
    }
    
    if (prev != NULL) {
        INITIALIZE(m_allocResponse, prev);
    }
#undef INITIALIZE
}

bool Processor::Network::SendMessage(const RemoteMessage& msg)
{
    assert(msg.type != RemoteMessage::MSG_NONE);
    
    // Delegated message
    DelegateMessage dmsg;
    (RemoteMessage&)dmsg = msg;
    dmsg.src = m_parent.GetPID();

    // Get destination
    switch (msg.type)
    {
    case RemoteMessage::MSG_ALLOCATE:     
    case RemoteMessage::MSG_BUNDLE:       dmsg.dest = msg.allocate.place.pid; break;     
    case RemoteMessage::MSG_SET_PROPERTY: dmsg.dest = msg.property.fid.pid; break;
    case RemoteMessage::MSG_CREATE:       dmsg.dest = msg.create.fid.pid; break;
    case RemoteMessage::MSG_DETACH:       dmsg.dest = msg.detach.fid.pid; break;
    case RemoteMessage::MSG_SYNC:         dmsg.dest = msg.sync.fid.pid; break;
    case RemoteMessage::MSG_RAW_REGISTER: dmsg.dest = msg.rawreg.pid; break;
    case RemoteMessage::MSG_FAM_REGISTER: dmsg.dest = msg.famreg.fid.pid; break;
    case RemoteMessage::MSG_BREAK:        dmsg.dest = msg.brk.pid; break;
    default:                              dmsg.dest = INVALID_PID; break;
    }
    
    assert(dmsg.dest != INVALID_PID);

    if (dmsg.dest == dmsg.src)
    {
        if (GetKernel()->GetActiveProcess() == &p_DelegationIn)
        {
           /*
            This response is meant for us as a result of an input message.
            To avoid having to go to the output buffer, and then into this
            input buffer again, we forcibly overwrite the contents of the input
            register with the response.
            This is also necessary to avoid a circular dependency on the output buffer.
            */
            m_delegateIn.Simulator::Register<DelegateMessage>::Write(dmsg);

            // Return here to avoid clearing the input buffer. We want to process this
            // response next cycle.
            return SUCCESS;
        }

        if (!m_delegateIn.Write(dmsg))
        {
            DeadlockWrite("Unable to buffer local network message to loopback: %s", msg.str().c_str());
            return false;
        }
        DebugNetWrite("Sending delegation message to loopback: %s", msg.str().c_str());
    }
    else
    {
        if (!m_delegateOut.Write(dmsg))
        {
            DeadlockWrite("Unable to buffer remote network message for CPU%u: %s", (unsigned)dmsg.dest, msg.str().c_str());
            return false;
        }
        DebugNetWrite("Sending delegation message to CPU%u: %s", (unsigned)dmsg.dest, msg.str().c_str());
    }
    return true;
}

bool Processor::Network::SendMessage(const LinkMessage& msg)
{
    assert(m_next != NULL);
    if (!m_link.out.Write(msg))
    {
        DeadlockWrite("Unable to buffer link message: %s", msg.str().c_str());
        return false;
    }
    DebugNetWrite("Sending link message: %s", msg.str().c_str());
    return true;
}

bool Processor::Network::SendAllocResponse(const AllocResponse& msg)
{
    if (!m_allocResponse.out.Write(msg))
    {
        return false;
    }
    return true;
}

bool Processor::Network::SendSync(const SyncInfo& sync)
{
    if (!m_syncs.Push(sync))
    {
        // This shouldn't happen; the buffer should be large enough
        // to accomodate all family events (family table size).
        assert(false);
        return false;
    }
    return true;
}

Result Processor::Network::DoSyncs()
{
    assert(!m_syncs.Empty());
    const SyncInfo& info = m_syncs.Front();

    // Synchronization, send remote register write
    assert(info.fid != INVALID_LFID);
    assert(info.pid != INVALID_PID);
    assert(info.reg != INVALID_REG_INDEX);
    
    RemoteMessage msg;
    msg.type = RemoteMessage::MSG_RAW_REGISTER;
    msg.rawreg.pid             = info.pid;
    msg.rawreg.addr            = MAKE_REGADDR(RT_INTEGER, info.reg);
    msg.rawreg.value.m_state   = RST_FULL;
    msg.rawreg.value.m_integer = 0;

    if (!SendMessage(msg))
    {
        DeadlockWrite("Unable to buffer family event onto delegation network");
        return FAILED;
    }
    
    if (!m_allocator.DecreaseFamilyDependency(info.fid, FAMDEP_SYNC_SENT))
    {
        assert(false);
        DeadlockWrite("Unable to mark sync_sent in F%u", (unsigned)info.fid);
        return FAILED;
    }
    
    m_syncs.Pop();
    return SUCCESS;
}

Result Processor::Network::DoAllocResponse()
{
    assert(!m_allocResponse.in.Empty());
    AllocResponse msg = m_allocResponse.in.Read();
    
    const LFID lfid = msg.prev_fid;
    Family& family = m_familyTable[lfid];

    // Grab the previous FID from the link field
    msg.prev_fid = family.link;
    
    // Set the link field to the next FID (LFID_INVALID if failed)
    COMMIT{ family.link = msg.next_fid; }
    
    // Number of cores in the place up to, and including, this core
    const PSize numCores = (m_parent.GetPID() % family.placeSize) + 1;
    
    if (msg.numCores == 0 && !msg.exact && IsPowerOfTwo(numCores))
    {
        // We've unwinded the place to a power of two.
        // Stop unwinding and commit.
        msg.numCores = numCores;
        
        DebugSimWrite("Unwound allocation to %u cores", (unsigned)numCores);
    }
    
    if (msg.numCores == 0)
    {
        // Unwind the allocation by releasing the context
        m_allocator.ReleaseContext(lfid);
    }
    else
    {
        // Commit the allocation
        COMMIT{ family.numCores = msg.numCores; }
        msg.next_fid = lfid;
    }
    
    if (msg.prev_fid == INVALID_LFID)
    {
        // We're back at the first core, acknowledge allocate or fail
        FID fid;
        if (msg.numCores == 0)
        {
            // We can only fail at the first core if we have the exact flag
            // (Cause otherwise we commit from the power of two, and 1 core
            // always succeeds).
            assert(msg.exact);
            
            fid.pid        = 0;
            fid.lfid       = 0;
            fid.capability = 0;

            DebugSimWrite("Exact allocation failed");
        }
        else
        {
            fid.pid        = m_parent.GetPID();
            fid.lfid       = lfid;
            fid.capability = family.capability;
            
            DebugSimWrite("Allocation succeeded: F%u@CPU%u", (unsigned)fid.lfid, (unsigned)fid.pid);
        }
        
        RemoteMessage fwd;
        fwd.type = RemoteMessage::MSG_RAW_REGISTER;
        fwd.rawreg.pid             = msg.completion_pid;
        fwd.rawreg.addr            = MAKE_REGADDR(RT_INTEGER, msg.completion_reg);
        fwd.rawreg.value.m_state   = RST_FULL;
        fwd.rawreg.value.m_integer = m_parent.PackFID(fid);

        if (!SendMessage(fwd))
        {
            DeadlockWrite("Unable to send remote allocation writeback");
            return FAILED;
        }
    }
    // Forward response
    else if (!m_allocResponse.out.Write(msg))
    {
        return FAILED;
    }
    m_allocResponse.in.Clear();
    return SUCCESS;
}

bool Processor::Network::ReadLastShared(LFID fid, const RegAddr& raddr, RegValue& value)
{
    const RegAddr addr = m_allocator.GetRemoteRegisterAddress(fid, RRT_LAST_SHARED, raddr);
    assert(addr != INVALID_REG);

    // The thread of the register has been allocated, read it
    if (!m_regFile.p_asyncR.Read())
    {
        return false;
    }

    if (!m_regFile.ReadRegister(addr, value))
    {
        return false;
    }

    if (value.m_state != RST_FULL)
    {
        /*
         It's possible that the last shared in a family hasn't been written.
         Print a warning, and return a dummy value.
        */
        DebugProgWrite("Reading unwritten %s register %s from last thread in F%u from %s",
            GetRemoteRegisterTypeString(RRT_LAST_SHARED),
            raddr.str().c_str(),
            (unsigned)fid,
            addr.str().c_str()
        );
            
        value.m_state = RST_FULL;
        switch (addr.type)
        {
        case RT_FLOAT:   value.m_float.fromfloat(0.0f); break;
        case RT_INTEGER: value.m_integer = 0; break;
        }
    }
    else
    {
        DebugSimWrite("Read %s register %s in F%u from %s",
            GetRemoteRegisterTypeString(RRT_LAST_SHARED),
            raddr.str().c_str(),
            (unsigned)fid,
            addr.str().c_str()
        );
    }
    return true;
}

bool Processor::Network::WriteRegister(LFID fid, RemoteRegType kind, const RegAddr& raddr, const RegValue& value)
{
    RegAddr addr = m_allocator.GetRemoteRegisterAddress(fid, kind, raddr);
    if (addr != INVALID_REG)
    {
        // Write it
        DebugSimWrite("Writing %s register %s in F%u to %s",
            GetRemoteRegisterTypeString(kind),
            raddr.str().c_str(),
            (unsigned)fid,
            addr.str().c_str()
        );
                    
        if (!m_regFile.p_asyncW.Write(addr))
        {
            DeadlockWrite("Unable to acquire port to write register response to %s", addr.str().c_str());
            return false;
        }
                    
        if (!m_regFile.WriteRegister(addr, value, false))
        {
            DeadlockWrite("Unable to write register response to %s", addr.str().c_str());
            return false;
        }       
    }
    return true;
}


bool Processor::Network::OnSync(LFID fid, PID completion_pid, RegIndex completion_reg)
{
    Family& family = m_familyTable[fid];
    if (family.link != INVALID_LFID)
    {
        // Forward the sync to the last core
        LinkMessage fwd;
        fwd.type = LinkMessage::MSG_SYNC;
        fwd.sync.fid            = family.link;
        fwd.sync.completion_pid = completion_pid;
        fwd.sync.completion_reg = completion_reg;

        if (!SendMessage(fwd))
        {
            DeadlockWrite("Unable to forward sync onto link");
            return false;
        }
    }
    // We're the last core in the family
    else if (!family.sync.done)
    {
        // The family hasn't terminated yet, setup sync link
        COMMIT
        {
            family.sync.pid = completion_pid;
            family.sync.reg = completion_reg;
            family.dependencies.syncSent = false;
        }
    }
    else
    {
        // The family has already completed, send sync result back
        SyncInfo info;
        info.fid = fid;
        info.pid = completion_pid;
        info.reg = completion_reg;
        
        COMMIT{ family.dependencies.syncSent = false; }
        
        if (!SendSync(info))
        {
            DeadlockWrite("Unable to buffer sync acknowledgement");
            return false;
        }
    }
    return true;
}

bool Processor::Network::OnDetach(LFID fid)
{
    if (!m_allocator.DecreaseFamilyDependency(fid, FAMDEP_DETACHED))
    {
        DeadlockWrite("Unable to mark family detachment of F%u", (unsigned)fid);
        return false;
    }

    Family& family = m_familyTable[fid];
    if (family.link != INVALID_LFID)
    {
        // Forward message on link
        LinkMessage msg;
        msg.type = LinkMessage::MSG_DETACH;
        msg.detach.fid = family.link;
        if (!SendMessage(msg))
        {
            return false;
        }
    }
    DebugSimWrite("Detached F%u", (unsigned)fid);
    return true;
}

bool Processor::Network::OnBreak(LFID fid)
{
    const Family& family = m_familyTable[fid];
    if (!family.dependencies.allocationDone)
    {
        if (!m_allocator.DecreaseFamilyDependency(fid, FAMDEP_ALLOCATION_DONE))
        {
            DeadlockWrite("Unable to mark allocation done of F%u", (unsigned)fid);
            return false;
        }
    }
    
    if (family.link != INVALID_LFID)
    {
        LinkMessage msg;
        msg.type    = LinkMessage::MSG_BREAK;
        msg.brk.fid = family.link;
		
        if (!SendMessage(msg))
        {
            DeadlockWrite("Unable to send break message");
            return false;
        }
    }
    DebugSimWrite("Broken F%u", (unsigned)fid);
    return true;
}

Result Processor::Network::DoDelegationOut()
{
    // Send outgoing message over the delegation network
    assert(!m_delegateOut.Empty());
    const DelegateMessage& msg = m_delegateOut.Read();
    assert(msg.src == m_parent.GetPID());
    assert(msg.dest != m_parent.GetPID());

    // Send to destination
    if (!m_grid[msg.dest]->GetNetwork().m_delegateIn.Write(msg))
    {
        DeadlockWrite("Unable to buffer outgoing delegation message into destination input buffer");
        return FAILED;
    }
    
    m_delegateOut.Clear();
    return SUCCESS;
}

Result Processor::Network::DoDelegationIn()
{
    // Handle incoming message from the delegation network
    // Note that we make a copy here, because we want to clear it before
    // we process it, because we may overwrite the entry during processing.
    assert(!m_delegateIn.Empty());
    DelegateMessage msg = m_delegateIn.Read();
    m_delegateIn.Clear();
    assert(msg.dest == m_parent.GetPID());

    DebugNetWrite("Incoming delegation message: %s", msg.str().c_str());

    
    switch (msg.type)
    {
    case DelegateMessage::MSG_ALLOCATE:
        if (msg.allocate.type == ALLOCATE_BALANCED && msg.allocate.place.size > 1)
        {
            unsigned used_contexts = m_familyTable.GetNumUsedFamilies(CONTEXT_NORMAL);
            if (used_contexts >= m_loadBalanceThreshold)
            {
                // Forward on link network
                LinkMessage fwd;
                fwd.type = LinkMessage::MSG_BALLOCATE;
                fwd.ballocate.min_contexts   = used_contexts;
                fwd.ballocate.min_pid        = m_parent.GetPID();
                fwd.ballocate.size           = msg.allocate.place.size;
                fwd.ballocate.suspend        = msg.allocate.suspend;
                fwd.ballocate.completion_pid = msg.allocate.completion_pid;
                fwd.ballocate.completion_reg = msg.allocate.completion_reg;
                
                if (!SendMessage(fwd))
                {
                    return FAILED;
                }
                break;
            }
            
            // We're below the threshold; allocate here as a place of one
            msg.allocate.type = ALLOCATE_SINGLE;
        }

        if (!m_allocator.QueueFamilyAllocation(msg, false))
        {
            DeadlockWrite("Unable to process family allocation request");
            return FAILED;
        }
        break;
        
    case DelegateMessage::MSG_BUNDLE:
        if (!m_allocator.QueueFamilyAllocation(msg, true))
        {
            DeadlockWrite("Unable to process received indirect create");
            return FAILED;
        }
        break;
    
    case DelegateMessage::MSG_SET_PROPERTY:
    {
        Family& family = m_allocator.GetFamilyChecked(msg.property.fid.lfid, msg.property.fid.capability);
        COMMIT
        {
            switch (msg.property.type)
            {
                case FAMPROP_START: family.start         = (SInteger)msg.property.value; break;
                case FAMPROP_LIMIT: family.limit         = (SInteger)msg.property.value; break;
                case FAMPROP_STEP:  family.step          = (SInteger)msg.property.value; break;
                case FAMPROP_BLOCK: family.physBlockSize = (TSize)msg.property.value; break;
                default: assert(false); break;
            }
        }
        
        if (family.link != INVALID_LFID)
        {
            // Forward message on link
            LinkMessage fwd;
            fwd.type           = LinkMessage::MSG_SET_PROPERTY;
            fwd.property.fid   = family.link;
            fwd.property.type  = msg.property.type;
            fwd.property.value = msg.property.value;
            
            if (!SendMessage(fwd))
            {
                return FAILED;
            }
        }
        break;
    }
        
    case DelegateMessage::MSG_CREATE:
	{
            // Process the received delegated create
            PID src = (msg.create.bundle) ? msg.create.completion_pid : msg.src;
            if (!m_allocator.QueueCreate(msg, src))
            {
                DeadlockWrite("Unable to process received delegation create");
                return FAILED;
            }
            break;
	}
        
    case DelegateMessage::MSG_SYNC:
        // Authorize family access
        m_allocator.GetFamilyChecked(msg.sync.fid.lfid, msg.sync.fid.capability);
        if (!OnSync(msg.sync.fid.lfid, msg.src, msg.sync.completion_reg))
        {
            return FAILED;
        }
        break;
                
    case DelegateMessage::MSG_DETACH:
        // Authorize family access
        m_allocator.GetFamilyChecked(msg.detach.fid.lfid, msg.detach.fid.capability);
        if (!OnDetach(msg.detach.fid.lfid))
        {
            return FAILED;
        }
        break;
	
    case DelegateMessage::MSG_BREAK:
        // A break can only be sent from the family itself,
        // so no capability-verification has to be done.
        if (!OnBreak(msg.brk.fid))
        {
            return FAILED;
        }
        break;
        
    case DelegateMessage::MSG_RAW_REGISTER:
        // Remote register write.
        // No validation necessary; cannot be sent by user code.
        assert(msg.rawreg.value.m_state == RST_FULL);
          
        if (!m_regFile.p_asyncW.Write(msg.rawreg.addr))
        {
            DeadlockWrite("Unable to acquire port to write register response to %s", msg.rawreg.addr.str().c_str());
            return FAILED;
        }
                    
        if (!m_regFile.WriteRegister(msg.rawreg.addr, msg.rawreg.value, false))
        {
            DeadlockWrite("Unable to write register response to %s", msg.rawreg.addr.str().c_str());
            return FAILED;
        }
            
        DebugSimWrite("Written register response to %s", msg.rawreg.addr.str().c_str() );
        break;
        
    case DelegateMessage::MSG_FAM_REGISTER:
        switch (msg.famreg.kind)
        {
        case RRT_LAST_SHARED:
        {
            m_allocator.GetFamilyChecked(msg.famreg.fid.lfid, msg.famreg.fid.capability);
            
            // Create response and we're done
            DelegateMessage response;
            response.type        = DelegateMessage::MSG_RAW_REGISTER;
            response.src         = m_parent.GetPID();
            response.dest        = msg.src;
            response.rawreg.pid  = msg.src;
            response.rawreg.addr = MAKE_REGADDR(msg.famreg.addr.type, msg.famreg.completion_reg);

            if (!ReadLastShared(msg.famreg.fid.lfid, msg.famreg.addr, response.rawreg.value))
            {
                return FAILED;
            }

            if (!SendMessage(response))
            {
                DeadlockWrite("Unable to buffer outgoing remote register response");
                return FAILED;
            }
            break;
        }
        
        case RRT_FIRST_DEPENDENT:
            m_allocator.GetFamilyChecked(msg.famreg.fid.lfid, msg.famreg.fid.capability);
            if (!WriteRegister(msg.famreg.fid.lfid, msg.famreg.kind, msg.famreg.addr, msg.famreg.value))
            {
                return FAILED;
            }           
            break;

        case RRT_GLOBAL:
        {
            const Family& family = m_allocator.GetFamilyChecked(msg.famreg.fid.lfid, msg.famreg.fid.capability);
            if (!WriteRegister(msg.famreg.fid.lfid, msg.famreg.kind, msg.famreg.addr, msg.famreg.value))
            {
                return FAILED;
            }
            
            if (family.link != INVALID_LFID)
            {
                // Forward on link as well
                LinkMessage fwd;
                fwd.type         = LinkMessage::MSG_GLOBAL;
                fwd.global.fid   = family.link;
                fwd.global.addr  = msg.famreg.addr;
                fwd.global.value = msg.famreg.value;
                if (!SendMessage(fwd))
                {
                    return FAILED;
                }           
            }
            break;
        }
        
        default:
            assert(false);
            break;
        }
        break;
    
    default:
        assert(false);
        break;
    }

    return SUCCESS;
}
    
Result Processor::Network::DoLink()
{
    // Handle incoming message from the link
    assert(!m_link.in.Empty());
    const LinkMessage& msg = m_link.in.Read();

    DebugNetWrite("Incoming link message: %s", msg.str().c_str());
    
    switch (msg.type)
    {
    case LinkMessage::MSG_ALLOCATE:
        if (!m_allocator.QueueFamilyAllocation(msg))
        {
            DeadlockWrite("Unable to process family allocation request");
            return FAILED;
        }
        break;

    case LinkMessage::MSG_BALLOCATE:
    {
        RemoteMessage rmsg;
        rmsg.allocate.place.pid = m_parent.GetPID();

        unsigned used_contexts = m_familyTable.GetNumUsedFamilies(CONTEXT_NORMAL);
        if (used_contexts >= m_loadBalanceThreshold)
        {
            if ((m_parent.GetPID() + 1) % msg.ballocate.size != 0)
            {
                // Not the last core yet; forward the message
                LinkMessage fwd(msg);
                if (used_contexts <= fwd.ballocate.min_contexts)
                {
                    // This core's the new minimum
                    fwd.ballocate.min_contexts = used_contexts;
                    fwd.ballocate.min_pid      = m_parent.GetPID();
                }
                
                if (!SendMessage(fwd))
                {
                    return FAILED;
                }
                break;
            }
            
            // Last core and we haven't met threshold, allocate on minimum
            if (used_contexts > msg.ballocate.min_contexts)
            {
                // Minimum is not on this core, send it to the minimum
                rmsg.allocate.place.pid = msg.ballocate.min_pid;
            }
        }
        
        // Send a remote allocate as a place of one
        rmsg.type = RemoteMessage::MSG_ALLOCATE;
        rmsg.allocate.place.size     = msg.ballocate.size;
        rmsg.allocate.suspend        = msg.ballocate.suspend;
        rmsg.allocate.exclusive      = false;
        rmsg.allocate.type           = ALLOCATE_SINGLE;
        rmsg.allocate.completion_pid = msg.ballocate.completion_pid;
        rmsg.allocate.completion_reg = msg.ballocate.completion_reg;
        if (!SendMessage(rmsg))
        {
            return FAILED;
        }

        break;
    }
    
    case LinkMessage::MSG_SET_PROPERTY:
    {
        Family& family = m_familyTable[msg.property.fid];
        COMMIT
        {
            // Set property
            switch (msg.property.type)
            {
                case FAMPROP_START: family.start         = (SInteger)msg.property.value; break;
                case FAMPROP_LIMIT: family.limit         = (SInteger)msg.property.value; break;
                case FAMPROP_STEP:  family.step          = (SInteger)msg.property.value; break;
                case FAMPROP_BLOCK: family.physBlockSize = (TSize)msg.property.value; break;
                default: assert(false); break;
            }
        }
        
        if (family.link != INVALID_LFID)
        {
            // Forward message on link
            LinkMessage fwd(msg);
            fwd.property.fid = family.link;
            if (!SendMessage(fwd))
            {
                return FAILED;
            }
        }
        break;
    }
        
    case LinkMessage::MSG_CREATE:
    {
        Family& family = m_familyTable[msg.create.fid];
        
        if (msg.create.numCores == 0)
        {
            // Forward message and clean up context
            if (family.link != INVALID_LFID)
            {
                LinkMessage fwd(msg);
                fwd.create.fid = family.link;
                if (!SendMessage(fwd))
                {
                    DeadlockWrite("Unable to forward restrict message");
                    return FAILED;
                }
            }

            m_allocator.ReleaseContext(msg.create.fid);
            DebugSimWrite("Cleaned up F%u due to restrict", (unsigned)msg.create.fid);
        }
        // Process the received create.
        // This will forward the message.
        else if (!m_allocator.QueueCreate(msg))
        {
            DeadlockWrite("Unable to process received place create");
            return FAILED;
        }
        break;
    }
    
    case LinkMessage::MSG_DONE:
        if (!m_allocator.DecreaseFamilyDependency(msg.done.fid, FAMDEP_PREV_SYNCHRONIZED))
        {
            DeadlockWrite("Unable to mark family synchronization on F%u", (unsigned)msg.done.fid);
            return FAILED;
        }
        break;
        
    case LinkMessage::MSG_SYNC:
        if (!OnSync(msg.sync.fid, msg.sync.completion_pid, msg.sync.completion_reg))
        {
            return FAILED;
        }
        break;
    
    case LinkMessage::MSG_DETACH:
        if (!OnDetach(msg.detach.fid))
        {
            return FAILED;
        }
        break;
    
    case LinkMessage::MSG_GLOBAL:
    {
        const Family& family = m_familyTable[msg.global.fid];
        if (!WriteRegister(msg.global.fid, RRT_GLOBAL, msg.global.addr, msg.global.value))
        {
            return FAILED;
        }
            
        if (family.link != INVALID_LFID)
        {
            // Forward on link as well
            LinkMessage fwd(msg);
            fwd.global.fid = family.link;
            if (!SendMessage(fwd))
            {
                return FAILED;
            }           
        }
        break;
    }

    case LinkMessage::MSG_BREAK:
        if (!OnBreak(msg.brk.fid))
        {
            return FAILED;
        }
        break;

    default:
        assert(false);
        break;
    }

    m_link.in.Clear();
    return SUCCESS;
}

void Processor::Network::Cmd_Info(ostream& out, const vector<string>& /* arguments */) const
{
    out <<
    "The network component manages all inter-processor communication such as\n"
    "the broadcasting of creates or exchange of shareds and globals. It also\n"
    "connects each processor to the delegation network.\n"
    "For communication within the group it uses a ring network exclusively.\n\n"
    "Supported operations:\n"
    "- inspect <component>\n"
    "  Reads and displays the various registers and buffers from the component.\n";
}

void Processor::Network::Cmd_Read(ostream& out, const vector<string>& /* arguments */) const
{
    const struct {
        const char*                      name;
        const Register<DelegateMessage>& reg;
    } Registers[2] = {
        {"Incoming", m_delegateIn},
        {"Outgoing", m_delegateOut}
    };
    
    out << dec;
    for (size_t i = 0; i < 2; ++i)
    {
        out << Registers[i].name << " delegation network:" << endl;
        if (!Registers[i].reg.Empty()) {
            const DelegateMessage& msg = Registers[i].reg.Read();
            out << msg.str();
        } else {
            out << "Empty";
        }
        out << endl;
    }

    const struct {
        const char*                  name;
        const Register<LinkMessage>& reg;
    } LinkRegisters[2] = {
        {"Incoming", m_link.in},
        {"Outgoing", m_link.out}
    };
    
    for (size_t i = 0; i < 2; ++i)
    {
        out << LinkRegisters[i].name << " link:" << endl;
        out << dec;
        if (!LinkRegisters[i].reg.Empty()) {
            const LinkMessage& msg = LinkRegisters[i].reg.Read();
            out << msg.str();
        } else {
            out << "Empty";
        }
        out << endl;
    }
    
    out << "Family events:" << dec << endl;
    for (Buffer<SyncInfo>::const_iterator p = m_syncs.begin(); p != m_syncs.end(); ++p)
    {
        out << "R" << p->reg << "@P" << p->pid << " ";
    }
    out << endl;
}

string Processor::RemoteMessage::str() const
{
    ostringstream ss;

    switch (type)
    {
    case MSG_NONE: ss << "(no message)"; break;
    case MSG_ALLOCATE: 
        ss << "Allocate[ "
           << " pid(" << allocate.place.str()
           << ") susp(" << allocate.suspend
           << ") excl(" << allocate.exclusive
           << ") type(" << allocate.type
           << ") cpid(" << allocate.completion_pid
           << ") creg(" << allocate.completion_reg
           << ") ]";
        break;
    case MSG_BUNDLE: 
        ss << "Bundle[ "
           << " pid(" << allocate.place.str()
           << ") susp(" << allocate.suspend
           << ") excl(" << allocate.exclusive
           << ") type(" << allocate.type
           << ") cpid(" << allocate.completion_pid
           << ") creg(" << allocate.completion_reg
           << ") pc(" << hex << "0x" << allocate.bundle.pc << dec
           << ") parm(" << allocate.bundle.parameter
           << ") idx(" << allocate.bundle.index
           << ") ]";
        break;
    case MSG_SET_PROPERTY: 
        ss << "SetProperty[ "
           << "fid(" << property.fid.str()
           << ") type(" << property.type
           << ") val(" << property.value
           << ") ]";
        break;
    case MSG_CREATE: 
        ss << "Create[ "
           << "fid(" << create.fid.str()
           << ") pc(" << hex << "0x" << create.address << dec
           << ") creg(" << create.completion_reg
           << ") ]";
            ;
        break;
    case MSG_SYNC: 
        ss << "Sync[ "
           << "fid(" << sync.fid.str()
           << ") creg(" << sync.completion_reg
           << ") ]";
            ;
        break;
    case MSG_DETACH: 
        ss << "Detach[ "
           << "fid(" << detach.fid.str()
           << ") ]";
            ;
        break;
    case MSG_BREAK: 
        ss << "Break[ "
           << "pid(" << brk.pid
           << ") lfid(" << brk.fid
           << ") ]";
            ;
        break;
    case MSG_RAW_REGISTER: 
        ss << "RawRegister[ "
           << "pid(" << rawreg.pid
           << ") addr(" << rawreg.addr.str()
           << ") val(" << rawreg.value.str(rawreg.addr.type)
           << ") ]";
            ;
        break;
    case MSG_FAM_REGISTER: 
        ss << "FamRegister[ "
           << "fid(" << famreg.fid.str()
           << ") kind(" << GetRemoteRegisterTypeString(famreg.kind)
           << ") addr(" << famreg.addr.str()
            ;
        if (famreg.kind == RRT_LAST_SHARED)
        {
            ss << ") creg(" << famreg.completion_reg;
        }
        else
        {
            ss << ") val(" << famreg.value.str(famreg.addr.type);
        }
        ss << ") ]";
        break;
    default:
        assert(false); // all types should be listed here.
    }

    return ss.str();
}

string Processor::LinkMessage::str() const
{
    ostringstream ss;

    switch (type)
    {
    case MSG_ALLOCATE: 
        ss << "Allocate:    "
           << "ffid " << allocate.first_fid
           << "pfid " << allocate.prev_fid
           << "psz  " << allocate.size
           << "exct " << allocate.exact
           << "susp " << allocate.suspend
           << "cpid " << allocate.completion_pid
           << "creg " << allocate.completion_reg
            ;
        break;
    case MSG_BALLOCATE: 
        ss << "BAllocate:   "
           << "minc " << ballocate.min_contexts
           << "minp " << ballocate.min_pid
           << "psz  " << ballocate.size
           << "susp " << ballocate.suspend
           << "cpid " << ballocate.completion_pid
           << "creg " << ballocate.completion_reg
            ;
        break;
    case MSG_SET_PROPERTY: 
        ss << "SetProperty: "
           << "lfid " << property.fid
           << "type " << property.type
           << "val  " << property.value
            ;
        break;
    case MSG_CREATE: 
        ss << "Create:      "
           << "lfid " << create.fid
           << "psz  " << create.numCores
           << "pc   " << hex << "0x" << create.address << dec
           << "regs "
            ;
        for (size_t i = 0; i < NUM_REG_TYPES; ++i)
            ss << create.regs[i].globals << ' '
               << create.regs[i].shareds << ' '
               << create.regs[i].locals << ' '
                ;
        break;
    case MSG_DONE: 
        ss << "Done:        "
           << "lfid " << sync.fid
            ;
        break;
    case MSG_SYNC: 
        ss << "Sync:        "
           << "lfid " << sync.fid
           << "creg " << sync.completion_reg
            ;
        break;
    case MSG_DETACH: 
        ss << "Detach:      "
           << "lfid " << detach.fid
            ;
        break;
    case MSG_BREAK: 
        ss << "Break:       "
           << "lfid " << brk.fid
            ;
        break;
    case MSG_GLOBAL: 
        ss << "Global:      "
           << "lfid " << global.fid
           << "addr " << global.addr.str()
           << "val  " << global.value.str(global.addr.type)
            ;
        break;
    default:
        assert(false); // all types should be listed here.
    }

    return ss.str();
}

}
