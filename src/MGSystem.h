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
#ifndef MGSYSTEM_H
#define MGSYSTEM_H

#include "Processor.h"
#include "FPU.h"
#include "kernel.h"
#include "Allocator.h"
#include "Memory.h"
#include "config.h"
#include "symtable.h"
#include "breakpoints.h"

#include <vector>
#include <utility>
#include <string>

namespace Simulator {

    class MGSystem : public Object
    {
        std::vector<Processor*> m_procs;
        std::vector<FPU*>       m_fpus;
        std::vector<Object*>    m_objects;
        std::vector<PlaceInfo*> m_places;
        SymbolTable        m_symtable;
        BreakPoints        m_breakpoints;
        Kernel             m_kernel;
        IMemoryAdmin*      m_memory;
        void*              m_pmemory;  // Will be used by CMLink if COMA is enabled.
        std::string        m_objdump_cmd;
        std::string        m_program;
        enum {
            MEMTYPE_SERIAL = 1,
            MEMTYPE_PARALLEL = 2,
            MEMTYPE_BANKED = 3,
            MEMTYPE_RANDOMBANKED = 4,
            MEMTYPE_COMA_ZL = 5,
            MEMTYPE_COMA_ML = 6
        } m_memorytype; // for WriteConfiguration

        // Writes the current configuration into memory and returns its address
        MemAddr WriteConfiguration(const Config& config);

    public:
        // Get or set the debug flag
        int  GetDebugMode() const   { return m_kernel.GetDebugMode(); }
        void SetDebugMode(int mode) { m_kernel.SetDebugMode(mode); }
        void ToggleDebugMode(int mode) { m_kernel.ToggleDebugMode(mode); }

        uint64_t GetOp() const;
        uint64_t GetFlop() const;

        std::string GetSymbol(MemAddr addr) const;

        void Disassemble(MemAddr addr, size_t sz = 64) const;
        void PrintAllSymbols(std::ostream& os, const std::string& pat = "*") const;
        void PrintMemoryStatistics(std::ostream& os) const;
        void PrintState(const std::vector<std::string>& arguments) const;
        void PrintRegFileAsyncPortActivity(std::ostream& os) const;
        void PrintPipelineEfficiency(std::ostream& os) const;
        void PrintPipelineIdleTime(std::ostream& os) const;
        void PrintAllFamilyCompletions(std::ostream& os) const;
        void PrintFamilyCompletions(std::ostream& os) const;
        void PrintCoreStats(std::ostream& os) const;
        void PrintAllStatistics(std::ostream& os) const;

        const Kernel& GetKernel() const { return m_kernel; }
        Kernel& GetKernel()       { return m_kernel; }

        // Find a component in the system given its path
        // Returns NULL when the component is not found
        Object* GetComponent(const std::string& path);

        // Steps the entire system this many cycles
        void Step(CycleNo nCycles);
        void Abort() { GetKernel().Abort(); }
    
        MGSystem(const Config& config, Display& display, const std::string& program,
                 const std::string& symtable,
                 const std::vector<std::pair<RegAddr, RegValue> >& regs,
                 const std::vector<std::pair<RegAddr, std::string> >& loads,
                 bool quiet);

        ~MGSystem();

        void DumpState(std::ostream& os);

    };

}

#endif
