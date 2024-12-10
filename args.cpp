/*
 *  args.cpp
 *  DrawBoy
 */

/* Copyright (c) 2024 John Horigan <john@glyphic.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

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
enumSerial(std::vector<std::string>& exclude)
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
                if (std::find(exclude.begin(), exclude.end(), dname) ==
                    exclude.end())
                {
                    std::cout << dname << std::endl;
                }
            }
        }
        entry = readdir(devDir);
    }
    closedir(devDir);
}


std::vector<std::string>
readfile(std::istream& in, const std::string& commentPrefix)
{
    std::vector<std::string> buf;
    std::string line;
    if (!in) return buf;
    while (std::getline(in, line)) {
        line.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
        line.erase(line.find_last_not_of(" \t\n\r\f\v") + 1);
        if (line.empty() || line.find(commentPrefix) == 0)
            continue;
        buf.emplace_back(line);
    }
    return buf;
}

void
initLoomPort(int fd)
{
    struct termios term;

    if (tcgetattr(fd, &term) < 0)
        throw makesystem_error("Cannot communicate with loom device");
    
    cfmakeraw(&term);
    cfsetispeed(&term, B9600);          // set 9600 baud
    cfsetospeed(&term, B9600);
    term.c_cflag &= ~PARENB;            // set 8N1
    term.c_cflag &= ~CSTOPB;
    term.c_cflag = (term.c_cflag & ~CSIZE) | CS8;
    
    if (tcsetattr(fd, TCSAFLUSH, &term) < 0)
        throw makesystem_error("Cannot communicate with loom device");
}

}

std::system_error
makesystem_error(const char* what) {
  auto ec = std::error_code(errno, std::generic_category());
  return std::system_error(ec, what);
}

int
Options::getOptions(int argc, const char * argv[])
{
    std::unordered_map<std::string, Options::DobbyType> dobbyMap{
        {"positive", Options::DobbyType::Positive},
        {"negative", Options::DobbyType::Negative},
        {"+", Options::DobbyType::Positive},
        {"-", Options::DobbyType::Negative},
    };
    std::unordered_map<std::string, int> shaftMap{
        {"8", 8},
        {"12", 12},
        {"16", 16},
        {"24", 24},
        {"32", 32},
        {"40", 40},
    };
    
    const char* envLoom = std::getenv("DRAWBOY_LOOMDEVICE");
    const char* envShaft = std::getenv("DRAWBOY_SHAFTS");
    const char* envDobby = std::getenv("DRAWBOY_DOBBY");
    
    if (!envLoom) envLoom = "";
    
    auto f1 = envDobby ? dobbyMap.find(envDobby) : dobbyMap.end();
    Options::DobbyType defDobby = f1 != dobbyMap.end() ? f1->second : Options::DobbyType::Positive;
    
    auto f2 = envShaft ? shaftMap.find(envShaft) : shaftMap.end();
    int defShaft = f2 != shaftMap.end() ? f2->second : 0;
    
    args::ArgumentParser parser("AVL CompuDobby III loom driver.", "Report errors to John Horigan <john@glyphic.com>.");
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
    args::CompletionFlag completion(parser, {"complete"});
    args::Flag findloom(parser, "find loom", "Finds device files that might be the loom.", {"findloom"}, args::Options::KickOut);
    args::ValueFlag<std::string> _loomDevice(parser, "LOOM PATH",
        "The path of the loom device in the /dev directory", {'l', "loomDevice"},
        envLoom, *envLoom ? args::Options::None : args::Options::Required);
    args::ValueFlag<std::string> _wifFile(parser, "WIF PATH",
        "The path of the WIF file", {'f', "wif"}, "", args::Options::Required);
    args::MapFlag<std::string, int> _maxShafts(parser, "SHAFT COUNT",
        "Number of shafts on the loom", {'s', "shafts"}, shaftMap, defShaft,
        defShaft ? args::Options::None : args::Options::Required);
    args::MapFlag<std::string, Options::DobbyType> _dobbyType(parser, "DOBBY TYPE", "Is the loom a positive or negative dobby (+ and - are also accepted)", {'t', "dobbyType"}, dobbyMap, defDobby);
    args::ValueFlag<int> _pick(parser, "PICK",
        "The pick to start weaving at (defaults to 1).", {'p', "pick"}, 1);
    args::ValueFlag<std::string> _picks(parser, "PICK LIST",
        "List of pick ranges in the treadling or liftplan to weave.", {'P', "picks"}, "");
    try
    {
        parser.Prog("DrawBoy");
        parser.ParseCLI(argc, argv);
    }
    catch (const args::Completion& e)
    {
        std::cout << e.what();
        return 0;
    }
    catch (const args::Help&)
    {
        std::cout << parser;
        return 0;
    }
    catch (const args::ParseError& e)
    {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    catch (const args::RequiredError& e)
    {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    
    if (findloom) {
        std::vector<std::string> exclude;
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
    maxShafts = args::get(_maxShafts);

    if (_picks) {
        std::stringstream ss(args::get(_picks));
        std::string range;
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
                        for (int pick = start; pick <= end; ++pick)
                            picks.push_back(pick);
                    } else {
                        for (int pick = end; pick >= start; --pick)
                            picks.push_back(pick);
                    }
                }
            } catch (std::runtime_error& rte) {
                throw;
            } catch (...) {
                throw std::runtime_error("Syntax error in treadling range.");
            }
        }
    }
    
    wifFile = args::get(_wifFile);
    wifFileStream = std::fopen(wifFile.c_str(), "r");
    
    if (wifFileStream == nullptr)
        throw makesystem_error("Cannot open wif file");
    
    loomDeviceFD = checkForSerial(loomDevice);
    
    if (loomDeviceFD == 1)
        throw std::runtime_error("Cannot open loom device.");
    if (loomDeviceFD == 2)
        throw std::runtime_error("Loom device is not a serial port.");

    initLoomPort(loomDeviceFD);

    valid = true;
    return 0;
}

Options::~Options()
{
    close (loomDeviceFD);
    if (wifFileStream != nullptr)
        std::fclose(wifFileStream);
}
