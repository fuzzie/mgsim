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
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <stdexcept>

#ifndef WIN32
// On non-Windows machines (Unix, Linux, Solaris, MacOS), we use readline
// instead of cin.
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "Processor.h"
#include "ParallelMemory.h"
#include "commands.h"
#include "config.h"
#include "profile.h"
#include <math.h>
using namespace Simulator;
using namespace std;

static vector<string> Tokenize(const string& str, const string& sep)
{
    vector<string> tokens;
    for (size_t next, pos = str.find_first_not_of(sep); pos != string::npos; pos = next)
    {
        next = str.find_first_of(sep, pos);
        if (next == string::npos)
        {
            tokens.push_back(str.substr(pos));  
        }
        else
        {
            tokens.push_back(str.substr(pos, next - pos));
            next = str.find_first_not_of(sep, next);
        }
    }
    return tokens;
}

static string Trim(const string& str)
{
    size_t beg = str.find_first_not_of(" \t");
	if (beg != string::npos)
	{
		size_t end = str.find_last_not_of(" \t");
		return str.substr(beg, end - beg + 1);
	}
	return "";
}

class AlphaCMPSystem : public Object
{
    PSize           m_numProcs;
    Processor**     m_procs;
    vector<Object*> m_objects;
    Kernel          m_kernel;

public:
    //SimpleMemory*   m_memory;
    ParallelMemory*   m_memory;

    struct Config
    {
        PSize                  numProcessors;
		//SimpleMemory::Config   memory;
        ParallelMemory::Config memory;
        Processor::Config      processor;
    };

    // Get or set the debug flag
    int  debug() const   { return m_kernel.debug(); }
    void debug(int mode) { m_kernel.debug(mode); }

    uint64_t getOp() const
    {
        uint64_t op = 0;
        for (PSize i = 0; i < m_numProcs; i++) {
            op += m_procs[i]->getOp();
        }
        return op;
    }

    uint64_t getFlop() const
    {
        uint64_t flop = 0;
        for (PSize i = 0; i < m_numProcs; i++) {
            flop += m_procs[i]->getFlop();
        }
        return flop;
    }

	void PrintState() const
	{
		typedef map<string, RunState> StateMap;
		
		StateMap   states;
		streamsize length = 0;

		const Kernel::CallbackList& callbacks = m_kernel.GetCallbacks();
		for (Kernel::CallbackList::const_iterator p = callbacks.begin(); p != callbacks.end(); p++)
		{
			string name = p->first->getFQN() + ": ";
			states[name] = p->second.state;
			length = max(length, (streamsize)name.length());
		}
		
		cout << left << setfill(' ');
		for (StateMap::const_iterator p = states.begin(); p != states.end(); p++)
		{
			cout << setw(length) << p->first;
			switch (p->second)
			{
				case STATE_IDLE:     cout << "idle";     break;
				case STATE_DEADLOCK: cout << "deadlock"; break;
				case STATE_RUNNING:  cout << "running";  break;
			}
			cout << endl;
		}
	}

	void printRegFileAsyncPortActivity() const
	{
		float avg  = 0;
		float amax = 0.0f;
		float amin = 1.0f;
        for (PSize i = 0; i < m_numProcs; i++) {
            float a = m_procs[i]->getRegFileAsyncPortActivity();
			amax = max(amax, a);
			amin = min(amin, a);
			avg += a;
        }
        avg /= m_numProcs;
		cout << avg << "\t" << amin << "\t" << amax;
	}

    // Load the program file into the memory
    static void LoadProgram(IMemoryAdmin* memory, const string& path, MemAddr address)
    {
        ifstream input(path.c_str(), ios::binary);
        if (!input.is_open() || !input.good())
        {
            throw runtime_error("Unable to load program: " + path);
        }

        input.seekg(0, ios::end);
        streampos size = input.tellg();
        char* data = new char[size];
        input.seekg(0, ios::beg);
        input.read(data, size);
        memory->write(address, data, size);
        delete[] data;
    }

    const Kernel& getKernel() const { return m_kernel; }
          Kernel& getKernel()       { return m_kernel; }

    // Find a component in the system given its path
    // Returns NULL when the component is not found
    Object* GetComponent(const string& path)
    {
        Object* cur = this;
        vector<string> names = Tokenize(path, ".");
        for (vector<string>::iterator p = names.begin(); cur != NULL && p != names.end(); p++)
        {
            transform(p->begin(), p->end(), p->begin(), ::toupper);

            Object* next = NULL;
            for (unsigned int i = 0; i < cur->getNumChildren(); i++)
            {
                Object* child = cur->getChild(i);
                string name   = child->getName();
                transform(name.begin(), name.end(), name.begin(), ::toupper);
                if (name == *p)
                {
                    next = child;
                    break;
                }
            }
            cur = next;
        }
        return cur;
    }

    // Steps the entire system this many cycles
    RunState step(CycleNo nCycles)
    {
		try
		{
			Kernel& kernel = getKernel();
			return kernel.step(nCycles);
		}
		catch (Exception& e)
		{
			cout << endl << "Exception:" << endl << e.getType() << ": " << e.getMessage() << endl;
        }
		return STATE_IDLE;
    }

    AlphaCMPSystem(const Config& config, const string& program, MemAddr loadAddress, MemAddr runAddress, const vector<pair<RegAddr, RegValue> >& regs)
      : Object(NULL, NULL, "system"),
        m_numProcs(config.numProcessors)
    {
        //m_memory = new SimpleMemory(this, m_kernel, "memory", config.memory);

        m_memory = new ParallelMemory(this, m_kernel, "memory", config.memory, m_numProcs);
        m_objects.resize(m_numProcs + 1);
        m_objects[m_numProcs] = m_memory;

        // Create processor group
        m_procs = new Processor*[config.numProcessors];
        for (PSize i = 0; i < m_numProcs; i++)
        {
            stringstream name;
            name << "cpu" << i;
            m_procs[i]   = new Processor(this, m_kernel, i, m_numProcs, name.str(), *m_memory, config.processor, runAddress);
            m_objects[i] = m_procs[i];
        }

        // Connect processors in ring
        for (PSize i = 0; i < m_numProcs; i++)
        {
            m_procs[i]->initialize(*m_procs[(i+m_numProcs-1) % m_numProcs], *m_procs[(i+1) % m_numProcs]);
        }

        // Load the program into memory
        LoadProgram(m_memory, program, loadAddress);
        
        if (m_numProcs > 0)
        {
	        // Fill initial registers
	        for (size_t i = 0; i < regs.size(); i++)
	        {
	        	m_procs[0]->writeRegister(regs[i].first, regs[i].second);
	        }
	    }
    }

    ~AlphaCMPSystem()
    {
        delete m_memory;
        for (PSize i = 0; i < m_numProcs; i++)
        {
            delete m_procs[i];
        }
        delete[] m_procs;
    }
};

//
// Gets a command line from an input stream
//
static string GetCommandLine( const string& prompt, istream& input = cin )
{
    string line;
#ifdef WIN32
    // Simply read from cin
    cout << prompt;
    while (1)
    {
        int ch = cin.get();
        if (ch == '\n')
        {
            break;
        }
        line += (char)ch;
    }
#else
    // Use readline
    char* str = readline(prompt.c_str());
    if (str != NULL)
    {
        line = str;
        add_history(str);
        free(str);
    }
#endif
    return line;
}

// Return the classname. That's everything after the last ::, if any
// TODO: This doesn't work as well on *NIX. Fix it.
static const char* GetClassName(const type_info& info)
{
    const char* name = info.name();
    const char* pos  = strrchr(name, ':');
    return (pos != NULL) ? pos + 1 : name;
}

// Print all components that are a child of root
static void PrintComponents(const Object* root, const string& indent = "")
{
    for (unsigned int i = 0; i < root->getNumChildren(); i++)
    {
        const Object* child = root->getChild(i);
        string str = indent + child->getName();

        cout << str << " ";
        for (size_t len = str.length(); len < 30; len++) cout << " ";
        cout << GetClassName(typeid(*child)) << endl;

        PrintComponents(child, indent + "  ");
    }
}

// Prints the help text
static void PrintHelp()
{
    cout <<
        "Available commands:\n"
        "-------------------\n"
        "(h)elp         Print this help text.\n"
        "(p)rint        Print all components in the system.\n"
        "(s)tep         Advance the system one clock cycle.\n"
        "(r)un          Run the system until it is idle or deadlocks.\n"
        "               Deadlocks or livelocks will not be reported.\n"
        "debug [mode]   Show debug mode or set debug mode (SIM, PROG or ALL).\n"
        "profiles       Lists the total time of the profiled section.\n"
        "idle           Prints the idle state for all components.\n"
        "\n"
        "help <component>            Show the supported methods and options for this\n"
        "                            component.\n"
        "read <component> <options>  Read data from this component.\n"
        "info <component> <options>  Get general information from this component.\n"
        << endl;
}

static AlphaCMPSystem::Config ParseConfig(const Config& configfile)
{
    AlphaCMPSystem::Config config;
    config.numProcessors    = configfile.getInteger<PSize>("NumProcessors", 1);

    config.memory.baseRequestTime = configfile.getInteger<CycleNo>("MemoryBaseRequestTime", 1);
    config.memory.timePerLine     = configfile.getInteger<CycleNo>("MemoryTimePerLine", 1);
    config.memory.sizeOfLine      = configfile.getInteger<size_t>("MemorySizeOfLine", 8);
    config.memory.bufferSize      = configfile.getInteger<BufferSize>("MemoryBufferSize", INFINITE);
    config.memory.width           = configfile.getInteger<size_t>("MemoryParallelRequests", 1);

	config.processor.dcache.lineSize           = 
	config.processor.icache.lineSize           = configfile.getInteger<size_t>("CacheLineSize",    64);
	config.processor.pipeline.controlBlockSize = configfile.getInteger<size_t>("ControlBlockSize", 64);

	config.processor.icache.assoc    = configfile.getInteger<size_t>("ICacheAssociativity", 4);
	config.processor.icache.sets     = configfile.getInteger<size_t>("ICacheNumSets", 4);
	config.processor.dcache.assoc      = configfile.getInteger<size_t>("DCacheAssociativity", 4);
	config.processor.dcache.sets       = configfile.getInteger<size_t>("DCacheNumSets", 4);
	config.processor.threadTable.numThreads  = configfile.getInteger<TSize>("NumThreads", 64);
	config.processor.familyTable.numFamilies = configfile.getInteger<FSize>("NumFamilies", 8);
	config.processor.familyTable.numGlobals  = configfile.getInteger<FSize>("NumGlobalFamilies", 8);
	config.processor.registerFile.numIntegers      = configfile.getInteger<RegSize>("NumIntRegisters", 1024);
	config.processor.raunit.blockSizes[RT_INTEGER] = configfile.getInteger<RegSize>("IntRegistersBlockSize", 32); 
	config.processor.registerFile.numFloats        = configfile.getInteger<RegSize>("NumFltRegisters", 128);
	config.processor.raunit.blockSizes[RT_FLOAT]   = configfile.getInteger<RegSize>("FltRegistersBlockSize", 8); 
	config.processor.allocator.localCreatesSize  = configfile.getInteger<BufferSize>("LocalCreatesQueueSize", INFINITE);
	config.processor.allocator.remoteCreatesSize = configfile.getInteger<BufferSize>("RemoteCreatesQueueSize", INFINITE);
	config.processor.allocator.cleanupSize       = configfile.getInteger<BufferSize>("ThreadCleanupQueueSize", INFINITE);
	config.processor.fpu.addLatency  = configfile.getInteger<CycleNo>("FPUAddLatency",  1);
	config.processor.fpu.subLatency  = configfile.getInteger<CycleNo>("FPUSubLatency",  1);
	config.processor.fpu.mulLatency  = configfile.getInteger<CycleNo>("FPUMulLatency",  1);
	config.processor.fpu.divLatency  = configfile.getInteger<CycleNo>("FPUDivLatency",  1);
	config.processor.fpu.sqrtLatency = configfile.getInteger<CycleNo>("FPUSqrtLatency", 1);

    return config;
}

static void PrintProfiles()
{
    if (!ProfilingEnabled())
    {
        cout << "Profiling is not enabled." << endl
             << "Please build the simulator with the PROFILE macro defined." << endl;
        return;
    }

    const ProfileMap& profiles = GetProfiles();
    if (profiles.empty())
    {
        cout << "There are no profiles to show" << endl;
    }
    else
    {
        // Get maximum name length and total time
        size_t   length = 0;
        uint64_t total  = 0;
        for (ProfileMap::const_iterator p = profiles.begin(); p != profiles.end(); p++)
        {
            length = max(length, p->first.length());
            total += p->second;
        }

        for (ProfileMap::const_iterator p = profiles.begin(); p != profiles.end(); p++)
        {
            cout << setw((streamsize)length) << left << p->first << " "
                 << setw(6) << right << fixed << setprecision(2) << (p->second / 1000000.0f) << "s "
                 << setw(5) << right << fixed << setprecision(1) << (p->second * 100.0f / total) << "%" << endl;
        }
        cout << string(length + 15, '-') << endl;
        cout << setw((streamsize)length) << left << "Total:" << " "
             << setw(6) << right << fixed << setprecision(2) << total / 1000000.0f << "s 100.0%" << endl << endl;
    }
}

static void ExecuteCommand(AlphaCMPSystem& sys, const string& command, vector<string> args)
{
    // See if the command exists
    int i;
    for (i = 0; Commands[i].name != NULL; i++)
    {
        if (Commands[i].name == command)
        {
            if (args.size() == 0)
            {
                cout << "Please specify a component. Use \"print\" for a list of all components" << endl;
                break;
            }

            // Pop component name
            Object* obj = sys.GetComponent(args.front());
            args.erase(args.begin());

            if (obj == NULL)
            {
                cout << "Invalid component name" << endl;
            }
            else
            {
                // See if the object type matches
                int j;
                for (j = i; Commands[j].name != NULL && Commands[j].name == command; j++)
                {
                    if (Commands[j].execute(obj, args))
                    {
                        cout << endl;
                        break;
                    }
                }

                if (Commands[j].name == NULL || Commands[j].name != command)
                {
                    cout << "Invalid argument type for command" << endl;
                }
            }
            break;
        }
    }

    if (Commands[i].name == NULL)
    {
        // Command does not exist
        cout << "Unknown command" << endl;
    }
}

static void PrintUsage()
{
    cout <<
        "MGSim [options] <program-file>\n\n"
        "Options:\n"
        "-h, --help               Show this help\n"
        "-c, --config <filename>  Read configuration from file\n"
        "-i, --interactive        Start the simulator in interactive mode\n"
        "-a, --address <address>  Start executing from this address (default: 0)\n"
        "-l, --load <address>     Load the program file at this address (default: 0)\n"
        "-p, --print <value>      Print the value before printing the results when\n"
        "                         done simulating\n"
        "-R<X> <value>            Store the integer value in the specified register\n"
        "-F<X> <value>            Store the FP value in the specified register\n"
        "\n";
}

struct ProgramConfig
{
    string  m_programFile;
    string  m_configFile;
    MemAddr m_loadAddress;
    MemAddr m_runAddress;
    bool    m_interactive;
	string  m_print;
	
	vector<pair<RegAddr, RegValue> > m_regs;
};

static bool ParseArguments(int argc, const char* argv[], ProgramConfig& config)
{
    config.m_loadAddress = 0;
    config.m_runAddress  = 0;
    config.m_interactive = false;

    for (int i = 1; i < argc; i++)
    {
        const string arg = argv[i];
        if (arg[0] != '-')
        {
            config.m_programFile = arg;
            if (i != argc - 1)
            {
                cerr << "Warning: ignoring options after program file" << endl;
            }
            break;
        }
        else
        {
            stringstream sstream;
            sstream << arg;

                 if (arg == "-c" || arg == "--config")      config.m_configFile = argv[++i];
            else if (arg == "-i" || arg == "--interactive") config.m_interactive = true;
            else if (arg == "-h" || arg == "--help")        { PrintUsage(); return false; }
            else if (arg == "-a" || arg == "--address")     sstream >> config.m_runAddress;
            else if (arg == "-l" || arg == "--load")        sstream >> config.m_loadAddress;
            else if (arg == "-p" || arg == "--print")       config.m_print = string(argv[++i]) + " ";
            else if (toupper(arg[1]) == 'R' || toupper(arg[1]) == 'F')
            {
            	stringstream value;
                value << argv[++i];

                RegAddr  addr;
                RegValue val;

                char* endptr;
                unsigned long index = strtoul(&arg[2], &endptr, 0);
                if (*endptr != '\0') {
                	throw runtime_error("Error: invalid register specifier in option");
                }
                
            	if (toupper(arg[1]) == 'R') {
            		value >> *(int64_t*)&val.m_integer;
            		addr = MAKE_REGADDR(RT_INTEGER, index);
            	} else {
            		double f;
            		value >> f;
            		val.m_float.fromdouble(f);
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

    if (config.m_programFile.empty())
    {
        throw runtime_error("Error: no program file specified");
    }

    return true;
}

int main(int argc, const char* argv[])
{
    try
    {
        // Parse command line arguments
        ProgramConfig config;
        if (!ParseArguments(argc, argv, config))
        {
            return 0;
        }

        // Read configuration
        Config configfile(config.m_configFile);
        AlphaCMPSystem::Config systemconfig = ParseConfig(configfile);

        // Create the system
        AlphaCMPSystem sys(systemconfig, config.m_programFile, config.m_loadAddress, config.m_runAddress, config.m_regs);

        if (!config.m_interactive)
        {
            // Non-interactive mode; run and dump cycle count
            if (sys.step(INFINITE_CYCLES) == STATE_DEADLOCK)
			{
				throw runtime_error("Deadlock!");
			}
			cout << config.m_print << sys.getKernel().getCycleNo() << "\t"
                 << sys.getOp() << "\t"
                 << sys.getFlop() << "\t";
			sys.printRegFileAsyncPortActivity();
			cout << endl;
        }
        else
        {
            // Interactive mode
            cout << "Microthreaded Alpha System Simulator, version 1.0" << endl;
            cout << "Created by Mike Lankamp at the University of Amsterdam" << endl << endl;

            // Command loop
            vector<string> prevCommands;
            for (bool quit = false; !quit; )
            {
                stringstream prompt;
                prompt << dec << setw(8) << setfill('0') << right << sys.getKernel().getCycleNo() << "> ";
            
				// Read the command line and split into commands
				vector<string> commands = Tokenize(GetCommandLine(prompt.str()), ";");
				if (commands.size() == 0)
				{
					// Empty line, use previous command line
					commands = prevCommands;
				}
				prevCommands = commands;

				// Execute all commands
				for (vector<string>::const_iterator command = commands.begin(); command != commands.end() && !quit; command++)
				{
					vector<string> args = Tokenize(Trim(*command), " ");
					if (args.size() > 0)
					{
						// Pop the command from the front
						string command = args[0];
						args.erase(args.begin());

						if (command == "h" || command == "/?" || (command == "help" && args.empty()))
						{
							PrintHelp();
						}
						else if (command == "r" || command == "run" || command == "s" || command == "step")
						{
							// Step of run
							CycleNo nCycles = INFINITE_CYCLES;
							if (command[0] == 's')
							{
								// Step
								char* endptr;
								nCycles = args.empty() ? 1 : max(1UL, strtoul(args[0].c_str(), &endptr, 0));
							}

							if (sys.step(nCycles) == STATE_DEADLOCK)
							{
								cout << "Deadlock!" << endl;
							}
						}
						else if (command == "p" || command == "print")
						{
							PrintComponents(&sys);
						}
						else if (command == "exit" || command == "quit")
						{
							cout << "Thank you. Come again!" << endl;
							quit = true;
							break;
						}
						else if (command == "profiles")
						{
							PrintProfiles();
						}
						else if (command == "state")
						{
							sys.PrintState();
						}
						else if (command == "debug")
						{
							string state;
							if (!args.empty())
							{
								state = args[0];
								transform(state.begin(), state.end(), state.begin(), ::toupper);
							}
	        
                                 if (state == "SIM")  sys.debug(Kernel::DEBUG_SIM);
                            else if (state == "PROG") sys.debug(Kernel::DEBUG_PROG);
                            else if (state == "ALL")  sys.debug(Kernel::DEBUG_PROG | Kernel::DEBUG_SIM);
                            
                            string debugStr;
                            switch (sys.debug())
                            {
                            default:                                     debugStr = "nothing";   break;
                            case Kernel::DEBUG_PROG:                     debugStr = "program";   break;
                            case Kernel::DEBUG_SIM:                      debugStr = "simulator"; break;
                            case Kernel::DEBUG_PROG | Kernel::DEBUG_SIM: debugStr = "simulator and program"; break;
                            }
						    cout << "Debugging " << debugStr << endl;
						}
						else
						{
							ExecuteCommand(sys, command, args);
						}
					}
				}
			}
	    }
	}
    catch (Exception &e)
    {
        cerr << e.getMessage() << endl;
        return -1;
    }
    catch (exception &e)
    {
        cerr << e.what() << endl;
        return -1;
    }
    return 0;
}
