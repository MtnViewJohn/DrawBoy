/*
 *  wif.cpp
 *  DrawBoy
 */



#include "wif.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <iostream>
#include <string>
#include <exception>
#include <system_error>
#include <climits>
#include <cstdlib>

namespace  {
    bool
    valueToBool(const std::string& v)
    {
        if (v.empty()) throw std::runtime_error("Bad boolean value in wif file");
        if (::strncasecmp(v.c_str(), "true", 4) == 0) return true;
        if (::strncasecmp(v.c_str(), "on", 2) == 0) return true;
        if (v.front() == '1') return true;
        if (::strncasecmp(v.c_str(), "yes", 3) == 0) return true;
        if (::strncasecmp(v.c_str(), "false", 5) == 0) return false;
        if (::strncasecmp(v.c_str(), "off", 3) == 0) return false;
        if (v.front() == '0') return false;
        if (::strncasecmp(v.c_str(), "no", 2) == 0) return false;
        throw std::runtime_error("Bad boolean value in wif file");
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

    std::pair<int,int>
    valueToIntPair(const std::string& v, std::pair<int,int> def)
    {
        char *end;
        std::pair<int,int> ret;
        errno = 0;
        ret.first = (int)std::strtol(v.c_str(), &end, 10);
        if (errno || *end != ',' || *(end + 1) == '\0')
            return def;
        ret.second = (int)std::strtol(end + 1, &end, 10);
        if (errno)
            return def;
        return ret;
    }

    color::tupple3
    valueToInt3(const std::string& v, color::tupple3 def)
    {
        char *end;
        color::tupple3 ret;
        errno = 0;
        std::get<0>(ret) = (int)std::strtol(v.c_str(), &end, 10);
        if (errno || *end != ',' || *(end + 1) == '\0')
            return def;
        std::get<1>(ret) = (int)std::strtol(end + 1, &end, 10);
        if (errno || *end != ',' || *(end + 1) == '\0')
            return def;
        std::get<2>(ret) = (int)std::strtol(end + 1, &end, 10);
        if (errno)
            return def;
        return ret;
    }

    std::string
    valueStripWhite(const std::string& v)
    {
        std::string ret = v;
        while (auto p = ret.find_first_of(" \t\n\r\f\v") != std::string::npos)
            ret.erase(p);
        return ret;
    }

    std::runtime_error
    annotated_runtime_error(const char* desc, std::string& line)
    {
        line.insert(0, desc);
        return std::runtime_error(line);
    }


}

wif::wif(FILE* _wifstream)
: wifstream(_wifstream)
{
    if (!seekSection("WIF"))
        throw std::runtime_error("Error in wif file: no WIF section");
    if (!readSection("CONTENTS", 0, ""))
        throw std::runtime_error("Error in wif file: no CONTENTS section");
    
    auto f = nameKeys.begin();
    auto nkEnd = nameKeys.end();

    bool hasTieUp = false;
    bool hasTreadling = false;
    bool hasLiftplan = false;
    
    if ((f = nameKeys.find("tieup")) !=     nkEnd) hasTieUp = valueToBool(f->second);
    if ((f = nameKeys.find("treadling")) != nkEnd) hasTreadling = valueToBool(f->second);
    if ((f = nameKeys.find("liftplan")) !=  nkEnd) hasLiftplan = valueToBool(f->second);
    
    if (!hasTreadling && !hasLiftplan)
        throw std::runtime_error("Error in wif file: no treadling or liftplan");
    if (!hasLiftplan && hasTreadling && !hasTieUp)
        throw std::runtime_error("Error in wif file: has treadling but no tie-up");
    if (hasTreadling && hasLiftplan)
        std::cerr << "Issue in wif file: has treadling and liftplan, using liftplan." << std::endl;
    
    if (!readSection("WEAVING", 0, ""))
        throw std::runtime_error("Error in wif file: no WEAVING section");
    nkEnd = nameKeys.end();
    
    if ((f = nameKeys.find("rising shed")) != nkEnd)
        risingShed = valueToBool(f->second);
    else
        std::cerr << "Wif file does not specify rising/falling shed. Assuming rising shed." << std::endl;

    if ((f = nameKeys.find("shafts")) != nkEnd)
        maxShafts = valueToInt(f->second, 0);
    else
        throw std::runtime_error("Error in wif file: Shafts key missing");
    
    if (maxShafts < 1 || maxShafts > 40)
        throw annotated_runtime_error("Error in wif file, Shafts key illegal value: ", f->second);
    
    if ((f = nameKeys.find("treadles")) != nkEnd)
        maxTreadles = valueToInt(f->second, 0);
    else
        throw std::runtime_error("Error in wif file: Treadles key missing");
    
    if (maxTreadles < 1 || maxTreadles > 64)
        throw annotated_runtime_error("Error in wif file, Treadles key illegal value: ", f->second);
    
    if (!readSection("WARP", 0, ""))
        throw std::runtime_error("Error in wif file: no WARP section");
    nkEnd = nameKeys.end();

    if ((f = nameKeys.find("threads")) != nkEnd)
        ends = valueToInt(f->second, 0);
    else
        throw std::runtime_error("Error in wif file: Threads key missing from WARP section");
    if (ends <= 0)
        throw annotated_runtime_error("Error in wif file: Threads key illegal value in WARP section", f->second);
    
    size_t defWarpColor = 1;
    if ((f = nameKeys.find("color")) != nkEnd)
        defWarpColor = (size_t)valueToInt(f->second, 1);
    else
        std::cerr << "Wif file does not specify default warp color, using 1." << std::endl;

    if (!readSection("WEFT", 0, ""))
        throw std::runtime_error("Error in wif file: no WEFT section");
    nkEnd = nameKeys.end();

    if ((f = nameKeys.find("threads")) != nkEnd)
        picks = valueToInt(f->second, 0);
    else
        throw std::runtime_error("Error in wif file: Threads key missing from WEFT section");

    if (picks <= 0)
        throw annotated_runtime_error("Error in wif file, Threads key illegal value in WEFT section", f->second);

    size_t defWeftColor = 2;
    if ((f = nameKeys.find("color")) != nkEnd)
        defWeftColor = (size_t)valueToInt(f->second, 1);
    else
        std::cerr << "Wif file does not specify default weft color, using 2." << std::endl;

    std::vector<color> palette;
    palette.push_back({0.0,0.0,0.0});   // color 0 is unused
    if (!readSection("COLOR PALETTE", 0, "")) {
        std::cerr << "Wif file does not specify color palette. Using default." << std::endl;
        palette.push_back(color({255, 255, 255}, {0, 255}));
        palette.push_back(color({0, 0, 255}, {0, 255}));
    } else {
        nkEnd = nameKeys.end();
        std::pair<int,int> range;
        size_t colors;
        if ((f = nameKeys.find("entries")) != nkEnd)
            colors = (size_t)valueToInt(f->second, 2);
        else
            throw std::runtime_error("Error in wif file: Entries key missing from COLOR PALETTE section");
        
        if ((f = nameKeys.find("range")) != nkEnd)
            range = valueToIntPair(f->second, {0, 255});
        else
            throw std::runtime_error("Error in wif file: Range key missing from COLOR PALETTE section");

        palette.resize(colors + 1, {0.0,0.0,0.0});
        
        // Read the color table, but fail if any are missing or malformed
        if (!readSection("COLOR TABLE", (int)colors, "illegal"))
            throw std::runtime_error("Error in wif file: no COLOR TABLE section");

        for (size_t i = 1; i <= colors; ++i) {
            color::tupple3 c = valueToInt3(numberKeys[i], {INT_MAX, INT_MAX, INT_MAX});
            palette[i] = color(c, range);
        }
    }
    
    if (readSection("WARP COLORS", ends, ""))
        warpColor = processColorLines(palette, defWarpColor);
    else
        warpColor.resize((size_t)ends + 1, palette[defWarpColor]);

    if (readSection("WEFT COLORS", picks, ""))
        weftColor = processColorLines(palette, defWeftColor);
    else
        weftColor.resize((size_t)picks + 1, palette[defWeftColor]);

    if (!readSection("THREADING", ends, ""))
        throw std::runtime_error("Error in wif file: THREADING section missing");
    if (!nameKeys.empty())
        std::cerr << "Issue in wif file: spurious named keys in THREADING." << std::endl;
    
    threading = processKeyLines(false);

    if (hasLiftplan) {
        if (!readSection("LIFTPLAN", picks, ""))
            throw std::runtime_error("Error in wif file: LIFTPLAN section missing");
        if (!nameKeys.empty())
            std::cerr << "Issue in wif file: spurious named keys in LIFTPLAN." << std::endl;
        if (numberKeys.empty())
            throw std::runtime_error("Error in wif file: LIFTPLAN has no key lines");
        
        liftplan = processKeyLines(true);
    } else {
        if (!readSection("TIEUP", maxTreadles, ""))
            throw std::runtime_error("Error in wif file: TIEUP section missing");
        if (!nameKeys.empty())
            std::cerr << "Issue in wif file: spurious named keys in TIEUP." << std::endl;
        
        tieup = processKeyLines(true);
        
        if (!readSection("TREADLING", picks, ""))
            throw std::runtime_error("Error in wif file: TREADLING section missing");
        if (!nameKeys.empty())
            std::cerr << "Issue in wif file: spurious named keys in TREADLING." << std::endl;
        
        treadling.resize((size_t)picks + 1);
        liftplan.resize((size_t)picks + 1, 0);
        bool extraTreadle = false;
        for (size_t i = 1; i <= (size_t)picks; ++i) {
            treadling[i] = valueStripWhite(numberKeys[i]);
            if (treadling[i].empty()) continue;
            const char* v = treadling[i].c_str();
            for (;;) {
                char* end = nullptr;
                errno = 0;
                long treadle = std::strtol(v, &end, 10);
                if (errno)
                    throw annotated_runtime_error("Error in wif file, bad treadle number in liftplan: ", treadling[i]);
                while (*end == ' ') ++end;      // consume trailing whitespace

                if (treadle >= 1 && treadle <= maxTreadles)
                    liftplan[i] |= tieup[(size_t)treadle];
                else
                    extraTreadle = true;
                if (*end != ',') break;
                v = end + 1;
            }
        }
        if (extraTreadle)
            std::cerr << "Ignoring extra treadles." << std::endl;
    }
}

bool
wif::seekSection(const char* name)
{
    std::rewind(wifstream);
    size_t nameLen = std::strlen(name);
    malloc_holder mh(1024);
    
    while (::getline(&mh.buffer, &mh.capacity, wifstream) >= 0)
    {
        if (std::strlen(mh.buffer) >= nameLen + 2 && mh.buffer[0] == '[' &&
            ::strncasecmp(mh.buffer + 1, name, nameLen) == 0
            && mh.buffer[nameLen + 1] == ']')
        {
            return true;
        }
    }
    return false;
}

bool
wif::readSection(const char* name, int numlines, const std::string& defValue)
{
    if (!seekSection(name))
        return false;
    
    nameKeys.clear();
    numberKeys.clear();
    numberKeys.resize((size_t)numlines + 1, defValue);
    
    malloc_holder mh(1024);
    
    std::string line;
    for (;;) {
        line.clear();
        for (;;) {
            ssize_t len = ::getline(&mh.buffer, &mh.capacity, wifstream);
            if (len < 0) {
                if (std::ferror(wifstream))
                    throw std::system_error(errno, std::generic_category(), "Error reading wif file");
                break;
            }
            line.append(mh.buffer);
            if (line.ends_with("\\\n")) {   // if continuation then keep going
                line.pop_back();
                line.pop_back();
                continue;
            }
            if (line.back() == '\\') {
                line.pop_back();
                break;          // if continuation at EOF (!?!?) stop
            }
            break;
        }
        if (line.starts_with('[')) break;       // hit next section
        
        line.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
        line.erase(line.find_last_not_of(" \t\n\r\f\v") + 1);
        if (line.empty()) break;                // hit end of section
        if (line.front() == ';') continue;           // hit comment
        
        size_t eqpos = line.find('=');
        if (eqpos == std::string::npos || eqpos == 0 || eqpos == line.length() - 1)
            throw annotated_runtime_error("Error in wif file: ", line);
        std::string value = line.substr(eqpos + 1);
        value.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
        if (!value.empty() && value.front() == ';') value.clear();
        
        size_t digpos = 0;
        while (std::isdigit(+line[digpos])) ++digpos;
        
        if (digpos > 0) {
            if (digpos != eqpos && std::isprint(+line[digpos]))
                throw annotated_runtime_error("Error in wif file: ", line);
            try {
                size_t i = (size_t)std::stoi(line);
                if (i < 1)
                    throw annotated_runtime_error("Error in wif file: ", line);
                if (i < numberKeys.size())
                    numberKeys[i] = std::move(value);
                else
                    std::cerr << "Extra keyline in section " << name << std::endl;
            } catch (std::logic_error&) {
                throw annotated_runtime_error("Error in wif file: ", line);
            }
        } else {
            std::string key = line.substr(0, eqpos);
            key.erase(key.find_last_not_of(" \t\n\r\f\v") + 1);
            if (key.empty())
                throw annotated_runtime_error("Error in wif file: ", line);
            for (char& c: key)
                c = (char)std::tolower(+c);
            auto there = nameKeys.try_emplace(std::move(key), std::move(value));
            if (!there.second) {
                std::cerr << "Duplicate key in wif section, ignoring: " << line << std::endl;
            }
        }
    }
    return true;
}

std::vector<uint64_t>
wif::processKeyLines(bool multi)
{
    bool extraShafts = false;
    std::vector<uint64_t> keyLines(numberKeys.size(), 0);
    for (size_t i = 1; i < numberKeys.size(); ++i) {
        std::string shafts = valueStripWhite(numberKeys[i]);
        if (shafts.empty()) continue;
        const char* v = shafts.c_str();
        for (;;) {
            char* end = nullptr;
            errno = 0;
            long shaft = std::strtol(v, &end, 10);
            if (errno)
                throw std::runtime_error("Error in wif file: bad shaft number in liftplan");
            while (*end == ' ') ++end;      // consume trailing whitespace
            if (*end == ',' && !multi)
                throw std::runtime_error("Drawboy doesn't handle ends with multiple shafts");
            if (shaft >= 1 && shaft <= maxShafts)
                keyLines[i] |= 1ull << (shaft - 1);
            else
                extraShafts = true;
            if (*end != ',') break;
            v = end + 1;
        }
    }

    if (extraShafts)
        std::cerr << "Ignoring extra shafts." << std::endl;

    return keyLines;
}

std::vector<color>
wif::processColorLines(const std::vector<color>& palette, size_t def)
{
    std::vector<color> colors(numberKeys.size() + 1, palette[def]);
    for (size_t i = 1; i < numberKeys.size(); ++i) {
        auto keyLine = (size_t)valueToInt(numberKeys[i], (int)def);
        colors[i] = palette[keyLine];
    }
    return colors;
}
