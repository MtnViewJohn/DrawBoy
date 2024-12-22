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
#include "color.h"
#include <climits>

struct Options {
    ~Options();
    int getOptions(int argc, const char * argv[]);
    void parsePicks(const std::string& str, int maxPick = INT_MAX);
    
    bool valid = false;
    std::string loomDevice;
    int loomDeviceFD = 0;
    std::string wifFile;
    FILE* wifFileStream = nullptr;
    int maxShafts;
    DobbyType dobbyType = DobbyType::Positive;
    int pick;
    std::vector<int> picks;
    color tabbyColor;
    bool ascii;
    ANSIsupport ansi;
    uint64_t tabbyA = 0, tabbyB = 0;
};

