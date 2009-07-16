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
#include "RAUnit.h"
#include "RegisterFile.h"
#include "Processor.h"
#include "config.h"
#include <cassert>
#include <iomanip>
using namespace std;

namespace Simulator
{

template <typename T>
static bool IsPowerOfTwo(const T& x)
{
    return (x & (x - 1)) == 0;
}

RAUnit::RAUnit(Processor& parent, const std::string& name, const RegisterFile& regFile, const Config& config)
    : Object(&parent, &parent.GetKernel(), name)
{
    static struct {
        const char* name;
        size_t      def;
    } config_names[NUM_REG_TYPES] = {
        {"IntRegistersBlockSize", 32},
        {"FltRegistersBlockSize", 8}
    };
    
    for (RegType i = 0; i < NUM_REG_TYPES; ++i)
    {
        RegSize size = regFile.GetSize(i);

        m_blockSizes[i] = config.getInteger<size_t>(config_names[i].name, config_names[i].def);
        if (m_blockSizes[i] == 0 || !IsPowerOfTwo(m_blockSizes[i]))
        {
            throw InvalidArgumentException("Allocation block size is not a power of two");
        }

        if (size % m_blockSizes[i] != 0)
        {
            throw InvalidArgumentException("Register counts aren't a multiple of the allocation block size");
        }
    
        m_lists[i].resize(size / m_blockSizes[i], pair<RegSize,LFID>(0, INVALID_LFID));
    }
}

bool RAUnit::Alloc(const RegSize sizes[NUM_REG_TYPES], LFID fid, RegIndex indices[NUM_REG_TYPES])
{
	for (RegType i = 0; i < NUM_REG_TYPES; ++i)
	{
		indices[i] = INVALID_REG_INDEX;
		if (sizes[i] != 0)
		{
			// Get number of blocks (ceil)
			RegSize size = (sizes[i] + m_blockSizes[i] - 1) / m_blockSizes[i];

			List& list = m_lists[i];
			for (RegIndex pos = 0; pos < list.size() && indices[i] == INVALID_REG_INDEX;)
			{
				if (list[pos].first != 0)
				{
					pos += list[pos].first;
				}
				else
				{
					// Free area, check size
					for (RegIndex start = pos; pos < list.size() && list[pos].first == 0; ++pos)
					{
						if (pos - start + 1 == size)
						{
							indices[i] = start * m_blockSizes[i];
							break;
						}
					}
				}
			}

			if (indices[i] == INVALID_REG_INDEX)
			{
				// Couldn't get a block for this type
				return false;
			}
		}
	}

	COMMIT
	{ 
		// We've found blocks for all types, commit them
		for (RegType i = 0; i < NUM_REG_TYPES; ++i)
		{
			if (sizes[i] != 0)
			{
				RegSize size = (sizes[i] + m_blockSizes[i] - 1) / m_blockSizes[i];
				m_lists[i][indices[i] / m_blockSizes[i]].first  = size;
				m_lists[i][indices[i] / m_blockSizes[i]].second = fid;
			}
		}
	}

	return true;
}

void RAUnit::Free(RegIndex indices[NUM_REG_TYPES])
{
	for (RegType i = 0; i < NUM_REG_TYPES; ++i)
	{
		if (indices[i] != INVALID_REG_INDEX)
		{
			// Floor to block size
			RegIndex index = indices[i] / m_blockSizes[i];

			List& list = m_lists[i];
			assert(list[index].first != 0);

			COMMIT{ list[index].first = 0; }
		}
	}
}

void RAUnit::Cmd_Help(ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
        "The Register Allocation Unit is the component that manages the allocation\n"
        "data for the register file. It simply maintains an administration that\n"
        "indicates which registers are allocated and which are not.\n"
        "This component is used during family creation to allocate a block of\n"
        "registers for the new family.\n\n"
        "Supported operations:\n"
        "- read <component>\n"
        "  Reads the administration from the register allocation unit. Use this to\n"
        "  quickly see which registers are allocated to which family.\n";
}

void RAUnit::Cmd_Read(ostream& out, const vector<string>& /*arguments*/) const
{
    static const char* TypeNames[NUM_REG_TYPES] = {"Integer", "Float"};

    for (RegType i = 0; i < NUM_REG_TYPES; ++i)
    {
        const List&   list      = m_lists[i];
        const RegSize blockSize = m_blockSizes[i];

        out << TypeNames[i] << " registers (" << dec << blockSize << " registers per block):" << endl;
        for (size_t next, entry = 0; entry < list.size(); entry = next)
        {
            out << hex << setfill('0');
            out << "0x" << setw(4) << entry * blockSize << " - ";

            if (list[entry].first != 0) {
                next = entry + list[entry].first;
                out << "0x" << setw(4) << (next * blockSize) - 1 << ": Allocated to " << list[entry].second << endl;
            } else {
                for (next = entry + 1; next < list.size() && list[next].first == 0; ++next) {}
                out << "0x" << setw(4) << (next * blockSize) - 1 << ": Free" << endl;
            }
        }
        out << endl;
    }
}

}
