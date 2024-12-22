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
    int returnValue = 0;
    try {
        Options opts(argc, argv);
        
        if (opts.err == 0)
            driver(opts);
        returnValue = opts.err;
    } catch (std::system_error& se) {
        std::cerr << se.what() << std::endl;
        returnValue = 4;
    } catch (std::runtime_error& rte) {
        std::cerr << rte.what() << std::endl;
        returnValue = 4;
    } catch (std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
        returnValue = 5;
    }

    return returnValue;
}
