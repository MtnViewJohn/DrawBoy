/*
 *  main.cpp
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
 
#include <iostream>
#include <cstdio>
#include <vector>
#include <string>
#include "wif.h"
#include "args.h"




int main(int argc, const char * argv[]) {
    int returnValue = 0;
    Options opts;
    try {
        returnValue = opts.getOptions(argc, argv);
        
        if (opts.valid) {
            wif wifContents(opts.wifFileStream);
            
            if (wifContents.maxShafts > opts.maxShafts)
                throw std::runtime_error("Wif file has more shafts than the loom.");
            
            // If no treadle list provided then treadle the whole liftplan
            if (opts.picks.empty()) {
                opts.picks.resize(wifContents.picks);
                for (int i = 1; i <= wifContents.picks; ++i)
                    opts.picks[i - 1] = i;
            }
        }
    } catch (std::system_error& se) {
        std::cerr << se.what() << std::endl;
        returnValue = 4;
    } catch (std::runtime_error& rte) {
        std::cerr << rte.what() << std::endl;
        returnValue = 4;
    }

    return returnValue;
}
