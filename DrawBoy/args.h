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
#include <string_view>
#include <memory>

class draft;

struct Options {
    Options(int argc, const char * argv[]);
    ~Options();
    void parsePicks(const std::string& str, int maxPick);
    
    bool driveLoom = true;
    std::string loomDevice;
    int loomDeviceFD = 0;
    std::unique_ptr<draft> draftContents;
    int maxShafts;
    DobbyType dobbyType = DobbyType::Positive;
    int pick;
    std::vector<int> picks;
    color tabbyColor;
    bool ascii;
    ANSIsupport ansi;
    uint64_t tabbyA = 0, tabbyB = 0;
    TabbyPattern tabbyPattern = TabbyPattern::xAyB;
};

