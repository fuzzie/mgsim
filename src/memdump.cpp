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
#include "memdump.h"
#include <fstream>
#include <iostream>

using namespace std;

void DumpMemory(Simulator::IMemoryAdmin* memory, const std::string& path, Simulator::MemAddr address, Simulator::MemSize size)
{
    ofstream output(path.c_str(), ios::binary);
    if (!output.is_open() || !output.good())
    {
        throw runtime_error("Cannot open memory dump file: " + path);
    }

    char* data = new char[size_t(size)];
    memory->Read(address, data, size);
    output.write(data, streamsize(size));
    delete[] data;
    output.close();
}
