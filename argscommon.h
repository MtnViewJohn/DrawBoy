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

struct ci_equal {
    constexpr bool operator() (const std::string & s1, const std::string & s2) const {
        if (s1.length() != s2.length()) return false;
        for (std::size_t i = 0; i < s1.length(); ++i)
            if (std::tolower((unsigned char)s1[i]) != std::tolower((unsigned char)s2[i]))
                return false;
        return true;
    }
};
struct ci_hash {
    constexpr std::size_t operator()(const std::string& s) const {
        std::string t = s;
        for (char& c: t)
            c = (char)std::tolower((unsigned char)c);
        auto hashfn = std::hash<std::string>();
        return hashfn(t);
    }
};

inline std::unordered_map<std::string, DobbyType, ci_hash, ci_equal> dobbyMap{
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
inline std::unordered_map<std::string, ANSIsupport, ci_hash, ci_equal> ANSImap{
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
    std::size_t len = 1;
    for (auto&& part: parts)
        len += part.length();
    msg.reserve(len);
    for (auto&& part: parts)
        msg.append(part);
    return std::runtime_error(msg);
}



#endif /* argscommon_h */
