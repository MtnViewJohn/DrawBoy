//
//  fakemain.cpp
//  FakeLoom
//
//  Created by John Horigan on 12/12/24.
//

#include "fakeargs.h"
#include <exception>
#include <system_error>
#include <cstdio>
#include "driver.h"

int main(int argc, const char * argv[]) {
    try {
        Options opts(argc, argv);
        driver(opts);
    } catch (std::system_error& e) {
        std::fputs(e.what(), stderr); fputc('\n', stderr);
        return 2;
    } catch (std::runtime_error& e) {
        std::fputs(e.what(), stderr); fputc('\n', stderr);
        return 1;
    } catch (std::exception& e) {
        std::fputs(e.what(), stderr); fputc('\n', stderr);
        return 3;
    }
    return 0;
}
