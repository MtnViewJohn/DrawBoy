/*
 *  wif.h
 *  DrawBoy
 */

 
#pragma once
#include <cstdio>
#include <string>
#include "draft.h"

class wif : public draft {
public:
    wif(FILE* _wifstream);
    
private:
    bool seekSection(const char* name);
    bool readSection(const char* name, int numlines, const std::string& defValue);
    std::vector<uint64_t> processKeyLines(bool multi);
    std::vector<color> processColorLines(const std::vector<color>& palette, size_t def);

    std::vector<std::string>   treadling;
    FILE* wifstream = nullptr;
    std::map<std::string, std::string> nameKeys;
    std::vector<std::string> numberKeys;
};
