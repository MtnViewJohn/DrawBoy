//
//  dtx.hpp
//  DrawBoy
//
//  Created by John Horigan on 1/22/25.
//

#pragma once
#include <fstream>
#include <string>
#include "draft.h"

class dtx : public draft {
public:
    dtx(std::ifstream& _dtxstream);
};
