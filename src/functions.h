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
#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include "kernel.h"
#include <map>
#include <set>
#include <limits>

namespace Simulator
{

class ArbitratedFunction : public IFunction
{
    typedef std::map<int, const IComponent*> RequestMap;
    typedef std::map<const IComponent*,int>  PriorityMap;

public:
    ArbitratedFunction(const ArbitratedFunction& f)
      : IFunction(), m_priorities(f.m_priorities), m_requests(f.m_requests), m_kernel(f.m_kernel),
          m_priority(f.m_priority), m_component(f.m_component)
    {
        m_kernel.RegisterFunction(*this);
    }

    ArbitratedFunction(Kernel& kernel)
        : m_kernel(kernel), m_priority(std::numeric_limits<int>::max()), m_component(NULL)
    {
        m_kernel.RegisterFunction(*this);
    }

    virtual ~ArbitratedFunction() {
        m_kernel.UnregisterFunction(*this);
    }

    void SetPriority(const IComponent& component, int priority);
    void Arbitrate();

protected:
    bool IsAcquiring() const {
        return m_kernel.GetCyclePhase() == PHASE_ACQUIRE;
    }

    bool HasAcquired(const IComponent& component) const {
        return &component == m_component;
    }

    bool HasAcquired(int priority) const {
        return m_priority == priority;
    }

#ifndef NDEBUG
    void Verify(const IComponent& component) const;
#else
    void Verify(const IComponent& /* component */) const {}
#endif

    void AddRequest(const IComponent& component);
    void AddRequest(int priority);

private:
    PriorityMap m_priorities;
    RequestMap  m_requests;
    Kernel&     m_kernel;
    
    int               m_priority;
    const IComponent* m_component;
};

class DedicatedFunction
{
public:
    DedicatedFunction() : m_component(NULL) {}
    virtual ~DedicatedFunction() {}

    virtual void SetComponent(const IComponent& component) {
        m_component = &component;
    }

protected:
#ifndef NDEBUG
    void Verify(const IComponent& component) const;
#else
    void Verify(const IComponent& /* component */) const {}
#endif

private:
    const IComponent* m_component;
};

//
// Read and Write function
//
class ReadFunction
{
public:
    virtual bool Invoke(const IComponent& component) = 0;
    virtual bool Invoke(int priority) = 0;

    virtual ~ReadFunction() {}
};

class WriteFunction
{
public:
    virtual bool Invoke(const IComponent& component) = 0;
    virtual bool Invoke(int priority) = 0;

    virtual ~WriteFunction() {}
};

//
// Arbitrated Functions
//
class ArbitratedReadFunction : public ReadFunction, public ArbitratedFunction
{
public:
    ArbitratedReadFunction(Kernel& kernel) : ArbitratedFunction(kernel) {}
    void OnArbitrateReadPhase()               { Arbitrate(); }
    void OnArbitrateWritePhase()              {}
    bool Invoke(const IComponent& component)
    {
        Verify(component);
        if (IsAcquiring())
        {
            AddRequest(component);
        }
        else if (!HasAcquired(component))
        {
            return false;
        }
        return true;
    }

    bool Invoke(int priority)
    {
        if (IsAcquiring())
        {
            AddRequest(priority);
        }
        else if (!HasAcquired(priority))
        {
            return false;
        }
        return true;
    }
};

class ArbitratedWriteFunction : public WriteFunction, public ArbitratedFunction
{
public:
    ArbitratedWriteFunction(Kernel& kernel) : ArbitratedFunction(kernel) {}
    void OnArbitrateReadPhase()               {}
    void OnArbitrateWritePhase()              { Arbitrate(); }
    bool Invoke(const IComponent& component)
    {
        Verify(component);
        if (IsAcquiring())
        {
            AddRequest(component);
        }
        else if (!HasAcquired(component))
        {
            return false;
        }
        return true;
    }
    
    bool Invoke(int priority)
    {
        if (IsAcquiring())
        {
            AddRequest(priority);
        }
        else if (!HasAcquired(priority))
        {
            return false;
        }
        return true;
    }
};

//
// Dedicated Functions
//
class DedicatedReadFunction : public ReadFunction, public DedicatedFunction
{
public:
    bool Invoke(const IComponent& component) {
        Verify(component);
        return true;
    }
};

class DedicatedWriteFunction : public WriteFunction, public DedicatedFunction
{
public:
    bool Invoke(const IComponent& component) {
        Verify(component);
        return true;
    }
};

}

#endif

