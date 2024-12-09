/*
 *  wif.cpp
 *  DrawBoy
 */

/* Copyright (c) 2024 John Horigan <john@glyphic.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */


#include "wif.h"
#include <cstdio>
#include <strings.h>
#include <cctype>
#include <iostream>
#include <string>
#include <exception>

namespace  {
    bool
    valueToBool(const std::string& v)
    {
        if (strcasecmp(v.c_str(), "true") == 0) return true;
        if (strcasecmp(v.c_str(), "on") == 0) return true;
        if (strcmp(v.c_str(), "1") == 0) return true;
        if (strcasecmp(v.c_str(), "yes") == 0) return true;
        return false;
    }

    int
    valueToInt(const std::string& v, int def)
    {
        try {
            return std::stoi(v);
        } catch (std::logic_error&) {
            return def;
        }
    }

    std::string
    valueStripWhite(const std::string& v)
    {
        std::string ret = v;
        while (auto p = ret.find_first_of(" \t\n\r\f\v") != std::string::npos)
            ret.erase(p);
        return ret;
    }
}

wif::wif(FILE* _wifstream)
: wifstream(_wifstream)
{
    if (!seekSection("WIF"))
        throw std::runtime_error("Error in wif file: no WIF section");
    if (!readSection("CONTENTS"))
        throw std::runtime_error("Error in wif file: no CONTENTS section");
    
    auto f = nameKeys.find("tieup");
    if (f != nameKeys.end()) hasTieUp = valueToBool(f->second);
    f = nameKeys.find("treadling");
    if (f != nameKeys.end()) hasTreadling = valueToBool(f->second);
    f = nameKeys.find("liftplan");
    if (f != nameKeys.end()) hasLiftplan = valueToBool(f->second);
    
    if (!hasTreadling && !hasLiftplan)
        throw std::runtime_error("Error in wif file: no treadling or liftplan");
    if (!hasLiftplan && hasTreadling && !hasTieUp)
        throw std::runtime_error("Error in wif file: has treadling but no tie-up");
    if (hasTreadling && hasLiftplan)
        std::cerr << "Issue in wif file: has treadling and liftplan, using liftplan." << std::endl;
    
    if (!readSection("WEAVING"))
        throw std::runtime_error("Error in wif file: no WEAVING section");
    
    f = nameKeys.find("rising shed");
    if (f != nameKeys.end()) {
        risingShed = valueToBool(f->second);
    } else {
        std::cerr << "Wif file does not specify rising/falling shed. Assuming rising shed." << std::endl;
    }
    f = nameKeys.find("shafts");
    if (f == nameKeys.end())
        throw std::runtime_error("Error in wif file: Shafts key missing");
    maxShafts = valueToInt(f->second, 0);
    if (maxShafts < 1 || maxShafts > 40)
        throw std::runtime_error("Error in wif file: Shafts key illegal value");
    f = nameKeys.find("treadles");
    if (f == nameKeys.end())
        throw std::runtime_error("Error in wif file: Treadles key missing");
    maxTreadles = valueToInt(f->second, 0);
    if (maxTreadles < 1 || maxTreadles > 64)
        throw std::runtime_error("Error in wif file: Treadles key illegal value");
    
    if (!readSection("WARP"))
        throw std::runtime_error("Error in wif file: no WARP section");
    
    f = nameKeys.find("threads");
    if (f == nameKeys.end())
        throw std::runtime_error("Error in wif file: Threads key missing from WARP section");
    ends = valueToInt(f->second, 0);
    if (ends == 0)
        throw std::runtime_error("Error in wif file: Threads key illegal value in WARP section");

    if (!readSection("WEFT"))
        throw std::runtime_error("Error in wif file: no WEFT section");
    
    f = nameKeys.find("threads");
    if (f == nameKeys.end())
        throw std::runtime_error("Error in wif file: Threads key missing from WEFT section");
    picks = valueToInt(f->second, 0);
    if (picks == 0)
        throw std::runtime_error("Error in wif file: Threads key illegal value in WEFT section");

    if (!readSection("THREADING"))
        throw std::runtime_error("Error in wif file: THREADING section missing");
    if (!nameKeys.empty())
        std::cerr << "Issue in wif file: spurious named keys in THREADING." << std::endl;
    if (numberKeys.empty())
        throw std::runtime_error("Error in wif file: THREADING has no key lines");
    
    if (numberKeys.size() > ends + 1)
        std::cerr << "Extraneous ends found in THREADING section, discarded." << std::endl;
    numberKeys.resize(ends + 1);
    threading = processKeyLines(false);

    if (hasLiftplan) {
        if (!readSection("LIFTPLAN"))
            throw std::runtime_error("Error in wif file: LIFTPLAN section missing");
        if (!nameKeys.empty())
            std::cerr << "Issue in wif file: spurious named keys in LIFTPLAN." << std::endl;
        if (numberKeys.empty())
            throw std::runtime_error("Error in wif file: LIFTPLAN has no key lines");
        
        if (numberKeys.size() > picks + 1)
            std::cerr << "Extraneous picks found in LIFTPLAN section, discarded." << std::endl;
        numberKeys.resize(picks + 1);
        liftplan = processKeyLines(true);
    } else {
        if (!readSection("TIEUP"))
            throw std::runtime_error("Error in wif file: TIEUP section missing");
        if (!nameKeys.empty())
            std::cerr << "Issue in wif file: spurious named keys in TIEUP." << std::endl;
        if (numberKeys.empty())
            throw std::runtime_error("Error in wif file: TIEUP has no key lines");
        
        if (numberKeys.size() > maxTreadles + 1)
            std::cerr << "Extraneous treadles found in TIEUP section, discarded." << std::endl;
        numberKeys.resize(maxTreadles + 1);
        tieup = processKeyLines(true);
        
        if (!readSection("TREADLING"))
            throw std::runtime_error("Error in wif file: TREADLING section missing");
        if (!nameKeys.empty())
            std::cerr << "Issue in wif file: spurious named keys in TREADLING." << std::endl;
        if (numberKeys.empty())
            throw std::runtime_error("Error in wif file: TREADLING has no key lines");
        
        treadling.resize(picks + 1);
        liftplan.resize(picks + 1, 0);
        if (numberKeys.size() > picks + 1)
            std::cerr << "Extraneous treadlings found in TREADLING section, discarded." << std::endl;
        numberKeys.resize(picks + 1);
        for (int i = 1; i <= picks; ++i) {
            treadling[i] = valueStripWhite(numberKeys[i]);
            if (treadling[i].empty()) continue;
            try {
                const char* v = treadling[i].c_str();
                for (;;) {
                    char* end = nullptr;
                    long treadle = std::strtol(v, &end, 10);
                    if (treadle < 1 || treadle > maxTreadles)
                        throw std::runtime_error("Error in wif file: treadle number out of range in liftplan");
                    
                    liftplan[i] |= tieup[treadle];
                    if (*end != ',') break;
                    v = end + 1;
                }
            } catch (std::logic_error&) {
                throw std::runtime_error("Error in wif file: bad treadle number in liftplan");
            }
        }
    }
}

bool
wif::seekSection(const char* name)
{
    std::rewind(wifstream);
    char buf[128];
    std::size_t len = std::strlen(name);
    
    while (fgets(buf, 128, wifstream) != nullptr) {
        if (buf[0] == '[' && buf[len + 1] == ']' && strncasecmp(buf + 1, name, len) == 0)
            return true;
    }
    return false;
}

bool
wif::readSection(const char *name)
{
    if (!seekSection(name))
        return false;
    
    nameKeys.clear();
    numberKeys.clear();
    minKey.reset();
    maxKey.reset();
    
    char buf[128];
    std::string line;
    for (;;) {
        line.clear();
        for (;;) {  // read into line until EOL or EOF
            if (std::fgets(buf, 128, wifstream) == nullptr) break;
            if (buf[0] == '\0') break;
            line.append(buf);
            if (line.ends_with("\\\n")) {   // if continuation then keep going
                line.pop_back();
                line.pop_back();
                continue;
            }
            if (line.back() == '\n') break;
        }
        if (line.starts_with('[')) break;     // hit next section
        
        line.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
        line.erase(line.find_last_not_of(" \t\n\r\f\v") + 1);
        if (line.empty()) break;
        if (line[0] == ';') continue;
        
        std::size_t eqpos = line.find('=');
        if (eqpos == std::string::npos || eqpos == 0) {
            line.insert(0, "Error in wif file: ");
            throw std::runtime_error(line);
        }
        std::string value(line.begin() + eqpos + 1, line.end());
        value.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
        if (!value.empty() && value.front() == ';') value.clear();
        
        std::size_t digpos = 0;
        while (std::isdigit(+line[digpos])) ++digpos;
        
        if (digpos > 0) {
            if (digpos != eqpos && std::isprint(+line[digpos])) {
                line.insert(0, "Error in wif file: ");
                throw std::runtime_error(line);
            }
            try {
                int i = std::stoi(line);
                if (i < 1) {
                    line.insert(0, "Error in wif file: ");
                    throw std::runtime_error(line);
                }
                if (!minKey.has_value() || i < *minKey) minKey = i;
                if (!maxKey.has_value() || i > *maxKey) maxKey = i;
                if (i >= numberKeys.size())
                    numberKeys.resize(i + 1);
                numberKeys[i] = std::move(value);
            } catch (std::logic_error&) {
                line.insert(0, "Error in wif file: ");
                throw std::runtime_error(line);
            }
        } else {
            std::string key(line.begin(), line.begin() + eqpos);
            key.erase(key.find_last_not_of(" \t\n\r\f\v") + 1);
            if (key.empty()) {
                line.insert(0, "Error in wif file: ");
                throw std::runtime_error(line);
            }
            for (char& c: key)
                c = std::tolower(+c);
            auto there = nameKeys.try_emplace(std::move(key), std::move(value));
            if (!there.second) {
                std::cerr << "Duplicate key in wif section, ignoring: " << line << std::endl;
            }
        }
    }
    return true;
}

std::vector<std::uint64_t>
wif::processKeyLines(bool multi)
{
    std::vector<std::uint64_t> keyLines(numberKeys.size(), 0);
    for (int i = 1; i < numberKeys.size(); ++i) {
        std::string shafts = valueStripWhite(numberKeys[i]);
        if (shafts.empty()) continue;
        try {
            const char* v = shafts.c_str();
            for (;;) {
                char* end = nullptr;
                long shaft = std::strtol(v, &end, 10);
                if (shaft < 1 || shaft > maxShafts)
                    throw std::runtime_error("Error in wif file: shaft number out of range in liftplan");
                keyLines[i] |= 1 << (shaft - 1);
                if (*end == ',' && !multi)
                    throw std::runtime_error("Drawboy doesn't handle ends with multiple shafts");
                if (*end != ',') break;
                v = end + 1;
            }
        } catch (std::logic_error&) {
            throw std::runtime_error("Error in wif file: bad shaft number in liftplan");
        }
    }

    return keyLines;
}
