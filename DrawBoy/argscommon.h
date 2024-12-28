//
//  argscommon.h
//  DrawBoy
//
//  Created by John Horigan on 12/13/24.
//

#ifndef argscommon_h
#define argscommon_h

#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_map>
#include <system_error>

using std::size_t;
using std::uint64_t;

enum class DobbyType {
    Positive, Negative
};

enum class ANSIsupport {
    no,
    yes,
    truecolor,
};

inline const char* envLoom = std::getenv("DRAWBOY_LOOMDEVICE");
inline const char* envShaft = std::getenv("DRAWBOY_SHAFTS");
inline const char* envDobby = std::getenv("DRAWBOY_DOBBY");
inline const char* envASCII = std::getenv("DRAWBOY_ASCII");
inline const char* envANSI = std::getenv("DRAWBOY_ANSI");
inline const char* envSocket = std::getenv("DRAWBOY_SOCKET");

inline std::unordered_map<std::string, DobbyType> dobbyMap{
    {"positive", DobbyType::Positive},
    {"negative", DobbyType::Negative},
    {"+", DobbyType::Positive},
    {"-", DobbyType::Negative},
};
inline std::unordered_map<std::string, int> shaftMap{
    {"8", 8},
    {"12", 12},
    {"16", 16},
    {"24", 24},
    {"32", 32},
    {"40", 40},
};
inline std::unordered_map<std::string, ANSIsupport> ANSImap{
    {"no", ANSIsupport::no},
    {"yes", ANSIsupport::yes},
    {"truecolor", ANSIsupport::truecolor},
};

inline std::system_error
make_system_error(const char* what) {
  auto ec = std::error_code(errno, std::generic_category());
  return std::system_error(ec, what);
}

inline std::runtime_error
make_runtime_error(std::vector<std::string> parts)
{
    std::string msg;
    size_t len = 1;
    for (auto&& part: parts)
        len += part.length();
    msg.reserve(len);
    for (auto&& part: parts)
        msg.append(part);
    return std::runtime_error(msg);
}

struct ToLowerReader {
    std::string operator ()(const char * value)
    {
        std::string destination = value;
        for (char& c: destination)
            c = (char)std::tolower((unsigned char)c);
        return destination;
    }
    bool operator ()(const std::string &, const std::string &value, std::string &destination) const
    {
        destination = value;
        for (char& c: destination)
            c = (char)std::tolower((unsigned char)c);
        return true;
    }
};


#endif /* argscommon_h */