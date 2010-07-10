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
#include "Pipeline.h"
#include "Processor.h"
#include "FPU.h"
#include "config.h"
#include "symtable.h"

#include <limits>
#include <cassert>
#include <iomanip>
#include <sstream>
using namespace std;

namespace Simulator
{

Pipeline::Stage::Stage(const std::string& name, Pipeline& parent)
:   Object(name, parent),
    m_parent(parent)
{
}

Pipeline::Pipeline(
    const std::string&  name,
    Processor&          parent,
    LPID                lpid,
    RegisterFile&       regFile,
    Network&            network,
    Allocator&          alloc,
    FamilyTable&        familyTable,
    ThreadTable&        threadTable,
    ICache&             icache,
    DCache&             dcache,
    FPU&                fpu,
    const Config&       config)
:
    Object(name, parent),
    p_Pipeline("pipeline", delegate::create<Pipeline, &Pipeline::DoPipeline>(*this)),
    m_parent(parent),
    
    m_active(*parent.GetKernel()),
    
    m_nStagesRunnable(0), m_nStagesRun(0),
    m_pipelineBusyTime(0)
{
    static const size_t NUM_FIXED_STAGES = 6;
    
    m_active.Sensitive(p_Pipeline);
    
    // Number of forwarding delay slots between the Memory and Writeback stage
    const size_t num_dummy_stages = config.getInteger<size_t>("NumPipelineDummyStages", 0);
    
    m_stages.resize( num_dummy_stages + NUM_FIXED_STAGES );

    // Create the Fetch stage
    m_stages[0].stage  = new FetchStage(*this, m_fdLatch, alloc, familyTable, threadTable, icache, lpid, config);
    m_stages[0].input  = NULL;
    m_stages[0].output = &m_fdLatch;

    // Create the Decode stage
    m_stages[1].stage  = new DecodeStage(*this, m_fdLatch, m_drLatch, config);
    m_stages[1].input  = &m_fdLatch;
    m_stages[1].output = &m_drLatch;

    // Construct the Read stage later, after all bypasses have been created
    m_stages[2].input  = &m_drLatch;
    m_stages[2].output = &m_reLatch;
    std::vector<BypassInfo> bypasses;

    // Create the Execute stage
    m_stages[3].stage  = new ExecuteStage(*this, m_reLatch, m_emLatch, alloc, familyTable, threadTable, fpu, fpu.RegisterSource(regFile), config);
    m_stages[3].input  = &m_reLatch;
    m_stages[3].output = &m_emLatch;
    bypasses.push_back(BypassInfo(m_emLatch.empty, m_emLatch.Rc, m_emLatch.Rcv));

    // Create the Memory stage
    m_stages[4].stage  = new MemoryStage(*this, m_emLatch, m_mwLatch, dcache, alloc, config);
    m_stages[4].input  = &m_emLatch;
    m_stages[4].output = &m_mwLatch;
    bypasses.push_back(BypassInfo(m_mwLatch.empty, m_mwLatch.Rc, m_mwLatch.Rcv));

    // Create the dummy stages   
    MemoryWritebackLatch* last_output = &m_mwLatch;    
    m_dummyLatches.resize(num_dummy_stages);
    for (size_t i = 0; i < num_dummy_stages; ++i)
    {
        const size_t j = i + NUM_FIXED_STAGES - 1;
        StageInfo& si = m_stages[j];
        
        MemoryWritebackLatch& output = m_dummyLatches[i];
        bypasses.push_back(BypassInfo(output.empty, output.Rc, output.Rcv));

        stringstream name;
        name << "dummy" << i;
        si.input  = last_output;
        si.output = &output;
        si.stage  = new DummyStage(name.str(), *this, *last_output, output, config);
        
        last_output = &output;
    }
    
    // Create the Writeback stage
    m_stages.back().stage  = new WritebackStage(*this, *last_output, regFile, alloc, threadTable, network, config);
    m_stages.back().input  = m_stages[m_stages.size() - 2].output;
    m_stages.back().output = NULL;
    bypasses.push_back(BypassInfo(m_mwBypass.empty, m_mwBypass.Rc, m_mwBypass.Rcv));

    m_stages[2].stage = new ReadStage(*this, m_drLatch, m_reLatch, regFile, bypasses, config);
}

Pipeline::~Pipeline()
{
    for (vector<StageInfo>::const_iterator p = m_stages.begin(); p != m_stages.end(); ++p)
    {
        delete p->stage;
    }
}

Result Pipeline::DoPipeline()
{
    if (IsAcquiring())
    {
        // Begin of the cycle, initialize
        for (vector<StageInfo>::iterator p = m_stages.begin(); p != m_stages.end(); ++p)
        {
            p->status = (p->input != NULL && p->input->empty ? DELAYED : SUCCESS);
        }
    
        /*
         Make a copy of the WB latch before doing anything. This will be used as
         the source for the bypass to the Read Stage. This can be justified by
         noting that the stages *should* happen in parallel, so the read stage
         will read the WB latch before it's been updated.
        */
        m_mwBypass = m_dummyLatches.empty() ? m_mwLatch : m_dummyLatches.back();
    }
    
    Result result = FAILED;
    m_nStagesRunnable = 0;

    vector<StageInfo>::reverse_iterator stage;
    try
    {
    for (stage = m_stages.rbegin(); stage != m_stages.rend(); ++stage)
    {
        if (stage->status == FAILED) 
        {
            // The pipeline stalled at this point
            break;
        }
        
        if (stage->status == SUCCESS)
        {
            m_nStagesRunnable++;

            const PipeAction action = stage->stage->OnCycle();
            if (!IsAcquiring())
            {
                // If this stage has stalled or is delayed, abort pipeline.
                // Note that the stages before this one in the pipeline
                // will never get executed now.
                if (action == PIPE_STALL)
                {
                    stage->status = FAILED;
                    DeadlockWrite("%s stage stalled", stage->stage->GetName().c_str());
                    break;
                }
                
                if (action == PIPE_DELAY)
                {
                    result = SUCCESS;
                    break;
                }
                
                if (action == PIPE_IDLE)
                {
                    m_nStagesRunnable--;
                }
                else
                {
                    if (action == PIPE_FLUSH && stage->input != NULL)
                    {
                        // Clear all previous stages with the same TID
                        const TID tid = stage->input->tid;
                        for (vector<StageInfo>::reverse_iterator f = stage + 1; f != m_stages.rend(); ++f)
                        {
                            if (f->input != NULL && f->input->tid == tid)
                            {
                                f->input->empty = true;
                                f->status = DELAYED;
                            }
                            f->stage->Clear(tid);
                        }
                    }
                    
                    COMMIT
                    {
                        // Clear input and set output
                        if (stage->input  != NULL) stage->input ->empty = true;
                        if (stage->output != NULL) stage->output->empty = false;
                    }
                    result = SUCCESS;
                }
            }
            else
            {
                result = SUCCESS;
            }
        }
    }
    }
    catch (SimulationException& e)
    {
        if (stage->input != NULL)
        {
            // Add details about thread, family and PC
            stringstream details;
            details << "While executing instruction at " << GetKernel()->GetSymbolTable()[stage->input->pc_dbg] 
                        // "0x" << setw(sizeof(MemAddr) * 2) << setfill('0') << hex << stage->input->pc_dbg
                    << " in T" << dec << stage->input->tid << " in F" << stage->input->fid;
            e.AddDetails(details.str());
        }
        throw;
    }
    
    if (m_nStagesRunnable == 0) {
        // Nothing to do anymore
        m_active.Clear();
        return SUCCESS;
    }
    
    COMMIT
    {
        m_nStagesRun += m_nStagesRunnable;
        m_pipelineBusyTime++;
    }
    
    m_active.Write(true);
    return result;
}

void Pipeline::Cmd_Help(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
    "The pipeline reads instructions, loads operands, computes their results and/or\n"
    "dispatches asynchronous operations such as memory loads or FPU operations and\n"
    "finally writes back the result.\n\n"
    "Supported operations:\n"
    "- read <component>\n"
    "  Reads and displays the stages and latches.\n";
}

void Pipeline::PrintLatchCommon(std::ostream& out, const CommonData& latch) const
{
    out << " | LFID: F"  << dec << latch.fid
        << "    TID: T"  << dec << latch.tid << right
        << "    PC: " << GetKernel()->GetSymbolTable()[latch.pc] // "0x" << hex << setw(sizeof(MemAddr) * 2) << setfill('0') << latch.pc
        << "    Annotation: " << ((latch.kill) ? "End" : (latch.swch ? "Switch" : "None")) << endl
        << " |" << endl;
}

// Construct a string representation of a pipeline register value
/*static*/ std::string Pipeline::MakePipeValue(const RegType& type, const PipeValue& value)
{
    std::stringstream ss;

    switch (value.m_state)
    {
        case RST_INVALID: ss << "N/A";     break;
        case RST_PENDING: ss << "Pending"; break;
        case RST_EMPTY:   ss << "Empty";   break;
        case RST_WAITING: ss << "Waiting (T" << dec << value.m_waiting.head << ")"; break;
        case RST_FULL:
            if (type == RT_INTEGER) {
                ss << "0x" << setw(value.m_size * 2)
                   << setfill('0') << hex << value.m_integer.get(value.m_size);
            } else {
                ss << setprecision(16) << fixed << value.m_float.tofloat(value.m_size);
            }
            break;
    }

    std::string ret = ss.str();
    if (ret.length() > 18) {
        ret = ret.substr(0,18);
    }
    return ret;
}

static std::ostream& operator << (std::ostream& out, const RemoteRegAddr& rreg)
{
    if (rreg.fid.lfid != INVALID_LFID)
    {
        switch (rreg.type)
        {
        case RRT_DETACH:          out << "Detach"; break;
        case RRT_GLOBAL:          out << "Global "          << rreg.reg.str(); break;
        case RRT_NEXT_DEPENDENT:  out << "Next Dependent "  << rreg.reg.str(); break;
        case RRT_FIRST_DEPENDENT: out << "First Dependent " << rreg.reg.str(); break;
        case RRT_LAST_SHARED:     out << "Last Shared "     << rreg.reg.str(); break;
        default:                  assert(false); break;
        }
    
        out << ", F" << dec << rreg.fid.lfid;
        if (rreg.fid.pid != INVALID_GPID) {
            out << "@CPU" << rreg.fid.pid;
        }
    }
    else
    {
        out << "N/A";
    }
    return out;
}

void Pipeline::Cmd_Read(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    // Fetch stage
    out << "Fetch stage" << endl
        << " |" << endl;
    if (m_fdLatch.empty)
    {
        out << " | (Empty)" << endl;
    }
    else
    {
        PrintLatchCommon(out, m_fdLatch);
        out << " | Instr: 0x" << hex << setw(sizeof(Instruction) * 2) << setfill('0') << m_fdLatch.instr << endl;
    }
    out << " v" << endl;

    // Decode stage
    out << "Decode stage" << endl
        << " |" << endl;
    if (m_drLatch.empty)
    {
        out << " | (Empty)" << endl;
    }
    else
    {
        PrintLatchCommon(out, m_drLatch);
        out  << hex << setfill('0')
#if TARGET_ARCH == ARCH_ALPHA
             << " | Opcode:       0x" << setw(2) << (unsigned)m_drLatch.opcode << endl
             << " | Function:     0x" << setw(4) << m_drLatch.function << endl
             << " | Displacement: 0x" << setw(8) << m_drLatch.displacement << endl
#elif TARGET_ARCH == ARCH_SPARC
             << " | Op1:          0x" << setw(2) << (unsigned)m_drLatch.op1
             << "    Op2: 0x" << setw(2) << (unsigned)m_drLatch.op2
             << "    Op3: 0x" << setw(2) << (unsigned)m_drLatch.op3 << endl
             << " | Function:     0x" << setw(4) << m_drLatch.function << endl
             << " | Displacement: 0x" << setw(8) << m_drLatch.displacement << endl
#endif
             << " | Literal:      0x" << setw(8) << m_drLatch.literal << endl
             << dec
             << " | Ra:           " << m_drLatch.Ra << "/" << m_drLatch.RaSize << endl
             << " | Rb:           " << m_drLatch.Rb << "/" << m_drLatch.RbSize << endl
             << " | Rc:           " << m_drLatch.Rc << "/" << m_drLatch.RcSize << endl
#if TARGET_ARCH == ARCH_SPARC
             << " | Rs:           " << m_drLatch.Rs << "/" << m_drLatch.RcSize << endl
#endif
            ;
    }
    out << " v" << endl;

    // Read stage
    out << "Read stage" << endl
        << " |" << endl;
    if (m_reLatch.empty)
    {
        out << " | (Empty)" << endl;
    }
    else
    {
        PrintLatchCommon(out, m_reLatch);
        out  << hex << setfill('0')
#if TARGET_ARCH == ARCH_ALPHA
             << " | Opcode:       0x" << setw(2) << (unsigned)m_reLatch.opcode << endl
             << " | Function:     0x" << setw(4) << m_reLatch.function << endl
             << " | Displacement: 0x" << setw(8) << m_reLatch.displacement << endl
#elif TARGET_ARCH == ARCH_SPARC
             << " | Op1:          0x" << setw(2) << (unsigned)m_drLatch.op1
             << "    Op2: 0x" << setw(2) << (unsigned)m_drLatch.op2 
             << "    Op3: 0x" << setw(2) << (unsigned)m_drLatch.op3 << endl
             << " | Function:     0x" << setw(4) << m_reLatch.function << endl
             << " | Displacement: 0x" << setw(8) << m_reLatch.displacement << endl
#endif
             << " | Rav:          " << MakePipeValue(m_reLatch.Ra.type, m_reLatch.Rav) << "/" << m_reLatch.Rav.m_size << endl
             << " | Rbv:          " << MakePipeValue(m_reLatch.Rb.type, m_reLatch.Rbv) << "/" << m_reLatch.Rbv.m_size << endl
             << " | Rc:           " << m_reLatch.Rc << "/" << m_reLatch.RcSize << endl;
    }
    out << " v" << endl;

    // Execute stage
    out << "Execute stage" << endl
        << " |" << endl;
    if (m_emLatch.empty)
    {
        out << " | (Empty)" << endl;
    }
    else
    {
        PrintLatchCommon(out, m_emLatch);
        out << " | Rc:        " << m_emLatch.Rc << "/" << m_emLatch.Rcv.m_size << endl
            << " | Rcv:       " << MakePipeValue(m_emLatch.Rc.type, m_emLatch.Rcv) << endl;
        if (m_emLatch.size == 0)
        {
            // No memory operation
            out << " | Operation: N/A" << endl
                << " | Address:   N/A" << endl
                << " | Size:      N/A" << endl;
        }
        else
        {
            out << " | Operation: " << (m_emLatch.Rcv.m_state == RST_FULL ? "Store" : "Load") << endl
                << " | Address:   0x" << hex << setw(sizeof(MemAddr) * 2) << setfill('0') << m_emLatch.address 
                << " " << GetKernel()->GetSymbolTable()[m_emLatch.address] << endl
                << " | Size:      " << dec << m_emLatch.size << " bytes" << endl;
        }
    }
    out << " v" << endl;

    // Memory stage
    out << "Memory stage" << endl
        << " |" << endl;
    
    const MemoryWritebackLatch* latch = &m_mwLatch;
    for (size_t i = 0; i <= m_dummyLatches.size(); ++i)
    {
        if (latch->empty)
        {
            out << " | (Empty)" << endl;
        }
        else
        {
            PrintLatchCommon(out, *latch);
            out << " | Rc:  " << latch->Rc << "/" << latch->Rcv.m_size << endl
                << " | Rcv: " << MakePipeValue(latch->Rc.type, latch->Rcv) << endl;
        }
        out << " v" << endl;
        
        if (i < m_dummyLatches.size())
        {
            out << "Dummy Stage" << endl
                << " |" << endl;
            latch = &m_dummyLatches[i];
        }
    }
    
    // Writeback stage
    out << "Writeback stage" << endl;
}

}
