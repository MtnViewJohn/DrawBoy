/*
 *  wif.h
 *  DrawBoy
 */

 
#pragma once
#include <cstdio>
#include <map>
#include <vector>
#include "color.h"

using std::size_t;
using std::uint64_t;

class wif {
public:
    wif() = default;

    void readWif(FILE* _wifstream);
    
    
    bool hasTieUp = false;
    bool hasTreadling = false;
    bool hasLiftplan = false;
    
    int  maxShafts;
    int  maxTreadles;
    bool risingShed = true;
    int ends = 0;
    int picks = 0;
    
    std::vector<uint64_t> liftplan;
    std::vector<uint64_t> tieup;
    std::vector<std::string>   treadling;
    std::vector<uint64_t> threading;
    std::vector<color>         warpColor, weftColor;
    
private:
    bool seekSection(const char* name);
    bool readSection(const char* name);
    std::vector<uint64_t> processKeyLines(bool multi);
    std::vector<size_t> processColorLines(size_t colors, size_t def);

    FILE* wifstream = nullptr;
    std::map<std::string, std::string> nameKeys;
    std::vector<std::string> numberKeys;
};
