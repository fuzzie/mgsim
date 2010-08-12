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
#include "topologys.h"
#include "../../config.h"
using namespace MemSim;

static const int MAX_NAME_SIZE = 0x30;

extern Config* g_Config;

TopologyS::TopologyS()
{
    // parsed config
    LinkConfig &cf = LinkMGS::s_oLinkConfig;
    const Config& config = *g_Config;

    g_nCacheLineSize       = cf.m_nLineSize;
    g_pMemoryDataContainer = this;

    assert(cf.m_nProcessorsPerCache > 0);
    assert(cf.m_nCachesPerDirectory > 0);
    
    // temporary names
    char tempname[MAX_NAME_SIZE];

    m_pclk     = new sc_clock("clk",     cf.m_nCycleTimeCore, SC_PS);
    m_pclkroot = new sc_clock("clkroot", cf.m_nCycleTimeCore, SC_PS);
    m_pclkmem  = new sc_clock("clkmem",  cf.m_nCycleTimeMemory, SC_PS);

    // Create L2 caches
    m_ppCacheL2.resize((cf.m_nProcs + cf.m_nProcessorsPerCache - 1) / cf.m_nProcessorsPerCache);
    m_ppBus.resize(m_ppCacheL2.size());
    for (unsigned int i = 0; i < m_ppCacheL2.size(); i++)
    {
        sprintf(tempname, "cache%d", i);
        m_ppCacheL2[i] = new CacheL2TOKIM(tempname, *m_pclk,
            cf.m_nCacheSet,
            cf.m_nCacheAssociativity,
            cf.m_nLineSize,
            (CacheState::INJECTION_POLICY)cf.m_nInject,
            cf.m_nCacheAccessTime );

        sprintf(tempname, "bus%d", i);
        m_ppBus[i] = new BusST(tempname, *m_pclk, *m_ppCacheL2[i]);
    }

    // set total token number
    CacheState::SetTotalTokenNum( m_ppCacheL2.size() );

    // Create processor links
    g_pLinks = (LinkMGS**)malloc(cf.m_nProcs * sizeof(LinkMGS*));
    m_ppProcessors.resize(cf.m_nProcs);
    for (unsigned int i = 0; i < cf.m_nProcs; i++)
    {
        sprintf(tempname, "p%d", i);
        g_pLinks[i] = m_ppProcessors[i] = new ProcessorTOK(tempname, *m_pclk, i);
        
        // Bind to L2 cache bus
        m_ppBus[i / cf.m_nProcessorsPerCache]->BindMaster(*m_ppProcessors[i]);
    }

    m_ppMem.resize(cf.m_nMemoryChannels);
    m_ppDirectoryRoot.resize(cf.m_nNumRootDirs);

    m_pNet = new Network(*m_pclkroot);

    // create level0 network
    m_ppDirectoryL0.resize((m_ppCacheL2.size() + cf.m_nCachesPerDirectory - 1) / cf.m_nCachesPerDirectory);
    m_ppNetL0.resize(m_ppDirectoryL0.size());
    for (unsigned int i = 0; i < m_ppDirectoryL0.size(); i++)
    {
        // creating lower level network
        m_ppNetL0[i] = new Network(*m_pclk);

        sprintf(tempname, "dir-l0-%d", i);
        m_ppDirectoryL0[i] = new DirectoryTOK(tempname, *m_pclkroot,
           cf.m_nCacheSet,
           cf.m_nCacheAssociativity * cf.m_nCachesPerDirectory,
           cf.m_nLineSize,
           cf.m_nCacheAccessTime );

        // bind directory to network
        (*m_ppNetL0[i])(m_ppDirectoryL0[i]->GetBelowIF());
    }        
    
    // Connect the caches to the networks
    for (unsigned int i = 0; i < m_ppCacheL2.size(); ++i)
    {
        (*m_ppNetL0[i / cf.m_nCachesPerDirectory])(*m_ppCacheL2[i]);
    }

    // connect directories to the root-networks
    // create top level rings
    for (unsigned int i = 0; i < m_ppDirectoryL0.size(); i++)
    {
        // connect with root-level network
        unsigned int index = cf.m_nNumRootDirs * i / m_ppDirectoryL0.size();
        if (m_ppDirectoryRoot[index] == NULL)
        {
            sprintf(tempname, "split-root-%d", index);
            m_ppDirectoryRoot[index] = new DirectoryRTTOK(tempname, *m_pclkroot,
                cf.m_nCacheSet / cf.m_nNumRootDirs,
                cf.m_nCacheAssociativity * m_ppCacheL2.size(),
                cf.m_nLineSize,
                index,
                cf.m_nNumRootDirs,
                cf.m_nCacheAccessTime );
            (*m_pNet)(*m_ppDirectoryRoot[index]);
        }

        (*m_pNet)(m_ppDirectoryL0[i]->GetAboveIF());
    }
    
    // Connect networks
    for (unsigned int i = 0; i < m_ppDirectoryL0.size(); i++)
    {
        m_ppNetL0[i]->ConnectNetwork();
    }
    m_pNet->ConnectNetwork();

    m_pBSMem = new BusSwitch("membusswitch", *m_pclkroot, cf.m_nNumRootDirs, cf.m_nMemoryChannels,
        (int)ceil(log2(cf.m_nLineSize)) | ((int)(ceil(log2(cf.m_nMemoryChannels)))<<8) );

    for (unsigned int i = 0; i < cf.m_nNumRootDirs; i++)
    {
        m_pBSMem->BindMaster(*m_ppDirectoryRoot[i]);
    }

    for (unsigned int i = 0;i < cf.m_nMemoryChannels; i++)
    {
        sprintf(tempname, "ddrmem-ch-%d", i); 
        m_ppMem[i] = new DDRMemorySys(tempname, *m_pclkmem, *this, config);
        m_pBSMem->BindSlave(*m_ppMem[i]);
    }
}


TopologyS::~TopologyS()
{
    for (size_t i = 0; i < m_ppProcessors.size(); ++i)
        delete m_ppProcessors[i];

    for (size_t i = 0; i < m_ppCacheL2.size(); ++i)
    {
        delete m_ppBus[i];
        delete m_ppCacheL2[i];
    }

    delete m_pNet;

    for (size_t i = 0; i < m_ppDirectoryRoot.size(); ++i)
        delete m_ppDirectoryRoot[i];

    for (size_t i = 0; i < m_ppMem.size(); ++i)
        delete m_ppMem[i];

    delete m_pBSMem;

    delete m_pclk;
    delete m_pclkroot;
    delete m_pclkmem;
}
