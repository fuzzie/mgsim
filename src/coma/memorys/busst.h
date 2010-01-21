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
#ifndef _BUSST_H
#define _BUSST_H

#include "predef.h"

#include "busst_if.h"
#include "busst_slave_if.h"
#include "busst_master.h"

namespace MemSim
{

class BusST : public BusST_if, public sc_module
{
    enum STATE_MEM {
        STATE_MEM_AVAILABLE,
        STATE_MEM_CONGEST
    };

	BusST_Slave_if&            m_port_slave;
	sc_fifo_in<ST_request*>    m_port_fifo_slave_in;
    std::vector<BusST_Master*> m_vecBusMaster;
    std::vector<BusST_Master*> m_vecBCMasters;
	sc_fifo<ST_request*>       m_pfifoMasterIn;
    ST_request*                m_pReqCurINI;
    ST_request*                m_pReqCurMEM;
    STATE_MEM                  m_nStateMEM;
    
    void SendFeedbackToMaster(ST_request* req);
    void BroadCastFeedback(ST_request* req);
    
public:
	SC_HAS_PROCESS(BusST);

	// constructor
	BusST(sc_module_name name, sc_clock& clock, BusST_Slave_if& slave)
	  : sc_module(name),
        m_port_slave(slave),
	    m_pReqCurINI(NULL),
        m_nStateMEM(STATE_MEM_AVAILABLE)
    {
		// process declaration
		SC_METHOD(BehaviorMaster);
		sensitive << clock.negedge_event();
		dont_initialize();

		SC_METHOD(BehaviorSlave);
		sensitive << clock.posedge_event();
		dont_initialize();

        m_port_fifo_slave_in(slave.channel_fifo_slave);
	}

	// Process
	void BehaviorMaster();
	void BehaviorSlave();

	// direct BUS interface
    bool request(ST_request* req);

    void BindMaster(BusST_Master& master)
    {
        master.port_bus(*this);
        m_vecBusMaster.push_back(&master);
    }
};

}
#endif

