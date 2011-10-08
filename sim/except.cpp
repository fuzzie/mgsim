/*
mgsim: Microgrid Simulator
Copyright (C) 2006,2007,2008,2009,2010,2011,2012  The Microgrid Project.

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
#include "except.h"
#include "kernel.h"
#include <algorithm>
#include <iostream>

namespace Simulator
{

static std::string MakeMessage(const Object& object, const std::string& msg)
{
    std::string name = object.GetFQN();
    std::transform(name.begin(), name.end(), name.begin(), toupper);
    return name + ": " + msg;
}

SimulationException::SimulationException(const std::string& msg, const Object& object)
    : std::runtime_error(MakeMessage(object, msg))
{
    
}

SimulationException::SimulationException(const Object& object, const std::string& msg)
    : std::runtime_error(MakeMessage(object, msg))
{
    
}

void PrintException(std::ostream& out, const std::exception& e)
{
    out << std::endl << e.what() << std::endl;

    const SimulationException* se = dynamic_cast<const SimulationException*>(&e);
    if (se != NULL)
    {
        // SimulationExceptions hold more information, print it
        const std::list<std::string>& details = se->GetDetails();
        for (std::list<std::string>::const_iterator p = details.begin(); p != details.end(); ++p)
        {
            out << *p << std::endl;
        }
    }
}



}
