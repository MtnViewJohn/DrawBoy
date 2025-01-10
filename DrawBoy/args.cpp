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
#include <memory>

namespace {
int
checkForSerial(std::string& name)
{
    int fd = ::open(name.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    int modemBits = 0;
    if (fd == -1)
        return -1;
    if (!::isatty(fd) || ::ioctl(fd, TIOCMGET, &modemBits) == -1)
    {
        ::close(fd);
        return -2;
    }
    return fd;
}

struct dir_deleter {
    void operator()(DIR* dp) { ::closedir(dp); }
};

using unique_dir = std::unique_ptr<DIR, dir_deleter>;

std::set<std::string>
enumSerial(const std::set<std::string>& exclude)
{
    std::set<std::string> result;
    
    auto devDir = unique_dir(::opendir("/dev"));
    
    if (devDir) {
        while (struct dirent *entry = ::readdir(devDir.get())) {
            if (entry->d_type == DT_CHR) {
                std::string dname = "/dev/";
                dname.append(entry->d_name);
                if (exclude.contains(dname))
                    continue;
                std::putchar('.');
                int fd = checkForSerial(dname);
                if (fd >= 0) {
                    ::close(fd);
                    std::cout << '\n' << dname << std::endl;
                    result.emplace(std::move(dname));
                }
            }
        }
    }
    return result;
}


void
initLoomPort(int fd)
{
    struct termios term;

    if (::tcgetattr(fd, &term) < 0)
        throw make_system_error("Cannot communicate with loom device");
    
    ::cfmakeraw(&term);
    ::cfsetispeed(&term, B9600);          // set 9600 baud
    ::cfsetospeed(&term, B9600);
    term.c_cflag &= (tcflag_t)(~PARENB);            // set 8N1
    term.c_cflag &= (tcflag_t)(~CSTOPB);
    term.c_cflag = (term.c_cflag & (tcflag_t)(~CSIZE)) | CS8;
    
    if (::tcsetattr(fd, TCSAFLUSH, &term) < 0)
        throw make_system_error("Cannot communicate with loom device");
}

struct file_deleter {
    void operator()(std::FILE* fp) { std::fclose(fp); }
};

using unique_file = std::unique_ptr<std::FILE, file_deleter>;

}

void
Options::parsePicks(const std::string& str, int maxPick)
{
    std::stringstream ss(str);
    std::string range;
    std::vector<int> newpicks;
    
    // If no treadle list provided then treadle the whole liftplan
    if (str.empty()) {
        picks.resize((size_t)maxPick);
        for (int i = 0; i < maxPick; ++i)
            picks[(size_t)i] = i + 1;
        return;
    }
    
    bool tabbyIsA = tabbyPattern == TabbyPattern::xAyB || tabbyPattern == TabbyPattern::AxBy;
    
    while (std::getline(ss, range, ',')) {
        try {
            if (range.empty())
                throw std::runtime_error("Empty treadling range.");
            int mult = 1;
            size_t multToken = range.find("x");
            if (multToken != std::string::npos) {
                size_t check;
                mult = std::stoi(range, &check);
                if (check != multToken || mult < 1)
                    throw std::runtime_error("Syntax error in treadling multiplier.");
                range.erase(0, multToken + 1);
            }
            if (std::strchr("ABab", range.front())) {
                for (int count = 0; count < mult; ++count) {
                    for (char p: range) {
                        switch (p) {
                            case 'a':
                            case 'A':
                                addpick(-1, newpicks, false, tabbyIsA);
                                break;
                            case 'b':
                            case 'B':
                                addpick(-2, newpicks, false, tabbyIsA);
                                break;
                            default:
                                break;
                        }
                    }
                }
            } else {
                size_t rangeToken = std::string::npos;
                bool tabbyRange = range.front() == '~';     // single pick w/tabby
                if (tabbyRange)
                    range.erase(0, 1);
                int start = std::stoi(range, &rangeToken);
                int end = start;
                if (rangeToken < range.length() && (range[rangeToken] == '~' || range[rangeToken] == '-')) {
                    if (tabbyRange)
                        throw std::runtime_error("Spurious ~ in treadling range.");
                    tabbyRange = range[rangeToken] == '~';  // pick range w/tabby
                    range.erase(0, rangeToken + 1);
                    end = std::stoi(range, &rangeToken);
                }
                if (rangeToken < range.length())
                    throw std::runtime_error("Unparsed text in treadling range.");
                if (start < 1 || end < 1)
                    throw std::runtime_error("Bad treadling range.");
                if (start > maxPick || end > maxPick)
                    throw std::runtime_error("Pick list includes picks that are not in the wif file.");
                for (int count = 0; count < mult; ++count) {
                    if (start < end) {
                        for (int p = start; p <= end; ++p)
                            addpick(p, newpicks, tabbyRange, tabbyIsA);
                    } else {
                        for (int p = end; p >= start; --p)
                            addpick(p, newpicks, tabbyRange, tabbyIsA);
                    }
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

void
Options::addpick(int _pick, std::vector<int>& newpicks, bool isTabby, bool &isTabbyA)
{
    if (isTabby) {
        if (tabbyPattern == TabbyPattern::xAyB || tabbyPattern == TabbyPattern::xByA) {
            newpicks.push_back(_pick);
            newpicks.push_back(isTabbyA ? -1 : -2);
        } else {
            newpicks.push_back(isTabbyA ? -1 : -2);
            newpicks.push_back(_pick);
        }
        isTabbyA = !isTabbyA;
    } else {
        newpicks.push_back(_pick);
        isTabbyA = tabbyPattern == TabbyPattern::xAyB || tabbyPattern == TabbyPattern::AxBy;
    }
}

Options::Options(int argc, const char * argv[])
{
    if (!envLoom) envLoom = "";
    
    ToLowerReader tlr;
    
    auto f1 = envDobby ? dobbyMap.find(tlr(envDobby)) : dobbyMap.end();
    DobbyType defDobby = f1 != dobbyMap.end() ? f1->second : DobbyType::Positive;
    
    auto f2 = envShaft ? shaftMap.find(envShaft) : shaftMap.end();
    int defShaft = f2 != shaftMap.end() ? f2->second : 0;
    
    auto f3 = envANSI ? ANSImap.find(tlr(envANSI)) : ANSImap.end();
    ANSIsupport defANSI = f3 != ANSImap.end() ? f3->second : ANSIsupport::yes;
    
    args::ArgumentParser parser("AVL CompuDobby III loom driver.", "Report errors to John Horigan <john@glyphic.com>.");
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
    args::CompletionFlag completion(parser, {"complete"});
    args::Flag findloom(parser, "find loom", "Finds device files that might be the loom.", {"findloom"}, args::Options::KickOut);
    args::ValueFlag<int> _pick(parser, "PICK",
        "The pick to start weaving at (defaults to 1).", {'p', "pick"}, 1, args::Options::Single);
    args::ValueFlag<std::string> _picks(parser, "PICK LIST",
        "List of pick ranges in the treadling or liftplan to weave.", {'P', "picks"}, "");
    args::ValueFlag<std::string, ToLowerReader> _tabby(parser, "TABBY SPEC",
        "Which shafts are activated for tabby A and tabby B", {"tabby"}, args::Options::Single);
    args::MapFlag<std::string, TabbyPattern, ToLowerReader> _tabbyPattern(parser, "TABBY PATTERN",
        "Which pattern is used for inserted tabby picks",
        {"tabbyPattern"}, tabbyMap, TabbyPattern::xAyB, args::Options::Single);
    args::ValueFlag<std::string> _tabbyColor(parser, "TABBY COLOR", "Color displayed for tabby picks",
        {"tabbycolor"}, "00FF00", args::Options::Single);
    args::ValueFlag<std::string> _loomDevice(parser, "LOOM PATH",
        "The path of the loom device in the /dev directory", {"loomDevice"},
        envLoom, (*envLoom || envSocket) ? args::Options::Single : args::Options::Required | args::Options::Single);
    args::MapFlag<std::string, int> _maxShafts(parser, "SHAFT COUNT",
        "Number of shafts on the loom", {"shafts"}, shaftMap, defShaft,
        defShaft ? args::Options::Single : args::Options::Required | args::Options::Single);
    args::MapFlag<std::string, DobbyType, ToLowerReader> _dobbyType(parser, "DOBBY TYPE",
        "Is the loom a positive or negative dobby (+ and - are also accepted)", {"dobbyType"},
        dobbyMap, defDobby, args::Options::Single);
    args::Flag _ascii(parser, "ASCII only", "Restricts output to ASCII", {"ascii"}, args::Options::Single);
    args::MapFlag<std::string, ANSIsupport, ToLowerReader> _ansi(parser, "ANSI SUPPORT",
        "Does the terminal support ANSI style codes and possibly true-color", {"ansi"},
        ANSImap, defANSI, args::Options::Single);
    args::Positional<std::string> _wifFile(parser, "WIF PATH", "The path of the WIF file",
        args::Options::Required);

    try {
        parser.Prog("DrawBoy");
        parser.ParseCLI(argc, argv);
    } catch (const args::Completion& e) {
        std::cout << e.what();
        driveLoom = false;
        return;
    } catch (const args::Help&) {
        std::cout << parser;
        driveLoom = false;
        return;
    } catch (const args::ParseError& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        throw std::runtime_error("");   // rethrow, but erase error message
    } catch (const args::RequiredError& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        throw std::runtime_error("");   // rethrow, but erase error message
    }
    
    if (findloom) {
        char buf[100];
        std::cout << "\nMake sure that the USB dongle is unplugged, and then type return.";
        std::cin.getline(buf, 100, '\n');
        std::cout << "Scanning for pre-existing devices";
        auto exclude = enumSerial({});
        std::cout << "\nOK! Now plug in the USB dongle, wait a few seconds, and type return.";
        std::cin.getline(buf, 100, '\n');
        std::cout << "Scanning for new devices";
        auto results = enumSerial(exclude);
        if (results.empty()) {
            std::cout << "\nAlas! No new devices were found." << std::endl;
        } else {
            std::cout << "\nCandidate devices:" << std::endl;
            for (const auto& dev: results)
                std::cout << dev << std::endl;
        }
        driveLoom = false;
        return;
    }
    
    loomDevice = args::get(_loomDevice);
    wifFile = args::get(_wifFile);
    maxShafts = args::get(_maxShafts);
    pick = args::get(_pick);
    dobbyType = args::get(_dobbyType);
    ascii = args::get(_ascii) || envASCII != nullptr;
    ansi = args::get(_ansi);
    tabbyPattern = args::get(_tabbyPattern);

    wifFile = args::get(_wifFile);
    
    if (auto wiffileowner = unique_file(std::fopen(wifFile.c_str(), "r")))
        wifContents.readWif(wiffileowner.get());
    else
        throw make_system_error("Cannot open wif file");
    
    parsePicks(args::get(_picks), wifContents.picks);
    
    if (wifContents.maxShafts > maxShafts)
        throw std::runtime_error("Wif file requires more shafts than the loom possesses.");
    
    if (envSocket) {
        IPC::Client fakeLoom(envSocket);
        loomDeviceFD = fakeLoom.release();
    } else {
        loomDeviceFD = checkForSerial(loomDevice);
        
        if (loomDeviceFD == -1)
            throw std::runtime_error("Cannot open loom device.");
        if (loomDeviceFD == -2)
            throw std::runtime_error("Loom device is not a serial port.");
        
        initLoomPort(loomDeviceFD);
    }
    
    std::string tabby = args::get(_tabby);
    if (tabby.empty()) {
        tabby.reserve((size_t)maxShafts);
        for (size_t shaft = 0; shaft < (size_t)maxShafts; shaft += 2)
            tabby.append("ab");
    }
    
    for (size_t shaft = 0; shaft < tabby.length(); ++shaft) {
        if (tabby[shaft] == 'a')
            tabbyA |= 1 << shaft;
        else if (tabby[shaft] == 'b')
            tabbyB |= 1 << shaft;
        else
            throw std::runtime_error("Bad character in tabby specification.");
    }
            
    if (tabbyA == 0)
        std::cerr << "Tabby A has no shafts set." << std::endl;
    if (tabbyB == 0)
        std::cerr << "Tabby B has no shafts set." << std::endl;
    
    tabbyColor = color(args::get(_tabbyColor).c_str());
}

Options::~Options()
{
    if (loomDeviceFD >= 0) {
        ::close(loomDeviceFD);
        loomDeviceFD = -1;
    }
}
