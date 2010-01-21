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
#ifndef _CACHEL2_TOK_H
#define _CACHEL2_TOK_H

#include "cachest.h"
#include "busst_slave_if.h"
#include "networkbelow_if.h"
#include "processortok.h"
#include "fabuffer.h"
#include "mergestorebuffer.h"
#include <queue>
using namespace std;

namespace MemSim
{

class CacheL2TOK : public CacheST, public BusST_Slave_if, public NetworkBelow_if
{
protected:
    // constant numbers
    static const unsigned int EOQ;
    static const unsigned int QueueBufferSize;

    // states
    enum STATE_INI{
        STATE_INI_PROCESSING,
        STATE_INI_RETRY,
    };

    enum STATE_PAS{
        STATE_PAS_PROCESSING,
        STATE_PAS_POSTPONE,
        STATE_PAS_RETRY,
    };

    // current request
    ST_request* m_pReqCurINI;
    ST_request* m_pReqCurPAS;

    ST_request*        m_pReqCurINIasNodeDB;   // the one needs double retry
    ST_request*        m_pReqCurINIasNode;
    ST_request*        m_pReqCurINIasSlaveX;
    queue<ST_request*> m_queReqINIasSlave;

    ST_request*        m_pReqCurPASasNodeX;
    ST_request*        m_pReqCurPASasSlaveX;
    queue<ST_request*> m_queReqPASasNode;
    queue<ST_request*> m_queReqPASasSlave;

    static const unsigned int MAX_PREFETCHBUFFERSIZE = 8;
    std::list<ST_request*> m_prefetchBuffer;

    STATE_INI m_nStateINI;
    STATE_PAS m_nStatePAS;

    // pipeline
    pipeline_t m_pPipelineINI;
    pipeline_t m_pPipelinePAS;

    //////////////////////////////////////////////////////////////////////////
    // the requests stored in the global FIFO should be unrelated to each other
    // thus the order how those requests are organized are not critical
    // request can be popped out and pushed back to tail again without affecting the program result
    // the global queue has two threshold, UPPER threshold and LOWER threshold
    // the two threshold are calculated with buffersize and UPPER and LOWER margines
    // margin should be set larger than 1 for each case.
    // when the UPPER threshold is met in the cache buffer,
    // the buffer priority flag will be set and will not process any incoming requests
    // and will lead to the stall of the cache quite soon. the cache will then only deal with buffered requests.
    // when the number of the request in the buffer reached the LOWER threshold
    // the flag of buffer priority will be reset.
    // in simple case, the UPPER and LOWER threshold can be set to a same number
    // normally, upper threshold should be larger than lower threshold (upper margin should be smaller than lower margin)
    //////////////////////////////////////////////////////////////////////////
    bool              m_bBufferPriority;
	unsigned int      m_nGlobalFIFOSize;
    list<ST_request*> m_pGlobalFIFOLIST;

    static unsigned int s_nGlobalFIFOUpperMargin;     // UPPER Margin (corresponding to size since it will compared with free buffer size)
    static const unsigned int s_nGlobalFIFOLowerMargin;     // LOWER Margin (corresponding to size since it will compared with free buffer size)

    // fully associative Invalidation matching buffer for BR requests
    // BR request are sent to L1 cache or processor for Backward Broadcast Invalidation. 
    // With BBI_LAZY method, sometimes a number of backward invalidations might happen to the same line continuously
    // this might slow down the L1/Processor or saturate the shared bus connecting L2 cache
    // With the matching buffer, the some latest BR requests can be buffered and cleared according to the situation:
    // a BR is added to the buffer when a BR is sent to L1/Processor
    // a BR is removed from the buffer when a RR is delivered to the L1/Processor

    // Fully Associative Invalidation Matching Buffer for BR requests
    // the data is not necessary in this invalidation buffer, use char or anytype to save spaces
    FABuffer<__address_t, char> m_fabInvalidation;


    // fully associative Victim line matching buffer for LR requests
    // currently this is to investigate the effectiveness of a victim cache or eviction buffer
    // evicted lines matching buffer
    FABuffer<__address_t, char> m_fabEvictedLine;

	//// semaphore for FIFO buffer retry
	//// semaphore can make increments when a reply has been received
	//// when semaphore is larger than 0, the buffer always has higher priority than the input. 
	//// thus the global FIFO will be processed. 
	//// otherwise, the FIFO is only processed when the processor is idle (the input buffer from processor is empty)

    // Merge Store Buffer Implementation
    // 0. pending line can lock the line itself itself by further access (any further accesses or specific access -- decided by llock TBD)
    // 1. read on write pending lines (the corresponding merged line should not be locked) with tokens, even locked tokens, can be performed on the line directly and return.
    // 2. read on read pending line (the corresponding merged line should not be locked )with tokens, even ghost tokens, can be perfomed on the line directly and return.
    // 3. write on write pending lines with priority token can be performed directly according to the following rules
    //    a. it can be performed on the merged store line but no return, if the AT request was from a different family.
    //    b. if the AT request was from the same family, the request can be performed directly on the line and return immediately.  [N/A yet]
    //    *. if no knowledge about the family, then the request should be performed on the MSB without return.
    // 4. write on read pending line with priority token can be performed on the line directly and no immediate return,
    //    but the merged AT/AD needs to be send out and also change the state to writepending immediately 
    //    (additional line state needs to remember the outgoing AT request) [N/A yet]
    // 5. write on writepending lines without priority tokens may write to merge store buffer if availablei and no immediate return.
    //    a. write to unlocked merged lines can proceed directly with no immediate return.
    //    b. write to locked merged lines will be blocked (or proceeded according to previous request initiators on the merge buffer TBD)
    // 6. read on the writepending lines without priority tokens are more delicate, since read always needs to read the whole line
    //    a. if a merged buffer is not locked, it can proceeed, otherwise it will be blocked to suspend in the normal queue
    //    b. read might need to combine both line and merged store buffer, as long as the data are available, it can read and return;
    //       merged store buffer has the priority, if both line and merged buffer has the data, line data is ignored
    //    c. when a read fails, the buffer slot/line will be set as locked (or a bitmapping for the read initiator is updated to improve the performance TBD) 
    //       and preventing others (illegal ones) from access the data
    // 7. write on read pending line without priority token will have to write to merge buffer (or maybe just suspend, since it might waste too much MSB TBD) [N/A yet]
    //    a. write can proceed with no immediate reply, 
    //    b. update on the merged request is of course automatic
    // 8. whenever the writepending line gets the priority token, the line will directly merge with the corresponding merge store buffer line
    //    the merged request for the merge store line will be eliminated, 
    //    and all the following write requests suspended on the merged line will be directly returend without processing.
    //    this can happen also after the AD/AT request returns
    // 9. whenever read pending line get priority token, (T cannot possibly get priority token, only R can), merge write buffer will be directly merged with the line 
    //    and change the state to write pending state, merged request has to be sent out to invalidate others.    [N/A yet]
    //
    // *  any request suspended on the merge store line will be queued and a merged request will gather all the data in those request
    // *  any unavailablility of the merge store buffer will cause the incoming request to suspend on normal queues
    // *  merge buffer slot/line are locked or not is decided by the llock variable
    // *  all request without immediate reply to the processor, will have to be queued in the merged line queue. 
    //    the merged line queue will be served before any other requests suspended on the line or even in the pipeline
    // *  change in handling AD/RS/SR return on write pending state

    // Merge Store buffer module provide the merge capability on stores on the pending lines
    MergeStoreBuffer m_msbModule;

	// Outstanding request structure
	// later will be replaced by a pending request cache
	// currently a vector is used instead
	// the map is sorted with aligned addresses in the cachelines
	map<__address_t, ST_request*>	m_mapPendingRequests;

    // Injection policy
    INJECTION_POLICY m_nInjectionPolicy;
    
public:
	SC_HAS_PROCESS(CacheL2TOK);
	
    CacheL2TOK(sc_module_name nm, sc_clock& clock,
        unsigned int nset, unsigned int nassoc, unsigned int nlinesize,
        INJECTION_POLICY nIP = IP_NONE,
        UINT latency = 5,
		unsigned int nGlobalFIFOSize = 0x100)
      : CacheST(nm, nset, nassoc, nlinesize),
        m_nStateINI(STATE_INI_PROCESSING),
        m_nStatePAS(STATE_PAS_PROCESSING),
        m_pPipelineINI(latency-1),
        m_pPipelinePAS(latency),
        m_fabInvalidation(0x20),
        m_fabEvictedLine (0x40),
        m_msbModule(3),
        m_nInjectionPolicy(nIP)
	{
        assert(latency > 1);

        SC_METHOD(BehaviorIni);
        sensitive << clock.posedge_event();
        dont_initialize();

        SC_METHOD(BehaviorNet);
        sensitive << clock.posedge_event();
        dont_initialize();

        // rewrite upper margin
        s_nGlobalFIFOUpperMargin = latency + 2;

		// save the size of Global Queue 
		m_nGlobalFIFOSize = nGlobalFIFOSize;
        assert(nGlobalFIFOSize>=s_nGlobalFIFOLowerMargin);
        assert(nGlobalFIFOSize>s_nGlobalFIFOUpperMargin);
        assert(s_nGlobalFIFOUpperMargin<=s_nGlobalFIFOLowerMargin);

        // initialize the global queue
        m_bBufferPriority = false;
	}
	
	virtual void BehaviorIni();
	virtual void BehaviorNet();

protected:
    // transactions handler
    // initiative
    virtual void ProcessInitiative();

    virtual bool SendAsNodeINI(ST_request* req);
    virtual bool SendAsSlaveINI(ST_request* req);

    virtual void SendFromINI();

    void PrefetchINI();
    ST_request* FetchRequestINI();
    ST_request* FetchRequestINIFromQueue();

    // cleansing pipeline requests, if any of them are seeking the same line 
    virtual void CleansingPipelineINI(ST_request* req);
    // cleansing pipeline and insert request into queue
    virtual void CleansingAndInsert(ST_request* req);       // ONLY FOR INI

    // passive
    virtual void ProcessPassive();

    virtual bool SendAsNodePAS(ST_request* req);
    virtual bool SendAsSlavePAS(ST_request* req);
    virtual void SendFromPAS();
    //virtual void SetAvailablePAS();
    virtual ST_request* FetchRequestPAS();
    virtual void FinishCyclePAS();
    // skip this cycle without shifting the pipeline
    virtual void Postpone();

    //////////////////////////////////////////////////////////////////////////
    // pure virtual handler
    // initiative request handlers
    virtual void OnLocalRead(ST_request*) = 0;
    virtual void OnLocalWrite(ST_request*) = 0;
    virtual void OnInvalidateRet(ST_request*) = 0;

    // facilitating functions
private:
    void Modify2AcquireTokenRequest(ST_request*, unsigned int);
    // transfer ntoken tokens from line to req, transfer tokens, transfer priority to the req if line has any.
    ST_request* NewDisseminateTokenRequest(ST_request*, cache_line_t*, unsigned int, bool bpriority);
public:
    void Modify2AcquireTokenRequestRead(ST_request*);
    void Modify2AcquireTokenRequestWrite(ST_request*, bool reqdate);
    void Modify2AcquireTokenRequestWrite(ST_request*, cache_line_t*, bool reqdate);
    ST_request* NewDisseminateTokenRequest(ST_request*, cache_line_t*);
    void PostDisseminateTokenRequest(cache_line_t*, ST_request*);
    void AcquireTokenFromLine(ST_request*, cache_line_t*, unsigned int);


    // passive request handlers
    virtual void OnAcquireTokenRem(ST_request*) = 0;
    virtual void OnAcquireTokenRet(ST_request*) = 0;
    virtual void OnAcquireTokenDataRem(ST_request*) = 0;
    virtual void OnAcquireTokenDataRet(ST_request*) = 0;
    virtual void OnDisseminateTokenData(ST_request*) = 0;
    virtual void OnDirNotification(ST_request* req) = 0;

    cache_line_t* LocateLineEx(__address_t);
    cache_line_t* GetEmptyLine(__address_t);

public:
    // cacheline, request update method
    virtual void UpdateCacheLine(cache_line_t* line, ST_request* req, CACHE_LINE_STATE state, unsigned int token, bool pending, bool invalidated, bool priority, bool tlock, LINE_UPDATE_METHOD lum, bool bupdatetime = true);
    virtual void IVUpdateCacheLine(cache_line_t* line, ST_request* req, CACHE_LINE_STATE state, unsigned int token, bool pending, bool invalidated, bool priority, bool tlock, LINE_UPDATE_METHOD lum, bool bupdatetime = true);
    virtual void UpdateRequest(ST_request* req, cache_line_t* line, MemoryState::REQUEST requesttype, __address_t address, bool bdataavailable=false, bool bpriority=false, bool btransient=false, unsigned int ntokenacquired=0xffff, REQUEST_UPDATE_METHOD rum = RUM_ALL);

    virtual char* GenerateMaskFromRequest(ST_request* req){return NULL;};
    virtual bool CheckLineValidness(cache_line_t* line, bool bwholeline = true, char* pmask = NULL);   // check the details into the bit-mask rather states
    //virtual void UpdateRequestPartial();

    //////////////////////////////////////////////////////////////////////////
    // Global Queue
    // 1. when loading a request from the queue to process, always load from the top
    // 2. when loading a request from the input buffer, always check whether there are requests seeking the same line, 
    //    if yes, put the request in the end of the global queue and skip the cycle by returning NULL in request fetch
    // 3. when a request has to be queued again during processing, all the requests seeking the same line in the pipeline
    //    has to be reversely pushed back to the global queue   // JXXX this might be optimized by distinguishing read and write
    // 4. the previously queued requests have to be reversely pushed back (push at front) to the queue
    // 5. the previously non-queued requests should be pushed from the back 
    // JXXX potential optimization, queued request probably can bypass 

    // Number Free
    unsigned int GlobalFIFONumberFree()
    {
        assert(m_pGlobalFIFOLIST.size() < m_nGlobalFIFOSize);
        return m_nGlobalFIFOSize - m_pGlobalFIFOLIST.size();
    }

    unsigned int GlobalFIFONumberAvailable()
    {
        assert(m_pGlobalFIFOLIST.size() < m_nGlobalFIFOSize);

        return m_pGlobalFIFOLIST.size();
    }

    bool GlobalFIFOEmpty() {
        assert(m_pGlobalFIFOLIST.size() < m_nGlobalFIFOSize);
	return m_pGlobalFIFOLIST.empty();
    }

    // ONLY USED TO PUSH PREVIOUSLY QUEUED REQUESTS
    bool GlobalFIFOReversePush(ST_request* req)
    {
        assert(m_pGlobalFIFOLIST.size() < m_nGlobalFIFOSize);
        assert(req->bqueued == true);

        if (GlobalFIFONumberFree() == 0)
            return false;

        m_pGlobalFIFOLIST.push_front(req);
        
        return true;
    }


    // Check Buffer Threshold
    void CheckThreshold4GlobalFIFO()
    {
        assert(m_pGlobalFIFOLIST.size() < m_nGlobalFIFOSize);

        if (GlobalFIFONumberFree() <= s_nGlobalFIFOUpperMargin)
        {
            m_bBufferPriority = true;
        }
        else if (GlobalFIFONumberFree() >= s_nGlobalFIFOLowerMargin)
        {
            m_bBufferPriority = false;
        }
    }

    // USED TO INSERT BOTH PREVIOUSLY QUEUED AND NON-QUEUED REQUESTS
	// operation insert about global FIFO
	// if the FIFO is full return false;
	// otherwise return true;
	bool InsertRequest2GlobalFIFO(ST_request* req)
	{
        assert(m_pGlobalFIFOLIST.size() < m_nGlobalFIFOSize);

        if (GlobalFIFONumberFree() == 0)
            return false;

        if (req->bqueued)
            return GlobalFIFOReversePush(req);
        
        // the request will be a queued request
        req->bqueued = true;
        m_pGlobalFIFOLIST.push_back(req);

		return true;
	}

    // $$$ optimization $$$
    // according to location consistency, the request can be reversely pushed deeper than just to the top
    // the request from the same location can be sorted to the bottom of the queue
    // the reversely pushed request will be packed with other requests for the same line sink to the bottom of the global queue
    // the sorting method should only be used after a cleansing (including reverse push is happend)
    void SortGlobalFIFOAfterReversePush(ST_request* req)
    {
        assert(m_pGlobalFIFOLIST.size() < m_nGlobalFIFOSize);

        __address_t addr = req->getlineaddress();
        list<ST_request*>::iterator iter;
        vector<ST_request*> vecreq, vecother;

        for (iter=m_pGlobalFIFOLIST.begin();iter!=m_pGlobalFIFOLIST.end();iter++)
        {
            ST_request* reqtemp = *iter;

            if (reqtemp->getlineaddress() == addr)
                vecreq.push_back(reqtemp);
            else
                vecother.push_back(reqtemp);
        }

        // clean list
        m_pGlobalFIFOLIST.clear();

        // push back other requests
        for (unsigned int i=0;i<vecother.size();i++)
            m_pGlobalFIFOLIST.push_back(vecother[i]);

        // push back all the requests matchs the current address
        for (unsigned int i=0;i<vecreq.size();i++)
            m_pGlobalFIFOLIST.push_back(vecreq[i]);
    }
    
    // check queued request, from the queue register or the queue
    ST_request* PopQueuedRequest()      // the same as NBRead
    {
        assert(m_pGlobalFIFOLIST.size() < m_nGlobalFIFOSize);
        assert(!m_pGlobalFIFOLIST.empty());
        ST_request* ret = m_pGlobalFIFOLIST.front();
        m_pGlobalFIFOLIST.pop_front();
        return ret;
    }

    // check whether the request can be found in the queue
    bool DoesFIFOSeekSameLine(ST_request* req) const
    {
        assert(m_pGlobalFIFOLIST.size() < m_nGlobalFIFOSize);
        for (list<ST_request*>::const_iterator iter = m_pGlobalFIFOLIST.begin();iter != m_pGlobalFIFOLIST.end(); ++iter)
        {
            if ((*iter)->getlineaddress() == req->getlineaddress())
            {
                return true;
            }
        }
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // if bProcessing is set then m_pReqCurINI will be set
    // otherwise m_pReqCurINI will remain the same, and simply shift the pipeline
    void AdvancePipelineINI(bool bProcessing = false)
    {
        ST_request* req_incoming = FetchRequestINI();   // NULL or something

        if (bProcessing)    // get request from pipeline
            m_pReqCurINI = m_pPipelineINI.shift(req_incoming);
        else    // simply shift
            m_pPipelineINI.shift(req_incoming);
    }

	// insert a request to the outstanding request structure
	bool InsertOutstandingRequest(ST_request* req);

	// remove a request from the outstanding request structure
	ST_request* RemoveOutstandingRequest(__address_t addr);

    // create new eviction or write back request: 
    // bINI: is creating EV/WB from INI interface
    ST_request* NewEVorWBRequest(ST_request* req, cache_line_t* pline, bool beviction, bool bINI=true);

    // $$$ optimization $$$
    // victim line implementation and optimization

    // THIS METHOD SHOULD BE PERFORMED BEFORE INVALIDATION CARRIED OUT !!!
    // supplement line invalidation, this function does not perform real invalidation action
    // it only perform additional actions in addition to the invalidation (probably including optimizations)
    // bremote represent whether the request is a remote request invalidates a cacheline at normal state (RE_rem, IV_rem, ER_rem), 
    // or reply request (RS_ret@RPI, RE_ret@WPE, SR_ret@RPI, ER_ret@WPE, DE@WPE)
    void LineInvalidationExtra(ST_request* req, bool bremote)
    {
        if (bremote)    // remote
        {
            assert(m_pReqCurPASasSlaveX == NULL);
            ST_request *newreq = new ST_request(req);
            ADD_INITIATOR(newreq, (void*)NULL);
            newreq->type = REQUEST_INVALIDATE_BR;
            InsertSlaveReturnRequest(false, newreq);
        }
        else
        {
            // add backward broadcast flag into reply request   JXXX ???
            if (req->type == REQUEST_READ_REPLY)
                req->bprocessed = true;
            // send procedure will deal with the processed flag for WR
        }

        // $$$ optimization victime buffer $$$
        m_fabEvictedLine.RemoveBufferItem(req->getlineaddress());
    }

    // $$$ optimization for victim buffer $$$
    // Load from the victim buffer
    bool OnLocalReadVictimBuffer(ST_request* req);

    // insert Slave return request for either INIasSlave or PASasSlave request
    void InsertSlaveReturnRequest(bool ini, ST_request *req);

    // insert network request for PAS interface
    void InsertPASNodeRequest(ST_request* req);

    // get a slave request if none on the spot
    void AutoFillSlaveReturnRequest(bool ini)
    {
        if (ini) 
        {
            if (m_pReqCurINIasSlaveX == NULL)
            {
	            if (!m_queReqINIasSlave.empty())
                {
                    m_pReqCurINIasSlaveX = m_queReqINIasSlave.front();
                    m_queReqINIasSlave.pop();
                }
                else
                    m_pReqCurINIasSlaveX = NULL;
            }
        }
        else
        {
            if (m_pReqCurPASasSlaveX == NULL)
            {
                if (!m_queReqPASasSlave.empty())
                {
                    m_pReqCurPASasSlaveX = m_queReqPASasSlave.front();
                    m_queReqPASasSlave.pop();
                }
                else
                    m_pReqCurPASasSlaveX = NULL;
            }
        }

    }

    // get a pas node request if none on the spot
    void AutoFillPASNodeRequest();
};

}

#endif

