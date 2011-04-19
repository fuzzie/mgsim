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
#include "simreadline.h"
#include <sys/time.h>
#include <sstream>
#include <string>
#include <cstdlib>

#if defined(HAVE_LIBREADLINE) && !defined(HAVE_READLINE_READLINE_H)
extern "C" {
char *cmdline = NULL;
}
#endif

using namespace Simulator;
using namespace std;

int CommandLineReader::ReadLineHook(void) {
#ifdef CHECK_DISPLAY_EVENTS
    if (!m_display) 
        return 0;

    // readline is annoying: the documentation says the event
    // hook is called no more than 10 times per second, but
    // tests show it _can_ be much more often than this. But
    // we don't want to refresh the display or check events
    // too often (it's expensive...) So we keep track of time
    // here as well, and force 10ths of seconds ourselves.
        
    static long last_check = 0;
        
    struct timeval tv;
    gettimeofday(&tv, 0);
    long current = tv.tv_sec * 10 + tv.tv_usec / 100000;
    if (current - last_check)
    {
        m_display->CheckEvents();
        last_check = current;
    }
#endif
    return 0;
}

CommandLineReader::CommandLineReader(Display& d)  {
    m_display = &d;
#ifdef HAVE_LIBREADLINE
    rl_event_hook = &ReadLineHook;
# ifdef HAVE_READLINE_HISTORY
    ostringstream os;
    os << getenv("HOME") << "/.mgsim_history";
    m_histfilename = os.str();
    read_history(m_histfilename.c_str());
# endif
#endif
}

CommandLineReader::~CommandLineReader() {
#ifdef HAVE_LIBREADLINE
    rl_event_hook = 0;
#endif
    m_display = 0;
    CheckPointHistory();
}

char* CommandLineReader::GetCommandLine(const string& prompt)
{
#ifdef HAVE_LIBREADLINE
    char* str = readline(prompt.c_str());
# ifdef HAVE_READLINE_HISTORY
    if (str != NULL && *str != '\0')
    {
        add_history(str);
    }
# endif
#else // !HAVE_LIBREADLINE
    std::string line;

    std::cout << prompt;
    std::cout.flush();

    std::getline(std::cin, line);
    if (std::cin.fail())
        return 0;
    
    char *str = (char*)malloc(line.size() + 1);
    assert(str != 0);
    strcpy(str, line.c_str());
#endif
    return str;
}

void CommandLineReader::CheckPointHistory() {
#if defined(HAVE_LIBREADLINE) && defined(HAVE_READLINE_HISTORY)
    write_history(m_histfilename.c_str());
#endif
}


Display* CommandLineReader::m_display = 0;

vector<string> Tokenize(const string& str, const string& sep)
{
    vector<string> tokens;
    for (size_t next, pos = str.find_first_not_of(sep); pos != string::npos; pos = next)
    {
        next = str.find_first_of(sep, pos);
        if (next == string::npos)
        {
            tokens.push_back(str.substr(pos));  
        }
        else
        {
            tokens.push_back(str.substr(pos, next - pos));
            next = str.find_first_not_of(sep, next);
        }
    }
    return tokens;
}

