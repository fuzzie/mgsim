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
#ifndef __TRACEFILE_H
#define __TRACEFILE_H

#include <cstdio>
#include <string>

/*---------------------------------------------------------------------------*/

class TraceFile {

public:
    enum EntryType {
        ENTRY_TYPE_READ,
        ENTRY_TYPE_WRITE
    };

    enum ReadResult {
        READ_RES_OK,
        READ_RES_EOF,
        READ_RES_NULL_ENTRY,
        READ_RES_FILE_ERROR,
        READ_RES_INVALID_PID,
        READ_RES_INVALID_CALL
    };

    enum OpenResult {
        OPEN_RES_OK,
        OPEN_RES_FILE_ERROR,
        OPEN_RES_MALLOC_ERROR,
        OPEN_RES_INVALID_CALL,
        OPEN_RES_INVALID_FILE_FORMAT
    };

    enum CloseResult {
        CLOSE_RES_OK,
        CLOSE_RES_FILE_ERROR,
        CLOSE_RES_INVALID_CALL
    };

    struct Entry {
        EntryType    type;
        unsigned int addr;
    };

    /*----*/

    TraceFile(const char* fName);
    ~TraceFile();

    /*----*/

    OpenResult  open();
    CloseResult close();
    ReadResult  readNext(unsigned int pid, Entry& e);

    bool eof() const;

    unsigned int getProcCnt()                  const;
    unsigned int getCurEntry(unsigned int pid) const;

    const std::string& getFileName() const;

private:
    static const char NULL_ENTRY     = 0;
    static const char NON_NULL_ENTRY = 1;

    /*----*/

    std::string  m_fName;

    unsigned int m_procsCnt;
    unsigned int m_curEntry;
    unsigned int m_curEntryReadProcs;
    unsigned int m_bufSz;

    std::FILE*   m_fHnd;
    void*        m_buffer;
    Entry**      m_inBufPtrs;
    bool*        m_readCurEntry;

    ReadResult   m_bufferFillRes;

    /*----*/
    
    TraceFile(const TraceFile& trf);

    void        fillBuffer();
    CloseResult close(bool force);

}; // class TraceFile

#endif // __TRACEFILE_H

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/

