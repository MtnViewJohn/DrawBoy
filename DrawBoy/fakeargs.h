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
    int maxShafts;
    DobbyType dobbyType = DobbyType::Positive;
    bool ascii;
};

#endif /* fakeargs_h */
