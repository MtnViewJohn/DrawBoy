/*
 *  args.h
 *  DrawBoy
 */


#pragma once

#include "argscommon.h"
#include <exception>
#include <system_error>
#include <string>
#include <cstdio>
#include <vector>

struct Options {
    ~Options();
    int getOptions(int argc, const char * argv[]);
    void parsePicks(const std::string& str);
    
    bool valid = false;
    std::string loomDevice;
    int loomDeviceFD = 0;
    std::string wifFile;
    FILE* wifFileStream = nullptr;
    int maxShafts;
    DobbyType dobbyType = DobbyType::Positive;
    int pick;
    std::vector<int> picks;
    bool ascii;
    std::uint64_t tabbyA = 0, tabbyB = 0;
};

