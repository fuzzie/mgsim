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
#ifndef LOADER_H
#define LOADER_H

#include "Memory.h"
#include <algorithm>

// Load the program file into the memory;
// return entry pointer and end address.
std::pair<Simulator::MemAddr,Simulator::MemAddr>
 LoadProgram(Simulator::IMemoryAdmin* memory, const std::string& path, bool quiet);

// Load a data file into the memory;
// load address past the data.
Simulator::MemAddr LoadDataFile(Simulator::IMemoryAdmin* memory, const std::string& path,
				Simulator::MemAddr base, bool quiet);

#endif
