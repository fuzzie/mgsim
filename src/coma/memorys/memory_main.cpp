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
//////////////////////////////////////////////////////////////////////////
// sc_main

#include "predef.h"
#include "topologys.h"
#include <unistd.h>
#include <cstdio>

// include linkmgs and th header files, 
// the instance is also defined in the realization file
#include "../simlink/linkrealization.h"

using namespace MemSim;

int mgs_main(int argc, const char* argv[]);

static int RET_SUCCESS = 0;

void* thread_proc(void*)
{
	int ret = mgs_main(thpara.argc, (const char**)thpara.argv);

    // Stop the simulation
    thpara.bterm = true;
    sem_post(&thpara.sem_sync);

	return &RET_SUCCESS + ret;
}

int sc_main(int argc, char* argv[] )
{
	//////////////////////////////////////////////////////////////////////////
	// create thread for processor simulator

	cerr << "# SCM: starting MGSim thread..." << endl;

	// set parameter
	thpara.argc = argc;
	thpara.argv = argv;
	thpara.bterm = false;

	// thread creation and semaphore initialization
	sem_init(&thpara.sem_mgs);
	sem_init(&thpara.sem_sync);

	pthread_t pTh;
	int thret = pthread_create(&pTh, NULL, thread_proc, NULL);
	if (thret != 0)
	{
	  perror("pthread_create");
		cout << "SCM: Failed to create memory simulation thread." << endl ;
		return 1;
	}

	sem_wait(&thpara.sem_sync);
	cerr << "# SCM: MGSim thread started, initializing memory..." << endl;

    if (thpara.bterm)
        return 0;

	//////////////////////////////////////////////////////////////////////////
	// create topology in SystemC
    TopologyS *top;
    try {
        top = new TopologyS();
    } catch(std::exception& e) {
        cerr << e.what() << endl;
        return 1;
    }

	//////////////////////////////////////////////////////////////////////////
	// simulate both in synchronized steps
	// systemc setup is done, give the control back to simulator
	cerr << "# SCM: Memory initialized, control back to simulator..." << endl;
	sem_post(&thpara.sem_mgs);
    sem_wait(&thpara.sem_sync);

	//////////////////////////////////////////////////////////////////////////
	// start the simulation

	while(!thpara.bterm)
	{
		sem_wait(&thpara.sem_sync);
		if (thpara.bterm)
			break;
		sc_start(LinkMGS::s_oLinkConfig.m_nCycleTimeCore, SC_PS);
		sem_post(&thpara.sem_mgs);
	}

	sc_stop();

    void* ret;
    pthread_join(pTh, &ret);
    
    sem_destroy(&thpara.sem_mgs);
    sem_destroy(&thpara.sem_sync);
    
    delete top;
	return (int*)ret - &RET_SUCCESS;
}
