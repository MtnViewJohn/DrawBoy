//
//  fakeargs.cpp
//  FakeLoom
//
//  Created by John Horigan on 12/12/24.
//

#include "fakeargs.h"
#include "args.hxx"
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <exception>
#include <system_error>

    

Options::Options(int argc, const char * argv[])
{
    if (!envSocket) envSocket = "";
    
    ToLowerReader tlr;
    
    auto f1 = envDobby ? dobbyMap.find(tlr(envDobby)) : dobbyMap.end();
    DobbyType defDobby = f1 != dobbyMap.end() ? f1->second : DobbyType::Positive;
    
    auto f2 = envShaft ? shaftMap.find(envShaft) : shaftMap.end();
    int defShaft = f2 != shaftMap.end() ? f2->second : 0;

    args::ArgumentParser parser("AVL CompuDobby III loom simulator.", "Report errors to John Horigan <john@glyphic.com>.");
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
    args::ValueFlag<std::string> _socketPath(parser, "SOCKET PATH", "Path for the socket", {"socket"}, envSocket, *envSocket ? args::Options::None : args::Options::Required);
    args::MapFlag<std::string, int> _maxShafts(parser, "SHAFT COUNT",
        "Number of shafts on the loom", {"shafts"}, shaftMap, defShaft,
        defShaft ? args::Options::None : args::Options::Required);
    args::MapFlag<std::string, DobbyType, ToLowerReader> _dobbyType(parser, "DOBBY TYPE", "Is the loom a positive or negative dobby (+ and - are also accepted)", {"dobbyType"}, dobbyMap, defDobby);
    args::Flag _ascii(parser, "ASCII only", "Restricts output to ASCII", {"ascii"});
    args::ValueFlag<std::string> _autoInput(parser, "INPUT STREAM",
        "Automatically sent input stream", {"auto"}, "");
    args::Flag _autoReset(parser, "Automatic reset", "Automatically respond to solenoid reset",
        {"autoreset"});

    try
    {
        parser.Prog("fakeloom");
        parser.ParseCLI(argc, argv);
    } catch (const args::Completion& e) {
        std::cout << e.what();
        return;
    } catch (const args::Help&) {
        std::cout << parser;
        return;
    } catch (const args::ParseError& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        throw;
    } catch (const args::RequiredError& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        throw;
    }

    socketPath = args::get(_socketPath);
    ascii = args::get(_ascii) || envASCII != nullptr;
    autoInput = args::get(_autoInput);
    autoReset = args::get(_autoReset);
    maxShafts = args::get(_maxShafts);
    fakeLoom = true;
}
