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
#include <charconv>
#include "wif.h"
#include "dtx.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cassert>

namespace {
struct addr_deleter {
    void operator()(addrinfo* ap) { ::freeaddrinfo(ap); }
};

using unique_ai = std::unique_ptr<addrinfo, addr_deleter>;

int
openTelnet(std::string& address)
{
    addrinfo hint{AI_ADDRCONFIG, PF_INET, SOCK_STREAM, IPPROTO_TCP};
    addrinfo* results;

    if (::getaddrinfo(address.c_str(), "telnet", &hint, &results) < 0)
        return -2;

    auto aiList = unique_ai(results);

    for (addrinfo* ai = results; ai; ai = ai->ai_next) {
        int sockFD = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sockFD < 0)
            continue;

        if (::connect(sockFD, ai->ai_addr, ai->ai_addrlen) < 0) {
            ::close(sockFD);
            continue;
        }

        return sockFD;
    }
    return -1;
}

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
initLoomPort(int fd, int cdgen)
{
    struct termios term;

    if (::tcgetattr(fd, &term) < 0)
        throw make_system_error("Cannot communicate with loom device");
    
    ::cfmakeraw(&term);
    
    if (cdgen == 1) {
        ::cfsetispeed(&term, B1200);            // set 1200 baud
        ::cfsetospeed(&term, B1200);
        term.c_cflag |= PARENB;                 // set 7E2
        term.c_cflag &= (tcflag_t)(~PARODD);
        term.c_cflag |= CSTOPB;
        term.c_cflag = (term.c_cflag & (tcflag_t)(~CSIZE)) | CS7;
    } else {
        ::cfsetispeed(&term, B9600);            // set 9600 baud
        ::cfsetospeed(&term, B9600);
        term.c_cflag &= (tcflag_t)(~PARENB);    // set 8N1
        term.c_cflag &= (tcflag_t)(~CSTOPB);
        term.c_cflag = (term.c_cflag & (tcflag_t)(~CSIZE)) | CS8;
    }
    term.c_cflag |= CLOCAL;
    term.c_cc[VMIN] = 0;
    term.c_cc[VTIME] = 1;

    if (::tcsetattr(fd, TCSAFLUSH, &term) < 0)
        throw make_system_error("Cannot communicate with loom device");
}

void
addpick(int _pick, std::vector<int>& newpicks, bool isTabby, bool patternBeforeTabby)
{
    if (isTabby) {
        if (patternBeforeTabby) {
            newpicks.push_back(_pick);
            newpicks.push_back(TabbyNext);
        } else {
            newpicks.push_back(TabbyNext);
            newpicks.push_back(_pick);
        }
    } else {
        newpicks.push_back(_pick);
    }
}

int my_stoi(std::string_view str, size_t* pos = nullptr)
{
    int v = 0;
    auto r = std::from_chars(str.begin(), str.end(), v);
    if (pos)
        *pos = (size_t)(r.ptr - str.begin());
    if (r.ec == std::errc::invalid_argument)
        throw std::invalid_argument(std::string(str));
    if (r.ec == std::errc::result_out_of_range)
        throw std::out_of_range(std::string(str));
    return v;
}

size_t
findMatch(std::string_view str)
{
    int level = 0;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '(') ++level;
        if (str[i] == ')') --level;
        if (level == 0)
            return i;
        if (level < 0)
            return 0;
    }
    return 0;
}

std::vector<int>
ParsePicks(std::string_view str, int maxPick, bool patternBeforeTabby, bool threading)
{
    std::vector<int> newpicks;
    
    while (!str.empty()) {
        try {
            int mult = 1;
            std::vector<int> pickRange;
            size_t multToken = str.find("x");
            if (multToken != std::string::npos && std::isdigit(str.front())) {
                size_t check;
                mult = my_stoi(str, &check);
                if (check == multToken) {   // multiplier was for this term
                    if (mult < 1)
                        throw std::runtime_error("Syntax error in treadling multiplier.");
                    str.remove_prefix(multToken + 1);
                    if (str.empty() || str.front() == ',')
                        throw std::runtime_error("Syntax error in treadling multiplier.");
                } else {                    // multiplier was for another term
                    mult = 1;
                }
            }
            if (std::strchr("ABab", str.front())) {
                if (threading)
                    throw std::runtime_error("Tabby entries make no sense in treadle-the-threading mode.");
                while (!str.empty() && std::strchr("ABab", str.front())) {
                    switch (str.front()) {
                        case 'a':
                        case 'A':
                            addpick(-1, pickRange, false, false);
                            break;
                        case 'b':
                        case 'B':
                            addpick(-2, pickRange, false, false);
                            break;
                        default:
                            break;
                    }
                    str.remove_prefix(1);
                }
            } else if (str.front() == '(') {
                if (size_t match = findMatch(str)) {
                    pickRange = ParsePicks(str.substr(1, match - 1), maxPick, patternBeforeTabby, threading);
                    str.remove_prefix(match + 1);
                } else {
                    throw std::runtime_error("Unbalanced parentheses in pick list.");
                }
            } else {
                size_t rangeToken = std::string::npos;
                bool tabbyRange = str.front() == '~';     // single pick w/tabby
                if (tabbyRange)
                    str.remove_prefix(1);
                if (tabbyRange && threading)
                    throw std::runtime_error("Tabby entries make no sense in treadle-the-threading mode.");
                int start = my_stoi(str, &rangeToken);
                int end = start;
                if (rangeToken < str.length() && (str[rangeToken] == '~' || str[rangeToken] == '-')) {
                    if (tabbyRange)
                        throw std::runtime_error("Spurious ~ in treadling range.");
                    tabbyRange = str[rangeToken] == '~';  // pick range w/tabby
                    if (tabbyRange && threading)
                        throw std::runtime_error("Tabby entries make no sense in treadle-the-threading mode.");
                    str.remove_prefix(rangeToken + 1);
                    end = my_stoi(str, &rangeToken);
                    str.remove_prefix(rangeToken);
                } else {
                    str.remove_prefix(rangeToken);
                }
                if (start < 1 || end < 1)
                    throw std::runtime_error("Bad treadling range.");
                if (start > maxPick || end > maxPick)
                    throw std::runtime_error("Pick list includes picks that are not in the wif file.");
                if (start <= end) {
                    for (int p = start; p <= end; ++p)
                        addpick(p, pickRange, tabbyRange, patternBeforeTabby);
                } else {
                    for (int p = end; p >= start; --p)
                        addpick(p, pickRange, tabbyRange, patternBeforeTabby);
                }
            }
            if (!str.empty() && str.front() != ',')
                throw std::runtime_error("Unparsed text in treadling range.");
            if (!str.empty())
                str.remove_prefix(1);
            for (auto i = 0; i < mult; ++i)
                newpicks.insert(newpicks.end(), pickRange.begin(), pickRange.end());
        } catch (std::runtime_error& rte) {
            throw;
        } catch (...) {
            throw std::runtime_error("Syntax error in treadling range.");
        }
    }
    return newpicks;
}

}

void
Options::parsePicks(const std::string &str)
{
    if (str.empty())
        return;

    bool patternBeforeTabby = tabbyPattern == TabbyPattern::xAyB || tabbyPattern == TabbyPattern::xByA;
    bool tabbyAFirst = tabbyPattern == TabbyPattern::xAyB || tabbyPattern == TabbyPattern::AxBy;
    
    auto picks = ParsePicks(str, draftContents->picks, patternBeforeTabby, false);
    std::vector<uint64_t> newLiftplan;
    std::vector<color>    newWeftColor;
    newLiftplan.reserve(picks.size() + 1);
    newWeftColor.reserve(picks.size() + 1);
    newLiftplan.push_back(0);
    newWeftColor.push_back(color());

    // Replace auto-tabby picks with actual tabby A or tabby B
    bool tabbyIsA = tabbyAFirst;
    int picksSinceTabby = 10;       // anything > 1
    for (int _pick: picks) {
        switch (_pick) {
            case TabbyA:
            case TabbyB:
                newLiftplan.push_back(_pick == TabbyA ? tabbyA : tabbyB);
                newWeftColor.push_back(tabbyColor);
                tabbyIsA = _pick != TabbyA;
                picksSinceTabby = 0;
                break;
            case TabbyNext:
                if (picksSinceTabby > 1)
                    tabbyIsA = tabbyAFirst;
                newLiftplan.push_back(tabbyIsA ? tabbyA : tabbyB);
                newWeftColor.push_back(tabbyColor);
                tabbyIsA = !tabbyIsA;
                picksSinceTabby = 0;
                break;
            default:
                assert(_pick > 0 && _pick <= draftContents->picks);
                newLiftplan.push_back(draftContents->liftplan[(size_t)_pick]);
                newWeftColor.push_back(draftContents->weftColor[(size_t)_pick]);
                ++picksSinceTabby;
                break;
        }
    }

    std::swap(draftContents->liftplan, newLiftplan);
    std::swap(draftContents->weftColor, newWeftColor);
    draftContents->picks = (int)draftContents->liftplan.size() - 1;
}

void
Options::parseEnds(const std::string& str)
{
    if (str.empty())
        return;

    auto endList = ParsePicks(str, draftContents->ends, true, true);
    std::vector<uint64_t> newThreading;
    std::vector<color>    newWarpColor;
    newThreading.reserve(endList.size() + 1);
    newWarpColor.reserve(endList.size() + 1);
    newThreading.push_back(0);
    newWarpColor.push_back(color());

    for (int end: endList) {
        newThreading.push_back(draftContents->threading[(size_t)end]);
        newWarpColor.push_back(draftContents->warpColor[(size_t)end]);
    }

    std::swap(draftContents->threading, newThreading);
    std::swap(draftContents->warpColor, newWarpColor);
    draftContents->ends = (int)draftContents->threading.size() - 1;
}

Options::Options(int argc, const char * argv[])
{
    bool defNetwork = envAddress;
    if (!envLoom) envLoom = "";
    if (!envAddress || *envAddress == '\0') envAddress = "169.254.128.3";
    
    int defGen = 0;
    if (envDobbyGen && *envDobbyGen) {
        auto res = std::from_chars(envDobbyGen, envDobbyGen + std::strlen(envDobbyGen), defGen, 10);
        if (res.ec != std::errc() || defGen < 1 || defGen > 4)
            defGen = 0;
    }

    pickFile = getenv("HOME");
    pickFile.append("/.drawboypick");

    ToLowerReader tlr;
    
    auto f1 = envDobbyType ? dobbyMap.find(tlr(envDobbyType)) : dobbyMap.end();
    DobbyType defDobby = f1 != dobbyMap.end() ? f1->second : DobbyType::Unspecified;
    
    auto f2 = envShaft ? shaftMap.find(envShaft) : shaftMap.end();
    int defShaft = f2 != shaftMap.end() ? f2->second : 0;
    
    auto f3 = envANSI ? ANSImap.find(tlr(envANSI)) : ANSImap.end();
    ANSIsupport defANSI = f3 != ANSImap.end() ? f3->second : ANSIsupport::yes;
    
    args::ArgumentParser parser("AVL Compu-Dobby loom driver.", "Report errors to John Horigan <john@glyphic.com>.");
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
    args::CompletionFlag completion(parser, {"complete"});
    args::Flag findloom(parser, "find loom", "Finds device files that might be the loom.", {"findloom"}, args::Options::KickOut);
    args::Flag check(parser, "check draft parsing", "Tests whether the draft file parses.", {"check"}, args::Options::Hidden);
    args::Flag _cd1(parser, "Compu-Dobby I", "Loom has a Compu-Dobby I", {"cd1"}, args::Options::Single);
    args::Flag _cd2(parser, "Compu-Dobby II", "Loom has a Compu-Dobby II", {"cd2"}, args::Options::Single);
    args::Flag _cd3(parser, "Compu-Dobby III", "Loom has a Compu-Dobby III", {"cd3"}, args::Options::Single);
    args::Flag _cd4(parser, "Compu-Dobby IV/4.5", "Loom has a Compu-Dobby IV/4.5", {"cd4"}, args::Options::Single);
    args::Flag _net(parser, "use ethernet", "Connect to the loom over ethernet", {'n', "net"}, args::Options::Single);
    args::ValueFlag<std::string> _pick(parser, "PICK",
        "The pick to start weaving at (defaults to 1).", {'p', "pick"}, "1", args::Options::Single);
    args::ValueFlag<std::string> _picks(parser, "PICK_LIST",
        "List of pick ranges in the treadling or liftplan to weave.", {'P', "picks"}, "");
    args::ValueFlag<std::string, ToLowerReader> _tabby(parser, "TABBY_SPEC",
        "Which shafts are activated for tabby A and tabby B", {"tabby"},
        "abababababababababababababababababababab", args::Options::Single);
    args::MapFlag<std::string, TabbyPattern, ToLowerReader> _tabbyPattern(parser, "TABBY_PATTERN",
        "Which pattern is used for inserted tabby picks",
        {"tabbyPattern"}, tabbyMap, TabbyPattern::xAyB, args::Options::Single);
    args::ValueFlag<std::string> _tabbyColor(parser, "TABBY_COLOR", "Color displayed for tabby picks",
        {"tabbycolor"}, "00FF00", args::Options::Single);
    args::ValueFlag<std::string> _ends(parser, "ENDS_LIST",
        "List of end ranges in the threading to weave", {"ends"},
        args::Options::Single);
    args::Flag _threading(parser, "treadle the threading", "Treadle the threading, instead of the picks",
        {"threading"}, args::Options::Single);
    args::ValueFlag<std::string> _sleying(parser, "SLEYING_PATTERN", "Pattern used for sleying the reed",
        {"sley"}, "", args::Options::Single);
    args::ValueFlag<std::string> _loomDevice(parser, "LOOM_PATH",
        "The path of the loom device in the /dev directory", {"loomDevice"},
        envLoom, args::Options::Single);
    args::ValueFlag<std::string> _loomAddress(parser, "LOOM_ADDRESS",
        "The network address or IP address of the loom", {"loomAddress"},
        envAddress, args::Options::Single);
    args::MapFlag<std::string, int> _maxShafts(parser, "SHAFT_COUNT",
        "Number of shafts on the loom", {"shafts"}, shaftMap, defShaft,
        args::Options::Single);
    args::MapFlag<std::string, DobbyType, ToLowerReader> _dobbyType(parser, "DOBBY_TYPE",
        "Is the loom a positive, negative, or virtual positive dobby (+ and - are also accepted)", {"dobbyType"},
        dobbyMap, defDobby, args::Options::Single);
    args::MapFlag<std::string, ColorAlert, ToLowerReader> _bell(parser, "COLOR_ALERT", "Ring bell on color changes",
        {"colorAlert"}, alertMap, ColorAlert::None);
    args::Flag _ascii(parser, "ASCII only", "Restricts output to ASCII", {"ascii"}, args::Options::Single);
    args::Flag _log(parser, "Enable logging", "Logs loom I/O to /tmp", {"log"}, args::Options::Hidden);
    args::MapFlag<std::string, ANSIsupport, ToLowerReader> _ansi(parser, "ANSI_SUPPORT",
        "Does the terminal support ANSI style codes and possibly true-color", {"ansi"},
        ANSImap, defANSI, args::Options::Single);
    args::Positional<std::string> _draftFile(parser, "DRAFT_PATH", "The path of the WIF or DTX file",
        args::Options::Required);

    try {
        parser.Prog("drawboy");
        parser.ParseCLI(argc, argv);
        if (!findloom) {
            if (_cd1.Get() + _cd2.Get() + _cd3.Get() + _cd4.Get() != 1 && defGen == 0)
                throw args::ParseError("Option Compu-dobby generation is required: --cd1, --cd2, --cd3, or --cd4.");
            if (_loomDevice.Get().empty() && !defNetwork && !_loomAddress && !envSocket)
                throw args::ParseError("Option loom device path or loom network address is required: --loomDevice or --loomAddress.");
            if (_net && args::get(_loomAddress).data()[0] == '\0')
                throw args::ParseError("Option loom  network address is required for network mode: --loomAddress.");
            if (!_cd4 && defGen != 4 && !_maxShafts)
                throw args::ParseError("Option --shaft required unless Compu-Dobby 4/4.5.");
            if (_pick) {
                bool autoPick = _pick.Get().starts_with("next");
                int pickNum = 0;
                if (_pick.Get() != "next") {
                    reverseTreadle = _pick.Get().starts_with('<');
                    size_t startAt = reverseTreadle ? 1 : (autoPick ? 4 : 0);
                    auto pickRes = std::from_chars(_pick.Get().data() + startAt,
                        _pick.Get().data() + _pick.Get().length(), pickNum);
                    if (pickRes.ec != std::errc())
                        throw args::ParseError("Argument 'PICK' received invalid value type '" +
                            _pick.Get() + "'");
                }
                if (autoPick) {
                    bool success = false;
                    if (std::FILE* pickf{std::fopen(pickFile.c_str(), "r")}) {
                        char buf[12];
                        if (std::fgets(buf, 12, pickf)) {
                            char* start = buf[0] == '<' ? buf + 1 : buf;
                            int pickBase = 1;
                            auto baseRes = std::from_chars(start, buf + 12, pickBase, 10);
                            if (baseRes.ec == std::errc()) {
                                reverseTreadle = buf[0] == '<';
                                pick = reverseTreadle ? pickBase - pickNum
                                                      : pickBase + pickNum;
                                success = true;
                            }
                        }
                        std::fclose(pickf);
                    }
                    if (success) {
                        std::print("Continuing at pick {}{}.\n\n", reverseTreadle ? "<" : "", pick);
                    } else {
                        std::print("Failed to fetch previous pick. Starting at pick 1.\n\n");
                        pick = 1;
                    }
                } else {
                    pick = pickNum;
                }
            }
        }
    } catch (const args::Completion& e) {
        std::cout << e.what();
        driveLoom = false;
        return;
    } catch (const args::Help&) {
        std::cout << parser;
        driveLoom = false;
        return;
    } catch (const args::ParseError& e) {
        std::cerr << e.what() << "\n\n";
        std::cerr << parser;
        throw std::runtime_error("");   // rethrow, but erase error message
    } catch (const args::RequiredError& e) {
        std::cerr << e.what() << "\n\n";
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
    loomAddress = args::get(_loomAddress);
    useNetwork = _net || (defNetwork && _loomDevice.Get().empty());
    maxShafts = args::get(_maxShafts);
    dobbyType = args::get(_dobbyType);
    ascii = args::get(_ascii) || envASCII != nullptr;
    ansi = args::get(_ansi);
    colorAlert = args::get(_bell);
    tabbyPattern = args::get(_tabbyPattern);
    treadleThreading = _threading;
    std::string draftFile = args::get(_draftFile);
    compuDobbyGen = defGen;
    if (dobbyType == DobbyType::Virtual) {
        dobbyType = DobbyType::Positive;
        virtualPositive = true;
    }

    if (check)
        std::cout << "Checking: " << draftFile << std::endl;
    
    if (_cd1)
        compuDobbyGen = 1;
    else if (_cd2)
        compuDobbyGen = 2;
    else if (_cd3)
        compuDobbyGen = 3;
    else if (_cd4)
        compuDobbyGen = 4;

    if (virtualPositive && compuDobbyGen != 4)
        std::cout << "Virtual positive mode is only available with Compu-Dobby IV.\n";
    if (dobbyType == DobbyType::Unspecified && compuDobbyGen != 4) {
        std::cout << "Assuming positive dobby.\n";
        dobbyType = DobbyType::Positive;
    }

    useNetwork = (_net || (defNetwork && _loomDevice.Get().empty())) && (compuDobbyGen == 4);
    if (_net && compuDobbyGen < 4) {
        if (envSocket || !loomDevice.empty()) {
            std::cout << "Network mode is only available with Compu-Dobby IV.\n";
            useNetwork = false;
        } else {
            throw std::runtime_error("Network mode is only available with Compu-Dobby IV.");
        }
    }

    if (compuDobbyGen == 4 && (_maxShafts || envShaft))
        std::cout << "Dobby shaft count will be provided by the loom.\n";
    if (compuDobbyGen != 4 && virtualPositive)
        std::cout << "Only Compu-Dobby IV/4.5 looms can be virtual positive dobbies.\n";

    if (auto draftfileowner = std::ifstream(draftFile)) {
        if (draftFile.ends_with(".wif"))
            draftContents = std::make_unique<wif>(draftfileowner);
        else if (draftFile.ends_with(".dtx"))
            draftContents = std::make_unique<dtx>(draftfileowner);
        else
            throw std::runtime_error("Unknown draft file type (not wif or dtx).");
    } else {
        throw make_system_error("Cannot open draft file");
    }
    
    if (check) {
        driveLoom = false;
        return;
    }

    if (_tabby) {
        std::string tabby = args::get(_tabby);
        if (tabby.length() > (size_t)draftContents->maxShafts)
            std::cerr << "Tabby specification is wider than the draft." << std::endl;

        for (size_t shaft = 0; shaft < tabby.length(); ++shaft) {
            if (tabby[shaft] == 'a')
                tabbyA |= 1ull << shaft;
            else if (tabby[shaft] == 'b')
                tabbyB |= 1ull << shaft;
            else
                throw std::runtime_error("Bad character in tabby specification.");
        }

        if (tabbyA == 0)
            std::cerr << "Tabby A has no shafts set." << std::endl;
        if (tabbyB == 0)
            std::cerr << "Tabby B has no shafts set." << std::endl;
    } else {
        for (size_t shaft = 0; (int)shaft < draftContents->maxShafts; ++shaft) {
            if (shaft & 1)
                tabbyB |= 1ull << shaft;
            else
                tabbyA |= 1ull << shaft;
        }
    }

    tabbyColor = color(args::get(_tabbyColor).c_str());

    parsePicks(args::get(_picks));
    parseEnds(args::get(_ends));

    if (pick <= 0) {
        if (treadleThreading || _sleying) {
            while (pick <= 0)
                pick += draftContents->ends;
        } else {
            while (pick <= 0)
                pick += draftContents->picks;
        }
    }

    if (treadleThreading || _sleying) {
        if (pick == 0 || pick > draftContents->ends)
            throw std::runtime_error("Bad --pick value. Must correspond to an end in the threading.");
        std::vector<size_t> sleying;
        size_t sleyed = 0;
        if (_sleying) {
            treadleSleying = true;
            size_t sleyNum = 0;
            const char* sleyChar = _sleying.Get().c_str();
            const char* sleyEnd = sleyChar + _sleying.Get().length();
            for(;;) {
                auto sleyValid = std::from_chars(sleyChar, sleyEnd, sleyNum);
                if (sleyValid.ec != std::errc{} || sleyNum < 0)
                    throw std::runtime_error("Parse error in --sley option.");
                sleying.push_back(sleyNum);
                sleyed += sleyNum;
                if (sleyValid.ptr == sleyEnd)
                    break;
                if (*sleyValid.ptr != ',')
                    throw std::runtime_error("Parse error in --sley option.");
                sleyChar = sleyValid.ptr + 1;
            };
        } else {
            sleying.push_back(1);
            sleyed = 1;
        }

        size_t pick_z = (size_t)pick;
        draftContents->liftplan.clear();
        draftContents->liftplan.reserve((size_t)draftContents->ends * sleying.size() / sleyed + 2);
        draftContents->liftplan.push_back(0);           // 1-based
        draftContents->weftColor.clear();
        draftContents->weftColor.reserve((size_t)draftContents->ends * sleying.size() / sleyed + 2);
        draftContents->weftColor.push_back(color());    // 1-based
        draftContents->picks = 0;
        draftContents->risingShed = true;
        draftContents->maxTreadles = draftContents->maxShafts;

        for (size_t dentIndex = 0, pickIndex = 1;
             (int)pickIndex <= draftContents->ends; ++dentIndex)
        {
            size_t endsInDent = sleying[dentIndex % sleying.size()];
            // If the starting point is in the current dent then make sure that
            // the whole dent is filled (unless it is the last dent)
            if (pick_z >= pickIndex && pick_z < pickIndex + endsInDent) {
                pick = (int)draftContents->liftplan.size();
                if (reverseTreadle && pick_z != pickIndex + endsInDent - 1 && pick != draftContents->ends)
                    throw std::runtime_error("Start pick is not left-most in the dent.");
                if (!reverseTreadle && pick_z != pickIndex)
                    throw std::runtime_error("Start pick is not right-most in the dent.");
            }
            uint64_t lift = 0;
            if (pickIndex + endsInDent - 1 > (size_t)draftContents->ends)   // last dent might be scant
                endsInDent = (size_t)draftContents->ends - pickIndex + 1;
            for (size_t i = 0; i < endsInDent; ++i)
                lift |= draftContents->threading[pickIndex + i];
            draftContents->liftplan.push_back(lift);
            if (_sleying)
                draftContents->weftColor.push_back(color((double)endsInDent,
                    (double)(pickIndex), (double)(pickIndex + endsInDent - 1)));
            else
                draftContents->weftColor.push_back(draftContents->warpColor[pickIndex]);
            ++draftContents->picks;
            pickIndex += endsInDent;
        }
    }
    
    if (draftContents->maxShafts > maxShafts && compuDobbyGen < 4)
        throw std::runtime_error("Draft file requires more shafts than the loom possesses.");
    
    if (envSocket) {
        IPC::Client fakeLoom(envSocket);
        loomDeviceFD = fakeLoom.release();
    } else if (useNetwork) {
        loomDeviceFD = openTelnet(loomAddress);

        if (loomDeviceFD == -1)
            throw std::runtime_error("Cannot open loom on network.");
        if (loomDeviceFD == -2)
            throw std::runtime_error("Loom address cannot be found on network.");
    } else {
        loomDeviceFD = checkForSerial(loomDevice);

        if (loomDeviceFD == -1)
            throw std::runtime_error("Cannot open loom device.");
        if (loomDeviceFD == -2)
            throw std::runtime_error("Loom device is not a serial port.");
        
        initLoomPort(loomDeviceFD, compuDobbyGen);
    }
    
    if (_log) {
        auto now = std::chrono::system_clock::now();
        auto date = std::chrono::floor<std::chrono::days>(now);
        auto clocktime = std::chrono::floor<std::chrono::seconds>(now - date);
        std::chrono::hh_mm_ss time_of_day{clocktime};
        
        std::string fname = std::format("drawboy_{}-{}.log",
                                        date,
                                        time_of_day);
        auto tmpdir = std::filesystem::temp_directory_path();
        auto tempfile = tmpdir / fname;
        std::cout << "Logging to " << tempfile << "\n";
        logFile = fopen(tempfile.c_str(), "w");
    }
}

Options::~Options()
{
    if (loomDeviceFD >= 0) {
        ::close(loomDeviceFD);
        loomDeviceFD = -1;
    }
    if (logFile) {
        std::fclose(logFile);
        logFile = nullptr;
    }
}
