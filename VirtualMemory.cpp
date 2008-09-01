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
#include "VirtualMemory.h"
#include "except.h"
using namespace std;

namespace Simulator
{

bool VirtualMemory::CheckPermissions(MemAddr address, MemSize size, int access) const
{
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

	size_t base = (size_t)address & -BLOCK_SIZE;	// Base address of block containing address
	for (BlockMap::const_iterator pos = m_blocks.find(base); size > 0; ++pos)
	{
		if (pos == m_blocks.end() || (pos->second.permissions & access) != access)
		{
			// Block not found or not the correct permissions
			return false;
		}

		size_t count = min( (size_t)size, (size_t)BLOCK_SIZE);
		size  -= count;
		base  += BLOCK_SIZE;
	}
	return true;
}

void VirtualMemory::read(MemAddr address, void* _data, MemSize size) const
{
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

	size_t base   = (size_t)address & -BLOCK_SIZE;	// Base address of block containing address
	size_t offset = (size_t)address - base;			// Offset within base block of address
	char*  data   = static_cast<char*>(_data);		// Byte-aligned pointer to destination

	for (BlockMap::const_iterator pos = m_blocks.lower_bound(base); size > 0;)
	{
		if (pos == m_blocks.end())
		{
			// Rest of address range does not exist, fill with garbage
			memset(data, 0xCD, (size_t)size);
			break;
		}

		// Number of bytes to read, initially
		size_t count = min( (size_t)size, (size_t)BLOCK_SIZE);

		if (pos->first > base) {
			// This part of the request does not exist, fill with garbage
			memset(data, 0xCD, count);
		} else {
			// Read data
			memcpy(data, pos->second.data + offset, count);
			++pos;
		}
		size  -= count;
		data  += count;
		base  += BLOCK_SIZE;
		offset = 0;
	}
}

void VirtualMemory::write(MemAddr address, const void* _data, MemSize size, int perm)
{
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

	size_t      base   = (size_t)address & -BLOCK_SIZE;		// Base address of block containing address
	size_t      offset = (size_t)address - base;			// Offset within base block of address
	const char* data   = static_cast<const char*>(_data);	// Byte-aligned pointer to destination

	BlockMap::iterator pos = m_blocks.begin();
	while (size > 0)
	{
		// Find or insert the block
		Block block = {NULL};

		pos = m_blocks.insert(pos, make_pair(base, block));
		if (pos->second.data == NULL) {
			// A new element was inserted, allocate and set memory
			pos->second.data        = new char[BLOCK_SIZE];
			pos->second.permissions = perm;
			memset(pos->second.data, 0xCD, BLOCK_SIZE);
		}
		
		// Number of bytes to write, initially
		size_t count = min( (size_t)size, (size_t)BLOCK_SIZE - offset);

		// Write data
		memcpy(pos->second.data + offset, data, count);
		size  -= count;
		data  += count;
		base  += BLOCK_SIZE;
		offset = 0;
	}
}

VirtualMemory::~VirtualMemory()
{
	// Clean up all allocated memory
	for (BlockMap::const_iterator p = m_blocks.begin(); p != m_blocks.end(); p++)
	{
		delete[] p->second.data;
	}
}

}

