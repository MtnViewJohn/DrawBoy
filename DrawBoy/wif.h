/*
 *  wif.h
 *  DrawBoy
 */

 
#pragma once
#include <fstream>
#include <string>
#include "draft.h"

class wif : public draft {
public:
    wif(std::ifstream& _wifstream);
    
private:
    bool seekSection(const char* name);
    bool readSection(const char* name, int numlines, const std::string& defValue);
    void processLine(std::string& line, const char* name);
    std::vector<uint64_t> processKeyLines(bool multi);
    std::vector<color> processColorLines(const std::vector<color>& palette, size_t def);

    std::vector<std::string>   treadling;
    std::ifstream& wifstream;
    std::map<std::string, std::string> nameKeys;
    std::vector<std::string> numberKeys;
};
