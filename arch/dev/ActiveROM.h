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
#ifndef ACTIVE_ROM_H
#define ACTIVE_ROM_H

#include "arch/IOBus.h"
#include "arch/Memory.h"
#include "sim/kernel.h"
#include "sim/config.h"
#include "sim/storage.h"
#include <map>

namespace Simulator
{
    class ActiveROM : public IIOBusClient, public Object
    {
    public:
        struct LoadableRange
        {
            size_t               rom_offset;
            MemAddr              vaddr;
            MemSize              size;
            IMemory::Permissions perm;
        };
    private:
        IMemoryAdmin&      m_memory;

        char              *m_data;
        size_t             m_lineSize;
        size_t             m_numLines;

        std::vector<LoadableRange> m_loadable;

        std::string        m_filename;
        bool               m_bootable;
        MemAddr            m_start_address;
        bool               m_legacy;

        // DCA parameters

        IODeviceID         m_devid;
        IIOBus&            m_iobus;
                           
        IODeviceID         m_client;
        IOInterruptID      m_completionTarget;
                           
        SingleFlag         m_loading;
        SingleFlag         m_notifying;

        size_t             m_currentRange;
        size_t             m_currentOffset;

        void LoadConfig(const Config& config);
        void LoadFile(const std::string& filename);
        void PrepareRanges(bool load_to_memory);

    public:
        ActiveROM(const std::string& name, Object& parent, IMemoryAdmin& mem, IIOBus& iobus, IODeviceID devid, Config& config);
        ~ActiveROM();

        Process p_Load;
        Process p_Notify;

        Result DoLoad();
        Result DoNotify();

        bool IsBootable() const { return m_bootable; }
        void GetBootInfo(MemAddr& start, bool& legacy) const;
        const std::string& GetProgramName() const { return m_filename; }
        IODeviceID GetDeviceID() const { return m_devid; }

        bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
        bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);
        

        void GetDeviceIdentity(IODeviceIdentification& id) const;
        std::string GetIODeviceName() const;

    };


}


#endif
