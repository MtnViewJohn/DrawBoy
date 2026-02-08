//
//  dtx.cpp
//  DrawBoy
//
//  Created by John Horigan on 1/22/25.
//

#include "dtx.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <map>
#include <sstream>
#include <bit>
#include <cctype>

namespace {
std::string_view currentline(std::string& line)
{
    std::string_view str(line);
    while (!str.empty() && std::isspace(str.back()))
        str.remove_suffix(1);
    while (!str.empty() && std::isspace(str.front()))
        str.remove_prefix(1);
    return str;
}

bool
seekSection(std::ifstream& dtxstream, const char* name)
{
    dtxstream.clear();
    dtxstream.seekg(0);
    size_t nameLen = std::strlen(name);
    
    for (std::string _line; std::getline(dtxstream, _line);) {
        auto line = currentline(_line);
        if (line.length() == nameLen + 2 &&
            line.starts_with("@@") &&
            line.ends_with(name))
        {
            return true;
        }
    }
    return false;
}

std::set<std::string>
readContentsToSet(std::ifstream& dtxstream)
{
    std::set<std::string> contents;
    if (!seekSection(dtxstream, "Contents"))
        return contents;
    
    for (std::string _line; std::getline(dtxstream, _line);) {
        auto line = currentline(_line);
        if (line.length() == 0) break;
        if (line.starts_with("@@")) break;
        contents.insert(std::string(line));
    }
    
    return contents;
}

std::map<std::string, int>
readInfoToMap(std::ifstream& dtxstream)
{
    std::map<std::string, int> infomap;
    if (!seekSection(dtxstream, "Info"))
        return infomap;
    
    for (std::string _line; std::getline(dtxstream, _line);) {
        auto line = currentline(_line);
        if (line.length() == 0) break;
        if (line.starts_with("@@")) break;

        auto space = line.find(' ');
        if (line.starts_with("%%") && space != std::string_view::npos) {
            std::string var(line.begin() + 2, line.begin() + space);
            infomap[var] = (int)std::strtol(line.begin() + space, nullptr, 10);
        } else {
            throw std::runtime_error("Error in dtx file: parse error in Info section.");
        }
    }
    return infomap;
}

std::vector<color>
ReadColorPalettte(std::ifstream& dtxstream)
{
    std::vector<color> palette;
    if (seekSection(dtxstream, "Color Palet")) {
        for (std::string _line; std::getline(dtxstream, _line);) {
            auto line = currentline(_line);
            if (line.length() == 0) break;
            if (line.starts_with("@@")) break;

            char* end = nullptr;
            errno = 0;
            int red = (int)std::strtol(line.data(), &end, 10);
            if (!errno && *end == ',') {
                int green = (int)std::strtol(end + 1, &end, 10);
                if (!errno && *end == ',') {
                    int blue = (int)std::strtol(end + 1, &end, 10);
                    if (!errno && end == line.end()) {
                        palette.push_back(color({red, green, blue}, {0, 255}));
                        continue;
                    }
                }
            }
            throw std::runtime_error("Error in dtx file: parse error in color palette.");
        }
    }
    
    return palette;
}

std::vector<color>
readColorSection(std::ifstream& dtxstream, const char* name, const std::vector<color>& palette)
{
    std::vector<color> colors;
    if (!seekSection(dtxstream, name))
        return colors;
    colors.push_back({});      // 1-based array
    
    for (std::string _line; std::getline(dtxstream, _line);) {
        auto line = currentline(_line);
        if (line.length() == 0) break;
        if (line.starts_with("@@")) break;

        char* end = const_cast<char*>(line.begin());    // bullshit strtol API
        errno = 0;
        while (end != line.end()) {
            size_t v = (size_t)std::strtol(end, &end, 10);
            if (errno)
                throw std::runtime_error("Error in dtx file: parse error in warp/weft color section.");
            if (v >= palette.size())
                throw std::runtime_error("Dtx file contains color outside of the palette.");
            colors.push_back(palette[v]);
        }
    }
    
    return colors;
}

std::vector<uint64_t>
readSectiontoVector(std::ifstream& dtxstream, const char* name)
{
    std::vector<uint64_t> ret;
    if (!seekSection(dtxstream, name))
        return ret;
    
    ret.push_back(0);           // 1-based array

    for (std::string _line; std::getline(dtxstream, _line);) {
        auto line = currentline(_line);
        if (line.length() == 0) break;
        if (line.starts_with("@@")) break;

        while (!line.empty()) {
            auto space = line.find(' ');
            auto term_end = space == std::string_view::npos ? line.end() : line.begin() + space;
            uint64_t v = 0;
            std::basic_istringstream shafts(std::string(line.begin(), term_end));
            for (std::string shaft; std::getline(shafts, shaft, ',');)
                if (shaft != "0")
                    v |= 1ull << (std::stoi(shaft) - 1);
            ret.push_back(v);
            if (space == std::string_view::npos) {
                line.remove_prefix(line.length());
            } else {
                line.remove_prefix(space);
                while (!line.empty() && std::isspace(line.front()))
                    line.remove_prefix(1);
            }
        }
    }
    
    return ret;
}

std::vector<uint64_t>
readTieup(std::ifstream& dtxstream, bool& rising)
{
    std::vector<uint64_t> tieup;
    if (!seekSection(dtxstream, "Tieup"))
        return tieup;
    
    std::vector<std::string> tieupstrings;

    for (std::string _line; std::getline(dtxstream, _line);) {
        auto line = currentline(_line);
        if (line.length() == 0) break;
        if (line.starts_with("@@")) break;
        if (line.compare("%%%%sinking") == 0) {
            rising = false;
            continue;
        }
        tieupstrings.insert(tieupstrings.begin(), std::string(line));
    }
    
    size_t treadles = tieupstrings.front().length();
    size_t shafts = tieupstrings.size();
    tieup.assign(treadles + 1, 0);
    for (size_t treadle = 0; treadle < treadles; ++treadle)
        for (size_t shaft = 0; shaft < shafts; ++shaft)
            if (tieupstrings[shaft][treadle] == '1')
                tieup[treadle + 1] |= 1ull << shaft;
    
    return tieup;
}

std::vector<uint64_t>
readLiftplan(std::ifstream& dtxstream, bool& rising)
{
    std::vector<uint64_t> liftplan;
    if (!seekSection(dtxstream, "Liftplan"))
        return liftplan;
    
    liftplan.push_back(0);           // liftplan is a 1-based array

    for (std::string _line; std::getline(dtxstream, _line);) {
        auto line = currentline(_line);
        if (line.length() == 0) break;
        if (line.starts_with("@@")) break;
        if (line.compare("%%%%sinking") == 0) {
            rising = false;
            continue;
        }
        
        uint64_t lift = 0;
        for (uint64_t shaft = 1; !line.empty(); shaft <<= 1) {
            if (line.front() == '1')
                lift |= shaft;
            line.remove_prefix(1);
        }
        liftplan.push_back(lift);
    }
    
    return liftplan;
}
}

dtx::dtx(std::ifstream& dtxstream)
{
    if (!seekSection(dtxstream, "StartDTX"))
        throw std::runtime_error("Error in dtx file: no StartDTX section.");
    
    auto contents = readContentsToSet(dtxstream);
    if (contents.empty())
        throw std::runtime_error("Error in dtx file: no Contents section.");
    bool hasLiftplan = contents.contains("Liftplan");
    bool hasTreadling = contents.contains("Treadling") && contents.contains("Tieup");
    if (!hasTreadling && !hasLiftplan)
        throw std::runtime_error("Error in dtx file: no treadling/tieup or liftplan");
    if (hasTreadling && hasLiftplan)
        std::cerr << "Issue in dtx file: has treadling and liftplan, using liftplan." << std::endl;
    bool hasColor = contents.contains("Color Palet") &&
                    contents.contains("Warp Colors") &&
                    contents.contains("Weft Colors");
    
    auto info = readInfoToMap(dtxstream);
    if (!info.contains("shafts") || !info.contains("shafts") ||
        !info.contains("shafts") || !info.contains("shafts"))
        throw std::runtime_error("Dtx file missing information.");
    maxShafts = info["shafts"];
    maxTreadles = info["treadles"];
    ends = info["ends"];
    picks = info["picks"];
    
    if (!hasColor) {
        // If the user never touches the color bars then Fiberworks does not
        // generate any color info. The warp is white and the weft is blue.
        warpColor.assign((size_t)ends + 1, color({255, 255, 255}, {0, 255}));
        weftColor.assign((size_t)picks + 1, color({0, 0, 255}, {0, 255}));
    } else {
        auto palette = ReadColorPalettte(dtxstream);
        if (palette.size() < 2)
            throw std::runtime_error("Dtx file is missing a color palette.");
        warpColor = readColorSection(dtxstream, "Warp Colors", palette);
        weftColor = readColorSection(dtxstream, "Weft Colors", palette);
        if (warpColor.size() != (size_t)ends + 1)
            throw std::runtime_error("Dtx file has wrong number of ends in the Warp Color section.");
        if (weftColor.size() != (size_t)picks + 1)
            throw std::runtime_error("Dtx file has wrong number of picks in the Weft Color section.");
    }
    
    threading = readSectiontoVector(dtxstream, "Threading");
    
    if (hasLiftplan) {
        liftplan = readLiftplan(dtxstream, risingShed);
        if (liftplan.size() != (size_t)picks + 1)
            throw std::runtime_error("Dtx file has wrong number of picks in liftplan.");
    } else {
        tieup = readTieup(dtxstream, risingShed);
        if (tieup.size() != (size_t)maxTreadles + 1)
            throw std::runtime_error("Dtx file has wrong number of treadles in tieup.");
        auto treadling = readSectiontoVector(dtxstream, "Treadling");
        if (treadling.size() != (size_t)picks + 1)
            throw std::runtime_error("Dtx file has wrong number of picks in treadling.");
        
        liftplan.reserve((size_t)picks + 1);
        for (auto treadles: treadling) {
            uint64_t lift = 0;
            size_t treadle = 1;
            while (treadles) {
                if (treadles & 1)
                    lift |= tieup[treadle];
                treadles >>= 1;
                ++treadle;
            }
            liftplan.push_back(lift);
        }
    }
}

