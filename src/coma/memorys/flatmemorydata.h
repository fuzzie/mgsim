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
#ifndef _FLAT_MEMORY_DATA_H
#define _FLAT_MEMORY_DATA_H

#include "predef.h"
#include <vector>
#include "../simlink/memorydatacontainer.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class FlatMemoryDataSim : public MemoryDataContainer, public SimObj
{
private:
    typedef struct _item_t
    {
        uint64 address;
        char data[4];
    } item_t;
    
    vector<item_t*> m_vecItemPtr;

public:
    // address will actually starts from aligned address, as XXXXXX00 
    bool Update(unsigned int64_t startingAddress, unsigned int size, char* data, unsigned char permission = 0)
    {
        startingAddress  = (startingAddress >> 2) << 2;
        if (size < 4) 
        {
            cerr << ERR_HEAD_OUTPUT << "request size less than 4." << endl;
            return false;
        }

        if (size%4 != 0)
        {
            cerr << ERR_HEAD_OUTPUT << "request size modulo of 4 has to be 0." << endl;
            return false;
        }

        for (unsigned int s=0;s<size;s+=4)
        {
            bool bMatch = false;
            vector<item_t *>::iterator pItemIter;
            for (pItemIter=m_vecItemPtr.begin(); pItemIter != m_vecItemPtr.end(); pItemIter++)
            {
                item_t *pItem = *pItemIter;

                if ((pItem->address|0x3) == ((startingAddress+s)|0x3))
                {
                    bMatch = true;
                    // update data
                    memcpy(pItem->data, &data[s], 4);
                    break;
                }
            }

            if (!bMatch)
            {
                // allocate new item space
                item_t *pItem = new item_t;
                pItem->address = ((startingAddress + s) >> 2) << 2;  // get rid of the two least significant bits
                memcpy(pItem->data, &data[s], 4);
                m_vecItemPtr.push_back(pItem);
            }
        }

        return true;
    }

    // FetchMemData will read data from certain address, if data not available, it will be generated
    // FetchMemData will automatically generate unavailable data
    // address will actually starts from aligned address, as XXXXXX00
    bool Fetch(unsigned int64_t startingAddress, unsigned int size, char* data)
    {
        startingAddress  = (startingAddress >> 2) << 2;
        if (size < 4) 
        {
            cerr << ERR_HEAD_OUTPUT << "request size less than 4." << endl;
            return false;
        }

        if (size%4 != 0)
        {
            cerr << ERR_HEAD_OUTPUT << "request size modulo of 4 has to be 0." << endl;
            return false;
        }

        for (unsigned int s=0;s<size;s+=4)
        {
            bool bMatch = false;
            vector<item_t *>::iterator pItemIter;
            for (pItemIter=m_vecItemPtr.begin(); pItemIter != m_vecItemPtr.end(); pItemIter++)
            {
                item_t *pItem = *pItemIter;

                // if address found
                if ((pItem->address|0x3) == ((startingAddress+s)|0x3))
                {
                    bMatch = true;
                    // read data
                    memcpy(&data[s], pItem->data, 4);
                    break;
                }
            }

            // if address not found, allocate a space, and generate the data
            if (!bMatch)
            {
                // allocate new item space
                item_t *pItem = new item_t;
                pItem->address = ((startingAddress + s) >> 2) << 2;  // get rid of the two least significant bits
                // generate data
                generate_random_data(pItem->data, sizeof(int));       // 32 bit alert ?

                // read data
                memcpy(&data[s], pItem->data, 4);
                m_vecItemPtr.push_back(pItem);
            }
        }
        return true;
    }

    // verify the validness of a piece of data
    // address will actually starts from aligned address, as XXXXXX00 
    bool Verify(unsigned int64_t startingAddress, unsigned int size, char* data)
    {
        bool ret = true;
        startingAddress = (startingAddress >> 2) << 2;
        if (size < 4) 
        {
            cerr << ERR_HEAD_OUTPUT << "request size less than 4." << endl;
            return false;
        }

        if (size%4 != 0)
        {
            cerr << ERR_HEAD_OUTPUT << "request size modulo of 4 has to be 0." << endl;
            return false;
        }

        for (unsigned int s=0;s<size;s+=4)
        {
            bool bMatch = false;
            vector<item_t *>::iterator pItemIter;
            for (pItemIter=m_vecItemPtr.begin(); pItemIter != m_vecItemPtr.end(); pItemIter++)
            {
                item_t *pItem = *pItemIter;

                if ((pItem->address|0x3) == ((startingAddress+s)|0x3))
                {
                    bMatch = true;
                    // compare data
                    if ( ((uint32_t)*((int*)pItem->data) ^ (uint32_t)*((int*)(&data[s])) ) != 0)
                    {
                        // data not match
                        cerr << ERR_HEAD_OUTPUT << "data@" << FMT_ADDR(startingAddress+s) << "("<< FMT_WID_DTA((int)(data[s] & 0xff), 2) << " " << FMT_WID_DTA((int)(data[s+1] & 0xff), 2) << " " 
                            << FMT_WID_DTA((int)(data[s+2] & 0xff), 2) << " " << FMT_WID_DTA((int)(data[s+3] & 0xff), 2) 
                            << ") should be " << "("<< FMT_WID_DTA((int)(pItem->data[0] & 0xff), 2) << " " << FMT_WID_DTA((int)(pItem->data[1] & 0xff), 2) << " " 
                            << FMT_WID_DTA((int)(pItem->data[2] & 0xff), 2) << " " << FMT_WID_DTA((int)(pItem->data[3] & 0xff), 2) << ")" << endl;
                        //cerr << ERR_HEAD_OUTPUT << "This might be an error or a data racing condition" << endl;
                        ret = false;
                    }
                    
                    break;
                }
            }

            if (!bMatch)
            {
                // address not found
                uint64 addr = startingAddress+s;
                cerr << ERR_HEAD_OUTPUT << "data@" << FMT_ADDR(addr) << " not found" << endl;
                ret = false;
            }
        }       

        return ret;
    }

    bool PrintData()
    {
        LOG_VERBOSE_BEGIN(VERBOSE_MOST)
            clog << sc_time_stamp() << "# " << __FUNCTION__ << ": " << endl;
            vector<item_t *>::iterator pItemIter;
            for (pItemIter=m_vecItemPtr.begin(); pItemIter != m_vecItemPtr.end(); pItemIter++)
            {
                item_t *pItem = *pItemIter;

                clog << "\t\tdata@" << FMT_ADDR(pItem->address) << "("<< FMT_WID_DTA((int)(pItem->data[0] & 0xff), 2) << " " << FMT_WID_DTA((int)(pItem->data[1] & 0xff), 2) << " " 
                    << FMT_WID_DTA((int)(pItem->data[2] & 0xff), 2) << " " << FMT_WID_DTA((int)(pItem->data[3] & 0xff), 2) << ")" << endl;
            }
            clog << endl;
        LOG_VERBOSE_END

        return true;
    }

};

//////////////////////////////
//} memory simulator namespace
}

#endif

