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
#ifndef DISPLAY_H
#define DISPLAY_H

#include "IOBus.h"
#include "kernel.h"
#include "storage.h"
#include "config.h"

struct SDL_Surface;


namespace Simulator
{
    class Display : public Object
    {
        static Display*       m_singleton;

        std::vector<uint8_t>  m_framebuffer;
        std::vector<uint32_t> m_palette;
        bool                  m_indexed;
        unsigned int          m_bpp; /* 8, 16, 24, 32 */
        unsigned int          m_width, m_height;
        float                 m_scalex_orig,       m_scalex;
        float                 m_scaley_orig,       m_scaley;
        unsigned int          m_refreshDelay_orig, m_refreshDelay;
        CycleNo               m_lastUpdate;
        SDL_Surface*          m_screen;
        unsigned int          m_max_screen_h, m_max_screen_w;

        bool                  m_enabled;

        class ControlInterface;

        class FrameBufferInterface : public IIOBusClient, public Object
        {
            IODeviceID      m_devid;
            IIOBus&         m_iobus;

            friend class ControlInterface;

            Display&        GetDisplay() { return *dynamic_cast<Display*>(GetParent()); }

        public:
            FrameBufferInterface(const std::string& name, Display& parent, IIOBus& iobus, IODeviceID devid);

            friend class Display;

            bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
            bool OnReadResponseReceived(IODeviceID from, const IOData& data) { return false; }
            bool OnInterruptRequestReceived(IOInterruptID which) { return true; }
            bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);
            void GetDeviceIdentity(IODeviceIdentification& id) const;
            
            std::string GetIODeviceName() const { return GetFQN(); }
        };

        class ControlInterface : public IIOBusClient, public Object
        {
            IODeviceID      m_devid;
            IIOBus&         m_iobus;
            std::vector<uint32_t> m_control;

            Display&        GetDisplay() { return *dynamic_cast<Display*>(GetParent()); }

        public:
            ControlInterface(const std::string& name, Display& parent, IIOBus& iobus, IODeviceID devid);

            friend class Display;

            bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
            bool OnReadResponseReceived(IODeviceID from, const IOData& data) { return false; }
            bool OnInterruptRequestReceived(IOInterruptID which) { return true; }
            bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);
            void GetDeviceIdentity(IODeviceIdentification& id) const;
            
            std::string GetIODeviceName() const { return GetFQN(); }
        };

        ControlInterface         m_ctlinterface;
        FrameBufferInterface     m_fbinterface;
        friend class ControlInterface;
        friend class FrameBufferInterface;
        
    public:
        Display(const std::string& name, Object& parent, IIOBus& iobus, IODeviceID ctldevid, IODeviceID fbdevid, Config& config);

        ~Display();

        void CheckEvents(void);
        void OnCycle(CycleNo cycle)
        {
            if (m_lastUpdate + m_refreshDelay > cycle)
                return;
            
            m_lastUpdate = cycle;
            CheckEvents();
        }

        static Display* GetDisplay() { return m_singleton; }

    protected:
        void Resize(unsigned w, unsigned h); 
        void Refresh() const;
        void ResetCaption() const;
        void ResizeScreen(unsigned int w, unsigned int h);
    };


}

#endif
