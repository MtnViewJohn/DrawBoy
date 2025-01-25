//
//  dtx.hpp
//  DrawBoy
//
//  Created by John Horigan on 1/22/25.
//

#pragma once
#include <cstdio>
#include <string>
#include "draft.h"

class dtx : public draft {
public:
    dtx(FILE* _dtxstream);
};
