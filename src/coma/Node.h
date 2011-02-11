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
#ifndef COMA_NODE_H
#define COMA_NODE_H

#include "COMA.h"

namespace Simulator
{

/**
 * void TraceWrite(MemAddr address, const char* fmt, ...);
 *
 * Note: The argument for 'fmt' has to be a string literal.
 *
 * For use in COMA::Object instances. Writes out a message
 * if the specified address is being traced.
 */
#define TraceWrite(addr, fmt, ...) do { \
        const COMA::TraceMap& traces = m_parent.GetTraces(); \
        MemAddr __addr = (addr) / m_lineSize * m_lineSize; \
        if (!traces.empty() && traces.find((__addr)) != traces.end()) { \
            OutputWrite(("0x%llx: " fmt), (unsigned long long)(__addr), ##__VA_ARGS__); \
        } \
    } while (false)

/**
 * This class defines a generic ring node interface.
 * Both directories and caches inherit from this. This way, a heterogeneous
 * COMA hierarchy can be easily composed, since no element needs to know the
 * exact type of its neighbour.
 */
class COMA::Node : public virtual COMA::Object
{
protected:
    /// This is the message that gets sent around
    union Message
    {
        enum Type {
            REQUEST,            ///< Read request (RR)
            REQUEST_DATA,       ///< Read request with data (RD)
            REQUEST_DATA_TOKEN, ///< Read request with data and tokens (RDT)
            EVICTION,           ///< Eviction (EV)
            UPDATE,             ///< Update (UP)
        };
        
        /// The actual message contents that's simulated
        struct
        {
            Type         type;          ///< Type of message
            MemAddr      address;       ///< The address of the cache-line
            CacheID      sender;        ///< ID of the sender of the message
            bool         ignore;        ///< Just pass this message through to the top level
            MemData      data;          ///< The data (RD, RDT, EV, UP)
            bool         dirty;         ///< Is the data dirty? (EV, RD, RDT)
            unsigned int tokens;        ///< Number of tokens in this message (RDT, EV)
            size_t       client;        ///< Sending client (UP)
            TID          tid;           ///< Sending thread (UP)
        };
        
        /// For memory management
        Message* next;

        // Overload allocation for efficiency
        static void * operator new (size_t size);
        static void operator delete (void *p, size_t size);
        
        Message() {}
    private:
        Message(const Message&) {} // No copying
    };

private:    
    // Message management
    static Message*            g_FreeMessages;
    static std::list<Message*> g_Messages;
    static unsigned long       g_References;

    static void PrintMessage(std::ostream& out, const Message& msg);

    Node*             m_prev;           ///< Prev node in the ring
    Node*             m_next;           ///< Next node in the ring
protected:
    Buffer<Message*>  m_incoming;       ///< Buffer for incoming messages from the prev node
private:
    Buffer<Message*>  m_outgoing;       ///< Buffer for outgoing messages to the next node
   
    /// Process for sending to the next node
    Process           p_Forward;
    Result            DoForward();

protected:
    /// Send the message to the next node
    bool SendMessage(Message* message, size_t min_space);
    
    /// Print the incoming and outgoing buffers of this node
    void Print(std::ostream& out) const;
    
    /// Construct the node
    Node(const std::string& name, COMA& parent, Clock& clock, const Config& config);
    virtual ~Node();

public:
    void Initialize(Node* next, Node* prev);
};

}
#endif
