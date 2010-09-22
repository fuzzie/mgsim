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
#ifndef _MEM_SCHEDULE_PIPELINE_H
#define _MEM_SCHEDULE_PIPELINE_H

#include "predef.h"

namespace MemSim
{

#define MSP_LRP_NONE        -255    // LAST REQUEST POSITION: regarded as no previous requests
#define MSP_LRP_TH_CLOSE    -20     // threashold to close row

// the requests are scheduled at their return time. 
// the requests moved out of the pipeline can be returned immediately
class MemorySchedulePipeline
{
    std::vector<Message*> m_pipeline;
    unsigned int             m_nHead;            // head of the buffer
    unsigned int             m_nPointer;         // pointing to the next empty slot to the last scheduled request
    int                      m_nLastReqPosition; // last request position, 
                                                 // it is a relative position to the head of the buffer
                                                 // it can be negtive 
public:
    MemorySchedulePipeline(unsigned int size)
        : m_pipeline(size, NULL),
        m_nHead(0),
        m_nPointer(0),
        m_nLastReqPosition(0)
    {
    }

    bool ScheduleNext(Message * req, unsigned int gap, unsigned int delay)
    {
        assert(delay + 1 < m_pipeline.size());
       
        if (m_nLastReqPosition == MSP_LRP_NONE)
        {
            m_nLastReqPosition = gap;
        }
        else
        {
            // Check pipeline size
            if (m_nLastReqPosition + (int)(gap + delay) >= (int)m_pipeline.size() - 1)
            {
                return false;
            }
            m_nLastReqPosition = std::max(m_nLastReqPosition + (int)gap, 0);
        }
        
        unsigned int newpos = m_nHead + m_nLastReqPosition + delay;
        m_pipeline[newpos % m_pipeline.size()] = req;
        m_nPointer = (newpos + 1) % m_pipeline.size();
        return true;
    }

    // eventid : 0 - nothing
    //           1 - row close
    Message* AdvancePipeline(unsigned int& eventid)
    {
        Message* req = NULL;
        if (m_nPointer != m_nHead)
        {
            req = m_pipeline[m_nHead];
            m_pipeline[m_nHead] = NULL;
            m_nHead = (m_nHead + 1) % m_pipeline.size();
        }
        
        eventid = 0;
        if (m_nLastReqPosition != MSP_LRP_NONE)
        {
            m_nLastReqPosition--;
            if (m_nLastReqPosition < MSP_LRP_TH_CLOSE)
            {
                m_nLastReqPosition = MSP_LRP_NONE;
                eventid = 1;
            }
        }
   
        return req; 
    }
};


}

#endif
