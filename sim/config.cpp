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
#include "config.h"
#include "except.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <fnmatch.h>

using namespace std;
using namespace Simulator;

template <>
bool Config::getValue<bool>(const std::string& name, const bool& def)
{
    string val = getValue<std::string>(name, def ? "true" : "false");
    transform(val.begin(), val.end(), val.begin(), ::toupper);

    // Check for the boolean values
    if (val == "TRUE" || val == "YES") return true;
    if (val == "FALSE" || val == "NO") return false;
    
    // Otherwise, try to interpret as an integer
    int i;
    stringstream stream(val);
    stream >> i;
    return (!stream.fail() && stream.eof()) ? i != 0 : def;
}

template <>
std::string Config::getValue<std::string>(const std::string& name_, const std::string& def)
{
    std::string name(name_);
    std::string val, pat;
    transform(name.begin(), name.end(), name.begin(), ::toupper);

    ConfigCache::const_iterator p = m_cache.find(name);
    if (p != m_cache.end())
        return p->second.first;

    bool found = false;
    for (ConfigMap::const_iterator p = m_overrides.begin(); p != m_overrides.end(); ++p)
    {
        pat = p->first;
        // std::cerr << "Lookup " << name << ": checking override " << pat << std::endl;
        if (FNM_NOMATCH != fnmatch(pat.c_str(), name.c_str(), 0))
        {
            // Return the overriden value
            val = p->second;
            found = true;
            break;
        }
    }
    
    if (!found)
    {
        for (ConfigMap::const_iterator p = m_data.begin(); p != m_data.end(); ++p)
        {
            pat = p->first;
            //std::cerr << "Lookup " << name << ": checking config " << pat << std::endl;
            if (FNM_NOMATCH != fnmatch(pat.c_str(), name.c_str(), 0))
            {
                // Return the configuration value
                val = p->second;
                found = true;
                break;
            }
        }
    }
    if (!found)
    {
        pat = "default";
        val = def;
        found = true;
    }

    if (found)
    {
        m_cache[name] = std::make_pair(val, pat);
    }
    return val;
}

void Config::dumpConfiguration(ostream& os, const string& cf) const
{
    os << "### begin simulator configuration" << endl;
    for (ConfigCache::const_iterator p = m_cache.begin(); p != m_cache.end(); ++p)
        os << "# -o " << p->first << " = " << p->second.first << " # set by " << p->second.second << endl;

    os << "### end simulator configuration" << endl;
}

vector<string> Config::getWordList(const string& name)
{
    vector<string> vals;
    istringstream stream(getValue<string>(name,""));
    string token = "";
    while (getline(stream, token, ','))
    {
        vals.push_back(token);
    }
    return vals;
}

vector<string> Config::getWordList(const Simulator::Object& obj, const string& name)
{
    return getWordList(obj.GetFQN() + '.' + name);
}


Config::Config(const string& filename, const ConfigMap& overrides)
    : m_overrides(overrides)
{
    enum State
    {
        STATE_BEGIN,
        STATE_COMMENT,
        STATE_NAME,
        STATE_EQUALS,
        STATE_VALUE,
    };

    State state = STATE_BEGIN;
    string name;
    string value;

    ifstream input(filename.c_str());
    if (!input.is_open())
    {
        throw FileNotFoundException(filename);
    }

    while (!input.eof())
    {
        int c = input.get();
        if (input.fail())
        {
            break;
        }

        if (state == STATE_BEGIN && !isspace(c))
        {
            if (c == '#' || c == ';')
            {
                state = STATE_COMMENT;
            }
            else if (isalpha(c) || c == '_' || c == '*' || c == '.')
            {
                state = STATE_NAME;
                name = (char)c;
            }
        }
        else if (state == STATE_COMMENT)
        {
            if (c == '\n')
            {
                state = STATE_BEGIN;
            }
        }
        else if (state == STATE_NAME)
        {
            if (isalnum(c) || c == '_' || c == '*' || c == '.') name += (char)c;
            else 
            {
                transform(name.begin(), name.end(), name.begin(), ::toupper);
                state = STATE_EQUALS;
            }
        }
        
        if (state == STATE_EQUALS && !isspace(c))
        {
            if (c == '=') state = STATE_VALUE;
        }
        else if (state == STATE_VALUE)
        {
            if (isspace(c) && value.empty())
            {
            }
            else if (c == '\r' || c == '\n' || c == '#')
            {
                if (!value.empty())
                {
                    // Strip off all the spaces from the end
                    string::size_type pos = value.find_last_not_of("\r\n\t\v\f ");
                    if (pos != string::npos) {
                        value.erase(pos + 1);
                    }
                    
                    m_data.push_back(make_pair(name,value));
                    name.clear();
                    value.clear();
                }
                state = (c == '#') ? STATE_COMMENT : STATE_BEGIN;
            }
            else 
            {
                value = value + (char)c;
            }
        }
    }
    
    if (value != "")
    {
        m_data.push_back(make_pair(name,value));
    }
}

