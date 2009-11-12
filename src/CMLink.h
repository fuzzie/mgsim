/*
mgsim: Microgrid Simulator
Copyright (C) 2006,2007,2008,2009  The Microgrid Project.

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
#ifndef CACHELINK_H
#define CACHELINE_H

#include <queue>
#include <set>
#include "Memory.h"
#include "kernel.h"
//#include "VirtualMemory.h"
#include "SimpleMemory.h"

#include "coma/simlink/linkmgs.h"

#include "coma/simlink/memorydatacontainer.h"

#include "Processor.h"

#include <fstream>

namespace Simulator
{

    class CMLink : public SimpleMemory
    {
    public:
        CMLink(Object* parent, Kernel& kernel, const std::string& name, const Config& config, LinkMGS* linkmgs, MemoryDataContainer* mdc=NULL)
            :SimpleMemory(parent, kernel, name, config), m_linkmgs(linkmgs)
#ifndef MEM_CACHE_LEVEL_ONE_SNOOP
                , m_pimcallback(NULL)
#endif

        {
            m_pProcessor = NULL;
            m_nTotalReq = 0;

            if (s_pLinks == NULL)
            {
                s_pLinks = new std::vector<CMLink*>();
            }

            s_pLinks->push_back(this);

#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
            // add this link to linkmgs
            m_linkmgs->SetCMLinkPTR((void*)this);
#endif
            // ignore memory data container pointer
            if (mdc == NULL)
                return;

            // save or update the memory container pointer
            if (s_pMemoryDataContainer == NULL)
            {
                s_pMemoryDataContainer = mdc;
            }
            else if (s_pMemoryDataContainer != mdc)
            {
                // inconsistent memory container
                throw runtime_error("Inconsistent memory container");
            }
        }

        ~CMLink(){
        }

        static std::vector<CMLink*> *s_pLinks;

        void Reserve(MemAddr address, MemSize size, int perm);
        void Unreserve(MemAddr address);

        bool Read (IMemoryCallback& callback, MemAddr address, MemSize size, MemTag tag);
        bool Write(IMemoryCallback& callback, MemAddr address, const void* data, MemSize size, MemTag tag);


        // IMemory
//        Result Read (IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);
//        Result Write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);
		bool CheckPermissions(MemAddr address, MemSize size, int access) const;

        // Component
        Result OnCycle(unsigned int stateIndex);
//        Result OnCycleWritePhase(unsigned int stateIndex);

        // IMemoryAdmin
        bool Allocate(MemSize size, int perm, MemAddr& address);
        void Read (MemAddr address, void* data, MemSize size);
        void Write(MemAddr address, const void* data, MemSize size);



//        virtual const std::queue<Request>& GetRequests() {	// MESMX
//			// duplicate the request to queue
//			std::set<Request*>::iterator iter;
//			while(!m_requests.empty())
//			{
//				m_requests.front();
//				//m_requests.pop();
//			}
//
//			for (iter = m_setrequests.begin();iter != m_setrequests.end();iter++)
//				m_requests.push(**iter);
//
//            return m_requests;
//        }

        void SetProcessor(Processor* proc){m_pProcessor = proc;}
        Processor* GetProcessor(){return m_pProcessor;}

#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
        bool OnMemorySnoopRead(unsigned __int64 address, void *data, unsigned int size, bool dcache);
        bool OnMemorySnoopWrite(unsigned __int64 address, void *data, unsigned int size);
        bool OnMemorySnoopIB(unsigned __int64 address, void* data, unsigned int size);
#endif

    private:
        std::set<Request*>            m_setrequests;     // MESMX 
        unsigned int                  m_nTotalReq;

#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
        std::queue<Request>           m_snoopreads;   // MESMX
#endif

        LinkMGS*                      m_linkmgs;     // MESMX

#ifndef MEM_CACHE_LEVEL_ONE_SNOOP
        IMemoryCallback*              m_pimcallback;
#endif

        Processor*                    m_pProcessor;

        static MemoryDataContainer *s_pMemoryDataContainer;
    };

}
#endif
