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
#include <cstdio>

class draft;

struct Options {
    Options(int argc, const char * argv[]);
    ~Options();
    void parsePicks(const std::string& str, int maxPick);
    
    bool driveLoom = true;
    int compuDobbyGen;
    bool useNetwork;
    std::string loomDevice;
    std::string loomAddress;
    int loomDeviceFD = 0;
    std::unique_ptr<draft> draftContents;
    int maxShafts;
    DobbyType dobbyType = DobbyType::Unspecified;
    bool virtualPositive = false;
    int pick = 1;
    std::vector<int> picks;
    bool treadleThreading;
    color tabbyColor;
    bool ascii;
    ColorAlert colorAlert;
    ANSIsupport ansi;
    uint64_t tabbyA = 0, tabbyB = 0;
    TabbyPattern tabbyPattern = TabbyPattern::xAyB;
    std::string pickFile;
    FILE* logFile = nullptr;
};

