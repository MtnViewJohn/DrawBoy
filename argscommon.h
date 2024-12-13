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
#include <unordered_map>
#include <system_error>

enum class DobbyType {
    Positive, Negative
};

inline const char* envLoom = std::getenv("DRAWBOY_LOOMDEVICE");
inline const char* envShaft = std::getenv("DRAWBOY_SHAFTS");
inline const char* envDobby = std::getenv("DRAWBOY_DOBBY");
inline const char* envASCII = std::getenv("DRAWBOY_ASCII");
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

std::system_error makesystem_error(const char* what);

#endif /* argscommon_h */
