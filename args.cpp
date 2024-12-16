/*
 *  args.cpp
 *  DrawBoy
 */


#include "args.h"
#include "args.hxx"
#include "stdio.h"
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include "ipc.h"
#include <set>

namespace {
int
checkForSerial(std::string& name)
{
    int fd = open(name.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    int modemBits = 0;
    if (fd == -1)
        return 1;
    if (!isatty(fd) || ioctl(fd, TIOCMGET, &modemBits) == -1)
    {
        close(fd);
        return 2;
    }
    return fd;
}

void
enumSerial(const std::set<std::string>& exclude)
{
    DIR* devDir = opendir("/dev");
    struct dirent *entry = readdir(devDir);
    while (entry != NULL) {
        if (entry->d_type == DT_CHR) {
            std::string dname = "/dev/";
            dname.append(entry->d_name, entry->d_namlen);
            int fd = checkForSerial(dname);
            if (fd != 1 && fd != 2) {
                close(fd);
                if (!exclude.contains(dname))
                    std::cout << dname << std::endl;
            }
        }
        entry = readdir(devDir);
    }
    closedir(devDir);
}


std::set<std::string>
readfile(std::istream& in, const std::string& commentPrefix)
{
    std::set<std::string> buf;
    std::string line;
    if (!in) return buf;
    while (std::getline(in, line)) {
        line.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
        line.erase(line.find_last_not_of(" \t\n\r\f\v") + 1);
        if (line.empty() || line.find(commentPrefix) == 0)
            continue;
        buf.emplace(std::move(line));
    }
    return buf;
}

void
initLoomPort(int fd)
{
    struct termios term;

    if (tcgetattr(fd, &term) < 0)
        throw make_system_error("Cannot communicate with loom device");
    
    cfmakeraw(&term);
    cfsetispeed(&term, B9600);          // set 9600 baud
    cfsetospeed(&term, B9600);
    term.c_cflag &= (tcflag_t)(~PARENB);            // set 8N1
    term.c_cflag &= (tcflag_t)(~CSTOPB);
    term.c_cflag = (term.c_cflag & (tcflag_t)(~CSIZE)) | CS8;
    
    if (tcsetattr(fd, TCSAFLUSH, &term) < 0)
        throw make_system_error("Cannot communicate with loom device");
}

}

void
Options::parsePicks(const std::string& str)
{
    std::stringstream ss(str);
    std::string range;
    std::vector<int> newpicks;
    
    while (std::getline(ss, range, ',')) {
        try {
            int mult = 1;
            std::size_t multToken = range.find("x");
            if (multToken != std::string::npos) {
                std::size_t check;
                mult = std::stoi(range, &check);
                if (check != multToken || mult < 1)
                    throw std::runtime_error("Syntax error in treadling multiplier.");
                range.erase(0, multToken + 1);
            }
            std::size_t rangeToken = std::string::npos;
            int start = std::stoi(range, &rangeToken);
            int end = start;
            if (rangeToken < range.length() && range[rangeToken] == '-') {
                range.erase(0, rangeToken + 1);
                end = std::stoi(range, &rangeToken);
            }
            if (start < 1 || end < 1)
                throw std::runtime_error("Bad treadling range.");
            for (int count = 0; count < mult; ++count) {
                if (start < end) {
                    for (int p = start; p <= end; ++p)
                        newpicks.push_back(p);
                } else {
                    for (int p = end; p >= start; --p)
                        newpicks.push_back(p);
                }
            }
        } catch (std::runtime_error& rte) {
            throw;
        } catch (...) {
            throw std::runtime_error("Syntax error in treadling range.");
        }
    }
    std::swap(newpicks, picks);
}

int
Options::getOptions(int argc, const char * argv[])
{
    if (!envLoom) envLoom = "";
    
    auto f1 = envDobby ? dobbyMap.find(envDobby) : dobbyMap.end();
    DobbyType defDobby = f1 != dobbyMap.end() ? f1->second : DobbyType::Positive;
    
    auto f2 = envShaft ? shaftMap.find(envShaft) : shaftMap.end();
    int defShaft = f2 != shaftMap.end() ? f2->second : 0;
    
    auto f3 = envANSI ? ANSImap.find(envANSI) : ANSImap.end();
    ANSIsupport defANSI = f3 != ANSImap.end() ? f3->second : ANSIsupport::yes;
    
    args::ArgumentParser parser("AVL CompuDobby III loom driver.", "Report errors to John Horigan <john@glyphic.com>.");
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
    args::CompletionFlag completion(parser, {"complete"});
    args::Flag findloom(parser, "find loom", "Finds device files that might be the loom.", {"findloom"}, args::Options::KickOut);
    args::ValueFlag<std::string> _loomDevice(parser, "LOOM PATH",
        "The path of the loom device in the /dev directory", {'l', "loomDevice"},
        envLoom, (*envLoom || envSocket) ? args::Options::None : args::Options::Required);
    args::ValueFlag<std::string> _wifFile(parser, "WIF PATH",
        "The path of the WIF file", {'f', "wif"}, "", args::Options::Required);
    args::MapFlag<std::string, int> _maxShafts(parser, "SHAFT COUNT",
        "Number of shafts on the loom", {'s', "shafts"}, shaftMap, defShaft,
        defShaft ? args::Options::None : args::Options::Required);
    args::MapFlag<std::string, DobbyType> _dobbyType(parser, "DOBBY TYPE", "Is the loom a positive or negative dobby (+ and - are also accepted)", {'t', "dobbyType"}, dobbyMap, defDobby);
    args::ValueFlag<int> _pick(parser, "PICK",
        "The pick to start weaving at (defaults to 1).", {'p', "pick"}, 1);
    args::ValueFlag<std::string> _picks(parser, "PICK LIST",
        "List of pick ranges in the treadling or liftplan to weave.", {'P', "picks"}, "");
    args::Flag _ascii(parser, "ASCII only", "Restricts output to ASCII", {"ascii"});
    args::MapFlag<std::string, ANSIsupport> _ansi(parser, "ANSI SUPPORT", "Does the terminal support ANSI style codes and possibly true-color", {"ansi"}, ANSImap, defANSI);
    args::ValueFlag<std::string> _tabby(parser, "TABBY SPEC", "Which shafts are activated for tabby A and tabby B", {'t', "tabby"});
    
    try {
        parser.Prog("DrawBoy");
        parser.ParseCLI(argc, argv);
    } catch (const args::Completion& e) {
        std::cout << e.what();
        return 0;
    } catch (const args::Help&) {
        std::cout << parser;
        return 0;
    } catch (const args::ParseError& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    } catch (const args::RequiredError& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    
    if (findloom) {
        std::set<std::string> exclude;
        if (!isatty(0))
            exclude = readfile(std::cin, " ");
        enumSerial(exclude);
        return 0;
    }
    
    loomDevice = args::get(_loomDevice);
    wifFile = args::get(_wifFile);
    maxShafts = args::get(_maxShafts);
    pick = args::get(_pick);
    dobbyType = args::get(_dobbyType);
    ascii = args::get(_ascii) || envASCII != nullptr;
    ansi = args::get(_ansi);

    if (_picks)
        parsePicks(args::get(_picks));
    
    wifFile = args::get(_wifFile);
    wifFileStream = std::fopen(wifFile.c_str(), "r");
    
    if (wifFileStream == nullptr)
        throw make_system_error("Cannot open wif file");
    
    if (envSocket) {
        IPC::Client fakeLoom(envSocket);
        loomDeviceFD = fakeLoom.release();
    } else {
        loomDeviceFD = checkForSerial(loomDevice);
        
        if (loomDeviceFD == 1)
            throw std::runtime_error("Cannot open loom device.");
        if (loomDeviceFD == 2)
            throw std::runtime_error("Loom device is not a serial port.");
        
        initLoomPort(loomDeviceFD);
    }
    
    std::string tabby = args::get(_tabby);
    if (tabby.empty()) {
        tabby.reserve((std::size_t)maxShafts);
        for (std::size_t shaft = 0; shaft < (std::size_t)maxShafts; shaft += 2)
            tabby.append("AB");
    }
    
    for (std::size_t shaft = 0; shaft < tabby.length(); ++shaft) {
        if (tabby[shaft] == 'A')
            tabbyA |= 1 << shaft;
        else if (tabby[shaft] == 'B')
            tabbyB |= 1 << shaft;
        else
            throw std::runtime_error("Bad character in tabby specification.");
    }
            
    if (tabbyA == 0)
        std::cerr << "Tabby A has no shafts set." << std::endl;
    if (tabbyB == 0)
        std::cerr << "Tabby B has no shafts set." << std::endl;

    valid = true;
    return 0;
}

Options::~Options()
{
    if (loomDeviceFD >= 0) {
        close(loomDeviceFD);
        loomDeviceFD = -1;
    }
    if (wifFileStream != nullptr) {
        std::fclose(wifFileStream);
        wifFileStream = nullptr;
    }
}
