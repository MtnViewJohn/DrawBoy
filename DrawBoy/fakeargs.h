//
//  fakeargs.h
//  FakeLoom
//
//  Created by John Horigan on 12/12/24.
//

#ifndef fakeargs_h
#define fakeargs_h

#include "argscommon.h"
#include <string>

struct Options {
    Options(int argc, const char * argv[]);
    std::string socketPath;
    std::string autoInput;
    bool autoReset;
    int maxShafts;
    DobbyType dobbyType = DobbyType::Positive;
    bool ascii;
    bool fakeLoom = false;
};

#endif /* fakeargs_h */
