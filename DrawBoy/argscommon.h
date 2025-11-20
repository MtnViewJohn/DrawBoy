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
#include <numeric>

using std::size_t;
using std::uint64_t;

enum class DobbyType {
    Positive, Negative, Virtual, Unspecified
};

enum class ANSIsupport {
    no,
    yes,
    truecolor,
};

enum class TabbyPattern {
    xAyB,
    AxBy,
    xByA,
    BxAy,
};

enum class ColorAlert {
    None,
    Simple,
    Pulse,
    Alternating
};

enum TabbyPick {
    TabbyA = -1,
    TabbyB = -2
};

inline const char* envLoom = std::getenv("DRAWBOY_LOOMDEVICE");
inline const char* envAddress = std::getenv("DRAWBOY_LOOMADDRESS");
inline const char* envShaft = std::getenv("DRAWBOY_SHAFTS");
inline const char* envDobbyType = std::getenv("DRAWBOY_DOBBYTYPE");
inline const char* envDobbyGen = std::getenv("DRAWBOY_DOBBYGENERATION");
inline const char* envASCII = std::getenv("DRAWBOY_ASCII");
inline const char* envANSI = std::getenv("DRAWBOY_ANSI");
inline const char* envSocket = std::getenv("DRAWBOY_SOCKET");

inline std::unordered_map<std::string, DobbyType> dobbyMap{
    {"positive", DobbyType::Positive},
    {"negative", DobbyType::Negative},
    {"+", DobbyType::Positive},
    {"-", DobbyType::Negative},
    {"virtual", DobbyType::Virtual},
};
inline std::unordered_map<DobbyType, const char*> dobbyName{
    {DobbyType::Positive, "Positive"},
    {DobbyType::Negative, "Negative"},
    {DobbyType::Virtual, "Virtual positive"},
    {DobbyType::Unspecified, "Unspecified"},
};
inline std::unordered_map<std::string, int> shaftMap{
    {"4", 4},
    {"8", 8},
    {"12", 12},
    {"16", 16},
    {"20", 20},
    {"24", 24},
    {"28", 28},
    {"32", 32},
    {"36", 36},
    {"40", 40},
};
inline std::unordered_map<std::string, ANSIsupport> ANSImap{
    {"no", ANSIsupport::no},
    {"yes", ANSIsupport::yes},
    {"truecolor", ANSIsupport::truecolor},
};
inline std::unordered_map<std::string, TabbyPattern> tabbyMap {
    {"xayb", TabbyPattern::xAyB},
    {"axby", TabbyPattern::AxBy},
    {"xbya", TabbyPattern::xByA},
    {"bxay", TabbyPattern::BxAy},
};
inline std::unordered_map<std::string, ColorAlert> alertMap {
    {"none", ColorAlert::None},
    {"simple", ColorAlert::Simple},
    {"pulse", ColorAlert::Pulse},
    {"alternating", ColorAlert::Alternating},
};

inline std::system_error
make_system_error(const char* what) {
  auto ec = std::error_code(errno, std::generic_category());
  return std::system_error(ec, what);
}

inline std::system_error
make_system_error(const std::vector<std::string>& parts) {
  auto ec = std::error_code(errno, std::generic_category());
  return std::system_error(ec, std::accumulate(parts.begin(), parts.end(), std::string()));
}

inline std::runtime_error
make_runtime_error(const std::vector<std::string>& parts)
{
    return std::runtime_error(std::accumulate(parts.begin(), parts.end(), std::string()));
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
