/*
 *  wif.h
 *  DrawBoy
 */

 
#pragma once
#include <cstdio>
#include <map>
#include <vector>
#include "color.h"

class wif {
public:
    wif(FILE* _wifstream);
    
    bool hasTieUp = false;
    bool hasTreadling = false;
    bool hasLiftplan = false;
    
    int  maxShafts;
    int  maxTreadles;
    bool risingShed = true;
    int ends = 0;
    int picks = 0;
    
    std::vector<std::uint64_t> liftplan;
    std::vector<std::uint64_t> tieup;
    std::vector<std::string>   treadling;
    std::vector<std::uint64_t> threading;
    std::vector<color>         warpColor, weftColor;
    
private:
    bool seekSection(const char* name);
    bool readSection(const char* name);
    std::vector<std::uint64_t> processKeyLines(bool multi);
    std::vector<std::size_t> processColorLines(std::size_t colors, std::size_t def);

    FILE* wifstream = nullptr;
    std::map<std::string, std::string> nameKeys;
    std::vector<std::string> numberKeys;
};
