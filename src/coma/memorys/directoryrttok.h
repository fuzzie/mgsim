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
#ifndef _DIRECTORYRT_TOK_H
#define _DIRECTORYRT_TOK_H

// root directory
#include "predef.h"
#include "busst_slave_if.h"
#include "busst_master.h"
#include "networkbelow_if.h"
#include "suspendedrequestqueue.h"
#include "evicteddirlinebuffer.h"

namespace MemSim
{

class DirectoryRTTOK : public sc_module, public CacheState, public NetworkBelow_if, public BusST_Master
{
    // Directory parameters
    unsigned int m_nLineSize;
	unsigned int m_nSet;
	unsigned int m_nAssociativity;
	unsigned int m_nSetBits;
	dir_set_t   *m_pSet;
	
    SuspendedRequestQueue m_srqSusReqQ;

    // current request
    ST_request* m_pPrefetchDeferredReq;
    ST_request* m_pReqCurNET;
    ST_request* m_pReqCurBUS;

    list<ST_request*> m_lstReqNET2Net;
    ST_request*       m_pReqCurNET2Net;
    ST_request*       m_pReqCurNET2Bus;
    ST_request*       m_pReqCurBUS2Net;

    // state
    enum STATE_NET {
        STATE_NET_PROCESSING,
        STATE_NET_RETRY
    };

    enum STATE_BUS {
        STATE_BUS_PROCESSING,
        STATE_BUS_RETRY_TO_NET
    };

    STATE_NET  m_nStateNET;
    STATE_BUS  m_nStateBUS;
    pipeline_t m_pPipelineNET;
    pipeline_t m_pPipelineBUS;

    // current map, line bits, set bits, split dir bits, then tabs
    unsigned int m_nRootDirCount;
    unsigned int m_nRootDirID;

    // evicted dirline buffer
    EvictedDirLineBuffer m_evictedlinebuffer;

public:
	// directory should be defined large enough to hold all the information in the hierarchy below
	SC_HAS_PROCESS(DirectoryRTTOK);
	DirectoryRTTOK(sc_module_name nm, sc_clock& clock, unsigned int nset, unsigned int nassoc, 
		       unsigned int nlinesize,
		       unsigned int nRootDirID,
		       unsigned int nRootDirCount,
		       int latency)
      : sc_module(nm), 
        m_nLineSize(nlinesize),
  	    m_nSet(nset), 
	    m_nAssociativity(nassoc), 
	    m_srqSusReqQ(0x200, 0x100),
        m_nStateNET(STATE_NET_PROCESSING),
        m_nStateBUS(STATE_BUS_PROCESSING),
        m_pPipelineNET(latency),
        m_pPipelineBUS(latency),
        m_nRootDirCount(nRootDirCount),
        m_nRootDirID(nRootDirID)
	{
        ST_request::s_nRequestAlignedSize = nlinesize;
        
		SC_METHOD(BehaviorNET);
		sensitive << clock.posedge_event();
		dont_initialize();

		// process for reply from memory system
		SC_METHOD(BehaviorBUS);
		sensitive << clock.posedge_event();
		dont_initialize();

		// allocate space for all cache lines
		InitializeDirLines();
    }

    ~DirectoryRTTOK(){
        for (unsigned int i=0;i<m_nSet;i++)
    	    free(m_pSet[i].lines);
        free(m_pSet);
    }

	void InitializeDirLines();

	void BehaviorNET();
    void BehaviorBUS();

    void ProcessRequestNET();
    void ProcessRequestBUS();

    bool SendRequestNETtoNET(ST_request*);
    bool SendRequestNETtoBUS(ST_request*);
    void SendRequestFromNET();

    void SendRequestBUStoNet();

	// transactions
    void OnNETAcquireToken(ST_request *);
    void OnNETAcquireTokenData(ST_request *);
    void OnNETDisseminateTokenData(ST_request *);

    //////////////////////////////////////////////////////////////////////////
    // cache interfaces
	dir_line_t* LocateLine(__address_t);
	dir_line_t* GetReplacementLine(__address_t);
	unsigned int DirIndex(__address_t);
	uint64 DirTag(__address_t);

    // fix the directory line states
    void FixDirLine(dir_line_t* line);

    //////////////////////////////////////////////////////////////////////////
    // queue handling

    // CHKS potential optimization, queued request probably can bypass 

    // fetch request from input buffer
    // [JNEW] the fetched request from the input buffer will get into to the pipeline
    ST_request* FetchRequestNet();

    // prefetch deferred request head,
    // if the request can be passed directly it will be popped
    // otherwise, no pop action will be taken 
    // [JNEw] the fetched request from the deferred buffer should be stored in m_pPrefetchDeferredReq from caller
    // [JNEW] and the deferred request will be processed through the bus or network
    ST_request* PrefetchDeferredRequest();

    //// pop out the deferred request head
    //// * popdeferredrequest will set the AUXSTATE to NONE, which could be incorrect, since sometimes it should be loading
    //// ** the auxstate should always be modified after calling pop
    // ST_request* PopDeferredRequest(dir_line_t*&line);

    // pop out the deferred request head
    // * popdeferredrequest will set the AUXSTATE to NONE, which could be incorrect, since sometimes it should be loading
    // ** the auxstate should always be modified after calling pop
    // *** when bRem == true, the line will be removed from the line queue, otherwise, keep the line there. 
    // ST_request* PopDeferredRequest(bool bRem = false);
    ST_request* PopDeferredRequest();

    //////////////////////////////////////////////////////////////////////////
    // 1. network request always has priority   // JXXX adjusted a bit, but almost always 
    // 2. when processing a previously non-queued request, find the line has queue associated, 
    //    it will be added to the end of queue, it will be added to the end of the queue, no matter what
    // 3. when processing a previously non-queued request, find the line has no queue, then do whatever correct
    // 4. when processing a previously queued request, find the line has queue associated,
    //    a cleansing method will be used, and all the previously queued request will be reversely pushed into the queue reversely
    //    all the previously non-queued request will be pushed into the queue
    // 5. when processing a previously queued request, find the line has no queue associated, do whatever appropriate
    //////////////////////////////////////////////////////////////////////////
};

}

#endif

