#ifndef INIMAP_ZONESTARTLEVEL_H
#define INIMAP_ZONESTARTLEVEL_H

#include <string>

/**
    Optional starting development level of a pre-placed DuneCity zone.

    A [STRUCTURES] entry is "ID###=House,Building,Health,Position". A zone may carry one more
    field, "...,Position,Level", with Level 0..3 (the zone's density; a residential lot gets the
    matching apartment population). Entries without it - every map written before this field
    existed - parse to -1 and load exactly as before: an empty lot at level 0. Older builds read
    only the first four fields and ignore the fifth, so maps that use it still load there too.
*/
namespace ZoneStartLevel {

constexpr int kMaxLevel = 3;

/// The level carried by @a entryValue, or -1 when it has no valid fifth field.
inline int parse(const std::string& entryValue) {
    std::string::size_type fieldStart = std::string::npos;
    int commas = 0;
    for(std::string::size_type i = 0; i < entryValue.size(); ++i) {
        if(entryValue[i] == ',' && ++commas == 4) fieldStart = i + 1;
    }
    if(commas != 4) return -1;
    std::string::size_type begin = fieldStart, end = entryValue.size();
    while(begin < end && (entryValue[begin] == ' ' || entryValue[begin] == '\t')) ++begin;
    while(end > begin && (entryValue[end - 1] == ' ' || entryValue[end - 1] == '\t')) --end;
    if(begin == end || end - begin > 1) return -1;
    const char digit = entryValue[begin];
    if(digit < '0' || digit > '0' + kMaxLevel) return -1;
    return digit - '0';
}

/// The suffix the map writer appends to a zone entry.
inline std::string format(int level) { return "," + std::to_string(level); }

}

#endif // INIMAP_ZONESTARTLEVEL_H
