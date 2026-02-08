//
//  draft.h
//  DrawBoy
//
//  Created by John Horigan on 1/22/25.
//

#pragma once
#include <map>
#include <vector>
#include "color.h"
#include <cstdint>
#include <cstdlib>

using std::size_t;
using std::uint64_t;

class draft {
public:
    virtual ~draft() = default;
    
    int  maxShafts;
    int  maxTreadles;
    bool risingShed = true;
    int ends = 0;
    int picks = 0;
    
    std::vector<uint64_t> liftplan;
    std::vector<uint64_t> tieup;
    std::vector<uint64_t> threading;
    std::vector<color>    warpColor, weftColor;
};
