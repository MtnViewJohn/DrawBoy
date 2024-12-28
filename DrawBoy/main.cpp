/*
 *  main.cpp
 *  DrawBoy
 */

 
#include <iostream>
#include <cstdio>
#include <vector>
#include <string>
#include "args.h"
#include "driver.h"



int main(int argc, const char * argv[]) {
    try {
        Options opts(argc, argv);
        
        if (opts.driveLoom)
            driver(opts);
    } catch (std::system_error& se) {
        std::cerr << se.what() << std::endl;
        return 4;
    } catch (std::runtime_error& rte) {
        if (*rte.what())
            std::cerr << rte.what() << std::endl;
        return 4;
    } catch (std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
        return 5;
    }

    return 0;
}
