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
#include "wif.h"

struct Options {
    Options(int argc, const char * argv[]);
    ~Options();
    void parsePicks(const std::string& str, int maxPick);
    
    int err = 1;
    std::string loomDevice;
    int loomDeviceFD = 0;
    std::string wifFile;
    wif wifContents;
    int maxShafts;
    DobbyType dobbyType = DobbyType::Positive;
    int pick;
    std::vector<int> picks;
    color tabbyColor;
    bool ascii;
    ANSIsupport ansi;
    uint64_t tabbyA = 0, tabbyB = 0;
};

