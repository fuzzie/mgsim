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
#include "commands.h"
#include <cstdlib>
#include <cerrno>

using namespace std;

bool cmd_bp_list(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    ctx.sys.GetKernel().GetBreakPoints().ListBreakPoints(cout);
    return false;
}


bool cmd_bp_state(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    ctx.sys.GetKernel().GetBreakPoints().ReportBreaks(cout);
    return false;
}


bool cmd_bp_on(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    ctx.sys.GetKernel().GetBreakPoints().EnableCheck();
    return false;
}


bool cmd_bp_off(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    ctx.sys.GetKernel().GetBreakPoints().DisableCheck();
    return false;
}


bool cmd_bp_clear(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    ctx.sys.GetKernel().GetBreakPoints().ClearAllBreakPoints();
    return false;
}


bool cmd_bp_del(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    errno = 0;
    unsigned id = strtoul(args[0].c_str(), 0, 0);
    if (errno == EINVAL)
        cout << "Invalid breakpoint: " << args[0] << endl;
    else
        ctx.sys.GetKernel().GetBreakPoints().DeleteBreakPoint(id);
    return false;
}


bool cmd_bp_enable(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    errno = 0;
    unsigned id = strtoul(args[0].c_str(), 0, 0);
    if (errno == EINVAL)
        cout << "Invalid breakpoint: " << args[0] << endl;
    else
        ctx.sys.GetKernel().GetBreakPoints().EnableBreakPoint(id);
    return false;
}


bool cmd_bp_disable(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    errno = 0;
    unsigned id = strtoul(args[0].c_str(), 0, 0);
    if (errno == EINVAL)
        cout << "Invalid breakpoint: " << args[0] << endl;
    else
        ctx.sys.GetKernel().GetBreakPoints().DisableBreakPoint(id);
    return false;
}


bool cmd_bp_add(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    int mode = 0;
    for (string::const_iterator i = args[0].begin(); i != args[0].end(); ++i)
    {
        switch(toupper(*i))
        {
        case 'R': mode |= BreakPoints::READ; break;
        case 'W': mode |= BreakPoints::WRITE; break;
        case 'X': mode |= BreakPoints::EXEC; break;
        case 'T': mode |= BreakPoints::TRACEONLY; break;
        default: break;
        }
    }
    if (mode == 0)
        cout << "Invalid breakpoint mode:" << args[0] << endl;
    else
        ctx.sys.GetKernel().GetBreakPoints().AddBreakPoint(args[1], 0, mode);
    return false;
}


