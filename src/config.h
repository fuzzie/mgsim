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
#ifndef CONFIG_H
#define CONFIG_H

#include <algorithm>
#include <sstream>
#include <map>
#include <string>
#include <vector>
#include <ostream>

class Config
{
    typedef std::map<std::string,std::string> ConfigMap;
    
    ConfigMap        m_data;
    const ConfigMap& m_overrides;

public:
    bool getBoolean(const std::string& name, const bool def) const;
    
    template <typename T>
    T getInteger(const std::string& name, const T& def) const
    {
        T val;
        std::stringstream stream;
        stream << getString(name, "");
        stream >> val;
        return (!stream.fail() && stream.eof()) ? val : def;
    }

    template <typename T>
    std::vector<T> getIntegerList(const std::string& name) const
    {
        std::vector<T> vals;
	std::string str = getString(name,"");
        std::istringstream stream(str);
        std::string token = "";
        while (getline(stream, token, ','))
        {
            std::stringstream ss;
            T val;
            ss << token;
            ss >> val;
            if (ss.fail()) break;
            vals.push_back(val);
        }
        return vals;
    }

    template <typename T>
    T getSize(const std::string& name, const T& def) const
    {
        T val;
        std::string strmult;
        std::stringstream stream;
        stream << getString(name, "");
        stream >> val;
        stream >> strmult;
        if (!strmult.empty())
        {
            if (strmult.size() == 2)
            {
                switch (toupper(strmult[0]))
                {
                    default: return def;
                    case 'G': val = val * 1024;
                    case 'M': val = val * 1024;
                    case 'K': val = val * 1024;
                }                
                strmult = strmult.substr(1);
            }

            if (strmult.size() != 1 || toupper(strmult[0]) != 'B')
            {
                return def;
            }
        }
        return (!stream.fail() && stream.eof()) ? val : def;
    }

    std::string getString(std::string name, const std::string& def) const;

    void dumpConfiguration(std::ostream& os) const;

    Config(const std::string& filename, const ConfigMap& overrides);
};

#endif

