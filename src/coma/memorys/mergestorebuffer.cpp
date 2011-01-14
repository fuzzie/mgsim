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
#include "mergestorebuffer.h"

namespace MemSim
{

// write to the buffer
bool MergeStoreBuffer::WriteBuffer(ST_request* req)
{
    __address_t address = req->getlineaddress();
    if (m_fabLines.FindBufferItem(address) != NULL)
    {
        // update the current merged line
        if (IsSlotLocked(address, req))
        {

            return false;
        }

        // proceed update
        UpdateSlot(req);
    }
    else
    {
        // allocate a new line for the incoming request
        int retfree = IsFreeSlotAvailable();
        if (retfree != -1)
        {
            // proceed allocating
            int index;
            AllocateSlot(address, index);

            // Update Slot
            UpdateSlot(req);
        }
        else
        {

            return false;
        }
    }

    // Update Request to return type
    req->type = MemoryState::REQUEST_WRITE_REPLY;

    //UpdateMergedRequest(address, NULL, NULL);
    UpdateMergedRequest(address, NULL, req);

    return true;
}

// load from the buffer
//virtual bool LoadBuffer(__address_t address, char* data, ST_request* req)
bool MergeStoreBuffer::LoadBuffer(ST_request* req, cache_line_t *linecache)
{
    //__address_t address = req->getreqaddress();
    __address_t addrline = req->getlineaddress();

    assert(req->nsize == g_nCacheLineSize);

    cache_line_t* line = m_fabLines.FindBufferItem(addrline);
    if (line == NULL)
    {
        return false;
    }

    //unsigned int maskoffset = req->offset/CACHE_REQUEST_ALIGNMENT;
    for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH;i++)
    {
        unsigned int maskhigh = i/8;
        unsigned int masklow = i%8;
        char masknow = 1 << masklow;
        if (line->bitmask[maskhigh] == masknow)
        {
            memcpy(&req->data[i*CACHE_REQUEST_ALIGNMENT], &line->data[i*CACHE_REQUEST_ALIGNMENT], CACHE_REQUEST_ALIGNMENT);
        }
        else if (linecache->bitmask[maskhigh] == masknow)
        {
            memcpy(&req->data[i*CACHE_REQUEST_ALIGNMENT], &linecache->data[i*CACHE_REQUEST_ALIGNMENT], CACHE_REQUEST_ALIGNMENT);
        }
        else
        {
            // lock the line 
            line->llock = true;
            return false;
        }
    }

    req->type = MemoryState::REQUEST_READ_REPLY;
    req->dataavailable = true;

    return true;
}

// return -1 : failed, nothing is free
// return  n : succeed, n == index
int MergeStoreBuffer::IsFreeSlotAvailable()
{
    return m_fabLines.IsEmptySlotAvailable();
}

bool MergeStoreBuffer::IsAddressPresent(__address_t address)
{
    address = (address / g_nCacheLineSize) * g_nCacheLineSize;
    return m_fabLines.FindBufferItem(address) != NULL;
}

int MergeStoreBuffer::AddressSlotPosition(__address_t address)
{
    int index = -1;
    address = (address / g_nCacheLineSize) * g_nCacheLineSize;
    m_fabLines.FindBufferItem(address, index);
    return index;
}

bool MergeStoreBuffer::IsSlotLocked(__address_t address, ST_request* req)
{
    address = (address / g_nCacheLineSize) * g_nCacheLineSize;
    cache_line_t* line = m_fabLines.FindBufferItem(address);
    return line != NULL && line->llock;
}

void MergeStoreBuffer::UpdateMergedRequest(__address_t address, cache_line_t* linecache, ST_request* reqpas)
{
    assert(reqpas != NULL);

    int index = -1;
    __address_t addrline = (address / g_nCacheLineSize) * g_nCacheLineSize;
    cache_line_t* line = m_fabLines.FindBufferItem(addrline, index);
    assert(line != NULL);

    for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH;i++)
    {
        unsigned int maskhigh = i/8;
        unsigned int masklow = i % 8;
        char testchar = 1 << masklow;

        if (reqpas->bitmask[maskhigh] & testchar)
        {
            m_ppMergedRequest[index]->bitmask[maskhigh] |= testchar;
            line->bitmask[maskhigh] |= testchar;
            memcpy(&m_ppMergedRequest[index]->data[i*CACHE_REQUEST_ALIGNMENT], &reqpas->data[i*CACHE_REQUEST_ALIGNMENT], CACHE_REQUEST_ALIGNMENT);
            memcpy(&line->data[i*CACHE_REQUEST_ALIGNMENT], &reqpas->data[i*CACHE_REQUEST_ALIGNMENT], CACHE_REQUEST_ALIGNMENT);
        }
    }

    m_ppMergedRequest[index]->type = MemoryState::REQUEST_ACQUIRE_TOKEN_DATA;           // NEED TO CHANGE, JONY XXXXXXX
    m_ppMergedRequest[index]->tokenacquired = 0;
    m_ppMergedRequest[index]->tokenrequested = CacheState::GetTotalTokenNum();
    m_ppMergedRequest[index]->addresspre = address / g_nCacheLineSize;
    m_ppMergedRequest[index]->offset = 0;
    m_ppMergedRequest[index]->nsize = 4;            /// NEED TO CHANGE  JONY XXXXXXX
    m_ppMergedRequest[index]->bmerged = true;
}

void MergeStoreBuffer::DuplicateRequestQueue(__address_t address, ST_request* reqrev)
{
    int index = AddressSlotPosition(address);
    // duplicate
    vector<ST_request*>* pvec = new vector<ST_request*>();
    for (unsigned int i=0;i<m_pvecRequestQueue[index]->size();i++)
    {
        pvec->push_back((*m_pvecRequestQueue[index])[i]);
    }

    reqrev->ref = (unsigned long*)pvec;
}

ST_request* MergeStoreBuffer::GetMergedRequest(__address_t address)
{
    int index = AddressSlotPosition(address);
    if (index == -1)
        return NULL;

    return m_ppMergedRequest[index];
}

vector<ST_request*>* MergeStoreBuffer::GetQueuedRequestVector(__address_t address)
{
    int index = AddressSlotPosition(address);
    if (index == -1)
        return NULL;

    return m_pvecRequestQueue[index];
}

bool MergeStoreBuffer::CleanSlot(__address_t address)
{
    int index;
    address = (address / g_nCacheLineSize) * g_nCacheLineSize;
    if (m_fabLines.FindBufferItem(address, index) != NULL)
    {
        // clean slot with data
        m_fabLines.RemoveBufferItem(address);

        // clean the merged request
        m_ppMergedRequest[index]->type = MemoryState::REQUEST_NONE;

        m_ppMergedRequest[index]->curinitiator = 0;

        for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
        {
            m_ppMergedRequest[index]->bitmask[i] = 0;
        }

        // clean the request queue
        m_pvecRequestQueue[index]->clear();

        assert(!m_fabLines.FindBufferItem(address));
        return true;
    }
    return false;
}

// return false : no allocation
// return  treu : allocated
// index always return the current/allocated index, -1 represent total failure
bool MergeStoreBuffer::AllocateSlot(__address_t address, int& index)
{
    address = (address / g_nCacheLineSize) * g_nCacheLineSize;
    int ind = AddressSlotPosition(address);

    if (ind != -1)
    {
        index = ind;
        return false;
    }

    ind = IsFreeSlotAvailable();
    index = ind;

    if (ind == -1)
        return false;

    cache_line_t* line = (cache_line_t*)malloc(sizeof(cache_line_t));

    line->llock = false;

    for (unsigned int k = 0;k<CACHE_BIT_MASK_WIDTH/8;k++)
    {
        line->bitmask[k] = 0;
        m_ppMergedRequest[index]->bitmask[k] = 0;
    }

    line->data = m_ppData[ind];

    // initialize again
    for (unsigned int i=0;i<g_nCacheLineSize;i++)
        line->data[i] = (char)0;

    m_fabLines.InsertItem2Buffer(address, line, 1);

    // verify...
    assert(AddressSlotPosition(address) == ind);

    free(line);

    m_ppMergedRequest[index]->curinitiator = 0;
    ADD_INITIATOR(m_ppMergedRequest[index], this);

    return true;
}

// req should be only write request
bool MergeStoreBuffer::UpdateSlot(ST_request* req)
{
    assert(req->type == MemoryState::REQUEST_WRITE);
    __address_t address = req->getlineaddress();

    int index =0;
    cache_line_t* line = m_fabLines.FindBufferItem(address, index);
    assert(line != NULL);

    {
        // update data and bit-mask

        assert(line->data == m_ppData[index]);


        unsigned int offset = req->offset;

        char maskchar[CACHE_BIT_MASK_WIDTH/8];
        for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
            maskchar[i] = 0;

        unsigned int maskoffset = offset/CACHE_REQUEST_ALIGNMENT;
        for (unsigned int i = 0;i<req->nsize;i+=CACHE_REQUEST_ALIGNMENT)
        {
            unsigned int currentpos = i + offset;

            memcpy(&line->data[currentpos], &req->data[currentpos], CACHE_REQUEST_ALIGNMENT);

            unsigned int maskhigh = maskoffset / 8;
            unsigned int masklow = maskoffset % 8;

            maskchar[maskhigh] |= (1 << masklow);
            maskoffset++;
        }

        for (unsigned int i=0;i<CACHE_BIT_MASK_WIDTH/8;i++)
            line->bitmask[i] |= maskchar[i];

        for (unsigned int i=0;i<g_nCacheLineSize;i+=CACHE_REQUEST_ALIGNMENT)
        {
            unsigned int maskhigh = i / (8*CACHE_REQUEST_ALIGNMENT);
            unsigned int masklow = i % (8*CACHE_REQUEST_ALIGNMENT);

            char testchar = 1 << masklow;

            if ((req->bitmask[maskhigh]&testchar) != 0)
            {
                line->bitmask[maskhigh] |= testchar;
                memcpy(&line->data[i], &req->data[i], CACHE_REQUEST_ALIGNMENT);
            }
        }

        // update request queue
        m_pvecRequestQueue[index]->push_back(req);
    }

    return true;
}

}
