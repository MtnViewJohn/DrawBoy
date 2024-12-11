/*
 *  wif.h
 *  DrawBoy
 */

/* Copyright (c) 2024 John Horigan <john@glyphic.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */
 
#pragma once
#include <cstdio>
#include <map>
#include <vector>

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
    
private:
    bool seekSection(const char* name);
    bool readSection(const char* name);
    std::vector<std::uint64_t> processKeyLines(bool multi);
    
    FILE* wifstream = nullptr;
    std::map<std::string, std::string> nameKeys;
    std::vector<std::string> numberKeys;
};
