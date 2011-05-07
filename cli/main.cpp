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
#ifdef HAVE_CONFIG_H
#include "sys_config.h"
#endif

#include "arch/MGSystem.h"
#include "simreadline.h"
#include "commands.h"
#include "sim/config.h"

#ifdef ENABLE_MONITOR
# include "sim/monitor.h"
#endif

#include <sstream>
#include <iostream>
#include <limits>

#ifdef USE_SDL
#include <SDL.h>
#endif

using namespace Simulator;
using namespace std;

struct ProgramConfig
{
    string                           m_configFile;
    string                           m_symtableFile;
    bool                             m_enableMonitor;
    bool                             m_interactive;
    bool                             m_terminate;
    bool                             m_dumpconf;
    bool                             m_quiet;
    bool                             m_dumpvars;
    vector<string>                   m_printvars;
    bool                             m_earlyquit;
    vector<pair<string,string> >     m_overrides;
    
    vector<pair<RegAddr, RegValue> > m_regs;
    vector<pair<RegAddr, string> >   m_loads;
};

static void ParseArguments(int argc, const char ** argv, ProgramConfig& config)
{
    config.m_configFile = MGSIM_CONFIG_PATH;
    config.m_enableMonitor = false;
    config.m_interactive = false;
    config.m_terminate = false;
    config.m_dumpconf = false;
    config.m_quiet = false;
    config.m_dumpvars = false;
    config.m_earlyquit = false;

    for (int i = 1; i < argc; ++i)
    {
        const string arg = argv[i];
        if (arg[0] != '-')
        {
            cerr << "Warning: converting extra argument to -o *.ROMFileName=" << arg << endl;
            config.m_overrides.push_back(make_pair("*.ROMFileName", arg));
        }
        else if (arg == "-c" || arg == "--config")      config.m_configFile    = argv[++i];
        else if (arg == "-i" || arg == "--interactive") config.m_interactive   = true;
        else if (arg == "-t" || arg == "--terminate")   config.m_terminate     = true;
        else if (arg == "-q" || arg == "--quiet")       config.m_quiet         = true;
        else if (arg == "-s" || arg == "--symtable")    config.m_symtableFile  = argv[++i];
        else if (arg == "--version")                    { PrintVersion(std::cout); exit(0); }
        else if (arg == "-h" || arg == "--help")        { PrintUsage(std::cout, argv[0]); exit(0); }
        else if (arg == "-d" || arg == "--dumpconf")    config.m_dumpconf      = true;
        else if (arg == "-m" || arg == "--monitor")     config.m_enableMonitor = true;
        else if (arg == "-l" || arg == "--list-mvars")  config.m_dumpvars      = true;
        else if (arg == "-p" || arg == "--print-final-mvars")
        {
            if (argv[++i] == NULL) {
                throw runtime_error("Error: expected variable name");
            }
            config.m_printvars.push_back(argv[i]);
        }
        else if (arg == "-n" || arg == "--do-nothing")  config.m_earlyquit     = true;
        else if (arg == "-o" || arg == "--override")
        {
            if (argv[++i] == NULL) {
                throw runtime_error("Error: expected configuration option");
            }
            string arg = argv[i];
            string::size_type eq = arg.find_first_of("=");
            if (eq == string::npos) {
                throw runtime_error("Error: malformed configuration override syntax");
            }
            string name = arg.substr(0, eq);
            transform(name.begin(), name.end(), name.begin(), ::toupper);
            config.m_overrides.push_back(make_pair(name, arg.substr(eq + 1)));
        }
        else if (arg[1] == 'L')  
        { 
            if (argv[++i] == NULL) {
                throw runtime_error("Error: expected filename");
            }
            string filename(argv[i]);
            char* endptr; 
            RegAddr  addr; 
            unsigned long index = strtoul(&arg[2], &endptr, 0); 
            if (*endptr != '\0') { 
                throw runtime_error("Error: invalid register specifier in option"); 
            } 
            addr = MAKE_REGADDR(RT_INTEGER, index);                      
            config.m_loads.push_back(make_pair(addr, filename)); 
        } 
        else if (arg[1] == 'R' || arg[1] == 'F')
        {
            if (argv[++i] == NULL) {
                throw runtime_error("Error: expected register value");
            }
            stringstream value;
            value << argv[i];

            RegAddr  addr;
            RegValue val;

            char* endptr;
            unsigned long index = strtoul(&arg[2], &endptr, 0);
            if (*endptr != '\0') {
                throw runtime_error("Error: invalid register specifier in option");
            }
                
            if (arg[1] == 'R') {
                value >> *(signed Integer*)&val.m_integer;
                addr = MAKE_REGADDR(RT_INTEGER, index);
            } else {
                double f;
                value >> f;
                val.m_float.fromfloat(f);
                addr = MAKE_REGADDR(RT_FLOAT, index);
            }
            if (value.fail()) {
                throw runtime_error("Error: invalid value for register");
            }
            val.m_state = RST_FULL;
            config.m_regs.push_back(make_pair(addr, val));
        }
    }
}

void PrintFinalVariables(const ProgramConfig& cfg)
{
    if (!cfg.m_printvars.empty())
    {
        std::cout << "### begin end-of-simulation variables" << std::endl;
        for (size_t i = 0; i < cfg.m_printvars.size(); ++i)
            ReadSampleVariables(cout, cfg.m_printvars[i]);
        std::cout << "### end end-of-simulation variables" << std::endl;
    }
}

#ifdef USE_SDL
extern "C"
#endif
int main(int argc, char** argv)
{
    srand(time(NULL));
    
    try
    {
        // Parse command line arguments
        ProgramConfig config;
        ParseArguments(argc, (const char**)argv, config);

        if (config.m_interactive)
        {
            // Interactive mode
            PrintVersion(std::cout);
        }

        // Read configuration
        Config configfile(config.m_configFile, config.m_overrides);
        
        // Create the system
        MGSystem sys(configfile, 
                     config.m_symtableFile,
                     config.m_regs, config.m_loads, !config.m_interactive, !config.m_earlyquit);

#ifdef ENABLE_MONITOR
        string mo_mdfile = configfile.getValue<string>("MonitorMetadataFile", "mgtrace.md");
        string mo_tfile = configfile.getValue<string>("MonitorTraceFile", "mgtrace.out");
        Monitor mo(sys, config.m_enableMonitor, 
                   mo_mdfile, config.m_earlyquit ? "" : mo_tfile, !config.m_interactive);
#endif

        if (config.m_dumpconf)
        {
            std::clog << "### simulator version: " PACKAGE_VERSION << std::endl;
            configfile.dumpConfiguration(std::clog, config.m_configFile);
        }

        if (config.m_dumpvars)
        {
            std::cout << "### begin monitor variables" << std::endl;
            ListSampleVariables(std::cout);
            std::cout << "### end monitor variables" << std::endl;
        }

        if (config.m_earlyquit)
            exit(0);

        bool interactive = config.m_interactive;
        if (!interactive)
        {
            // Non-interactive mode; run and dump cycle count
            try
            {
#ifdef ENABLE_MONITOR
                mo.start();
#endif
                StepSystem(sys, INFINITE_CYCLES);
#ifdef ENABLE_MONITOR
                mo.stop();
#endif
                
                if (!config.m_quiet)
                {
                    clog << "### begin end-of-simulation statistics" << endl;
                    sys.PrintAllStatistics(clog);
                    clog << "### end end-of-simulation statistics" << endl;
                }
            }
            catch (const exception& e)
            {
#ifdef ENABLE_MONITOR
                mo.stop();
#endif
                if (config.m_terminate) 
                {
                    // We do not want to go to interactive mode,
                    // rethrow so it abort the program.
                    PrintFinalVariables(config);
                    throw;
                }
                
                PrintException(cerr, e);
                
                // When we get an exception in non-interactive mode,
                // jump into interactive mode
                interactive = true;
            }
        }
        
        if (interactive)
        {
            // Command loop
            cout << endl;
            CommandLineReader clr;
            cli_context ctx = { clr, sys
#ifdef ENABLE_MONITOR
                                , mo 
#endif
            };

            while (HandleCommandLine(ctx) == false)
                /* just loop */;
        }

        PrintFinalVariables(config);
    }
    catch (const exception& e)
    {
        PrintException(cerr, e);
        return 1;
    }
    
    return 0;
}
