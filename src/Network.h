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
#ifndef NETWORK_H
#define NETWORK_H

#include "ports.h"
#include <cassert>

namespace Simulator
{

class Processor;
class RegisterFile;
class Allocator;
class FamilyTable;

template <typename T>
class Register : public IRegister
{
    struct Data
    {
        bool m_full;
        T    m_data;
    };

    void OnUpdate()
    {
        m_read = m_data;
    }
    
    Data              m_data;
    Data              m_read;
    ArbitratedService m_service;

public:
    void AddSource(const ArbitrationSource& source) {
        m_service.AddSource(source);
    }
    
    void Clear()              { COMMIT{ m_data.m_full = false; } }
    void Write(const T& data) { COMMIT{ m_data.m_data = data; m_data.m_full = true; } }
    const T& Read() const     { assert(m_read.m_full); return m_read.m_data; }
    bool CanRead()  const     { return  m_read.m_full; }
    bool CanWrite()           { return !m_data.m_full && m_service.Invoke(); }

    Register(const Object& object, const std::string& name) : IRegister(*object.GetKernel()), m_service(object, name)
    {
        m_data.m_full = m_read.m_full = false;
    }

    Register(const Object& object, const std::string& name, const T& def) : IRegister(*object.GetKernel()), m_service(object, name)
    {
        m_data.m_full = m_read.m_full = true;
		m_data.m_data = m_read.m_data = def;
    }
};

/// Network message for delegated creates
struct DelegateMessage
{
    SInteger  start;
    SInteger  limit;
    SInteger  step;
    TSize     blockSize;
    bool      exclusive;
	MemAddr   address;
	struct {
	    GPID pid;
	    LFID fid;
    }         parent;
    RegsNo    regsNo[NUM_REG_TYPES];
};

/// Network message for group creates
struct CreateMessage
{
    LFID      first_fid;            ///< FID of the family on the creating CPU
    LFID      link_prev;            ///< FID to use for the next CPU's family's link_prev
    bool      infinite;             ///< Infinite create?
	int64_t   start;                ///< Index start
	int64_t   step;                 ///< Index step size
	uint64_t  nThreads;             ///< Number of threads in the family
	uint64_t  virtBlockSize;        ///< Virtual block size
	TSize     physBlockSize;        ///< Physical block size
	MemAddr   address;			    ///< Initial address of new threads
    struct {
        GPID gpid;
        LPID lpid;
        TID  tid;
    } parent;                       ///< Parent Thread ID
    RegsNo    regsNo[NUM_REG_TYPES];///< Register counts
};

class Network : public IComponent
{
public:
    struct RegisterRequest
    {
        RemoteRegAddr addr;         ///< Address of the register to read
        GPID          return_pid;   ///< Address of the core to send to (delegated requests only)
        LFID          return_fid;   ///< FID of the family on the next core to write back to
    };
    
    struct RegisterResponse
    {
        RemoteRegAddr addr;  ///< Address of the register to write
        RegValue      value; ///< Value, in case of response
    };

    Network(Processor& parent, const std::string& name, const std::vector<Processor*>& grid, LPID lpid, Allocator& allocator, RegisterFile& regFile, FamilyTable& familyTable);
    void Initialize(Network& prev, Network& next);

    bool SendGroupCreate(LFID fid);
    bool SendDelegatedCreate(LFID fid);
    bool RequestToken();
    bool SendThreadCleanup(LFID fid);
    bool SendThreadCompletion(LFID fid);
    bool SendFamilySynchronization(LFID fid);
    bool SendFamilyTermination(LFID fid);
    bool SendRemoteSync(GPID pid, LFID fid, ExitCode code);
    
    bool SendRegister   (const RemoteRegAddr& addr, const RegValue& value);
    bool RequestRegister(const RemoteRegAddr& addr, LFID fid_self);
    
private:
    bool SetupFamilyNextLink(LFID fid, LFID link_next);
    bool OnGroupCreateReceived(const CreateMessage& msg);
    bool OnDelegationCreateReceived(const DelegateMessage& msg);
    bool OnRemoteTokenRequested();
    bool OnTokenReceived();
    bool OnThreadCleanedUp(LFID fid);
    bool OnThreadCompleted(LFID fid);
    bool OnFamilySynchronized(LFID fid);
    bool OnFamilyTerminated(LFID fid);
    bool OnRemoteSyncReceived(LFID fid, ExitCode code);
    bool OnRegisterRequested(const RegisterRequest& request);
    bool OnRegisterReceived (const RegisterResponse& response);
    bool OnRemoteRegisterRequested(const RegisterRequest& request);
    bool OnRemoteRegisterReceived(const RegisterResponse& response);
    bool ReadRegister(const RegisterRequest& request);

    Result OnCycleReadPhase(unsigned int stateIndex);
    Result OnCycleWritePhase(unsigned int stateIndex);

    Processor&                     m_parent;
    RegisterFile&                  m_regFile;
    FamilyTable&                   m_familyTable;
    Allocator&                     m_allocator;
    Network*                       m_prev;
    Network*                       m_next;
    LPID                           m_lpid;
    const std::vector<Processor*>& m_grid;

public:
	struct RemoteSync
	{
	    GPID     pid;
	    LFID     fid;
	    ExitCode code;
	};
	
	template <typename T>
	struct RegisterPair
	{
	    Register<T> out;  ///< Register for outgoing messages
	    Register<T> in;   ///< Register for incoming messages

        RegisterPair(const Object& object, const std::string& name)
            : out(object, name + ".out"), in(object, name + ".in") {}
	};

	// Group creates
    Register<CreateMessage>   m_createLocal;    ///< Outgoing group create
	Register<CreateMessage>   m_createRemote;   ///< Incoming group create

    // Delegation creates
    Register<std::pair<GPID, DelegateMessage> > m_delegateLocal;  ///< Outgoing delegation create
	Register<DelegateMessage>                   m_delegateRemote; ///< Incoming delegation create

	// Notifications
    Register<LFID>       m_synchronizedFamily; ///< Outgoing 'family synchronized' notification
    Register<LFID>       m_terminatedFamily;   ///< Outgoing 'family terminated' notification
	
    Register<LFID>       m_completedThread;     ///< Incoming 'thread completed' notification
    Register<LFID>       m_cleanedUpThread;     ///< Incoming 'thread cleaned up' notification
    Register<RemoteSync> m_remoteSync;          ///< Incoming remote synchronization

	// Register communication
	RegisterPair<RegisterRequest>  m_registerRequestRemote;  ///< Remote register request
	RegisterPair<RegisterResponse> m_registerResponseRemote; ///< Remote register response
    RegisterPair<RegisterRequest>  m_registerRequestGroup;   ///< Group register request
    RegisterPair<RegisterResponse> m_registerResponseGroup;  ///< Group register response
    RegValue                       m_registerValue;          ///< Value of incoming register request

	// Token management
    Register<bool> m_hasToken;		 ///< We have the token
    Register<bool> m_wantToken;		 ///< We want the token
    Register<bool> m_tokenUsed;      ///< Has the token been used locally?
    Register<bool> m_nextWantsToken; ///< Next processor wants the token
	Register<bool> m_requestedToken; ///< We've requested the token

};

}
#endif

