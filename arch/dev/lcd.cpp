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
#include <sstream>
#include <unistd.h>
#include <cstring>
#include <cctype>
#include <cassert>
#include "lcd.h"

namespace Simulator
{

// size_t LCD::GetSize() const { return m_width * m_height + 1; }

LCD::LCD(const std::string& name, Object& parent, 
         size_t width, size_t height,
         size_t startrow, size_t startcolumn,
         unsigned bgcolor, unsigned fgcolor)
    : Object(name, parent, parent.GetClock()),
      m_buffer(0),
      m_width(width), m_height(height),
      m_startrow(startrow), m_startcolumn(startcolumn),
      m_bgcolor(bgcolor % 10), m_fgcolor(fgcolor % 10),
      m_curx(0), m_cury(0)
{
    assert(width * height > 0);
    m_buffer = new char[width * height];
    memset(m_buffer, ' ', width * height);
}

bool LCD::OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data)
{
    if (data.size < 1)
        return true;
    
    COMMIT{
        if (address == m_width * m_height)
        {
            /* print with autoscroll */
            char thebyte = ((const char*)data.data)[0];
            if (std::isprint(thebyte))
            {
                m_buffer[m_cury * m_width + m_curx++] = thebyte;
                Refresh(m_cury, m_cury + 1);
            }
            else if (thebyte == '\t')
            {
                for (; m_curx < m_width && (m_curx % 8) != 0; ++m_curx)
                    m_buffer[m_cury * m_width + m_curx] = ' ';
                Refresh(m_cury, m_cury + 1);
            }
            else if (thebyte == '\r')
            {
                m_curx = 0;
            }
            else if (thebyte == '\n')
            {
                memset(m_buffer + m_cury * m_width + m_curx, ' ', m_width - m_curx);
                m_curx = 0;
                ++m_cury;
                if (m_cury < m_height)
                {
                    memset(m_buffer + m_cury * m_width, ' ', m_width);
                    Refresh(m_cury - 1, m_cury + 1);
                }
            }
            else if (thebyte == '\f')
            {
                memset(m_buffer, ' ', m_width * m_height);
                m_curx = m_cury = 0;
                Refresh(0, m_height);
            }
            else if (thebyte == '\b')
            {
                if (m_curx > 0)
                    --m_curx;
            }
            /* if necessary, force newline */
            if (m_curx >= m_width)
            {
                m_curx = 0;
                ++m_cury;
                if (m_cury < m_height)
                {
                    memset(m_buffer + m_cury * m_width, ' ', m_width);
                    Refresh(m_cury - 1, m_cury + 1);
                }
            }
            /* if necessary, scroll up */
            if (m_cury >= m_height)
            {
                memmove(m_buffer, m_buffer + m_width, (m_height - 1) * m_width);
                memset(m_buffer + (m_height - 1) * m_width, ' ', m_width);
                m_cury = m_height - 1;
                Refresh(0, m_height);
            }
            
        }
        else
        {
            for (size_t i = 0; i < data.size; ++i)
            {
                char thebyte = ((const char*)data.data)[i];
                m_buffer[address + i] = std::isprint(thebyte) ? thebyte : ' ';
                
                DebugIOWrite("LCD output by device %u: %u -> %ux%u",
                             (unsigned)from, (unsigned)thebyte, 
                             (unsigned)(i % m_width + 1), (unsigned)(i / m_width + 1));
            }
            Refresh(address / m_width, (address + data.size) / m_width + 1);
        }
    }
    return true;
}



#define ESC ((char)27)

void LCD::Refresh(unsigned first, unsigned last) const
{
    std::stringstream color;
    color    << ESC << '[' << (m_bgcolor + 40) << ';' << (m_fgcolor + 30) << 'm';
    char save[] = { ESC, '7' };
    char restore[] = { ESC, '8' };

    write(0, save, sizeof(save));
    for (size_t j = first; j < last && j < m_height; ++j)
    {
        std::stringstream pos;
        pos << ESC << '[' << (m_startrow + j) << ';' << m_startcolumn << 'H';
        write(0, pos.str().c_str(), pos.str().size());
        write(0, color.str().c_str(), color.str().size());
        write(0, m_buffer + j * m_width, m_width);
    }
    write(0, restore, sizeof(restore));
}    

}
