#include <misc/AnalyticsInstallID.h>

#include <FileClasses/INIFile.h>

#include <cstdint>
#include <exception>
#include <random>

namespace AnalyticsInstallID {

bool isValid(const std::string& id) {
    if(id.size() != 32) return false;
    for(const char c : id) {
        if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

std::string generate() {
    static constexpr char digits[] = "0123456789abcdef";
    std::string id;
    try {
        // The operating system's secure generator on every platform we ship: arc4random or
        // /dev/urandom (macOS, iOS, Android, Linux), rand_s (MSVC), crypto.getRandomValues (browser).
        std::random_device device;
        for(int word = 0; word < 4; ++word) {
            std::uint32_t bits = static_cast<std::uint32_t>(device());
            for(int nibble = 0; nibble < 8; ++nibble) {
                id += digits[bits & 0xFu];
                bits >>= 4;
            }
        }
    } catch(const std::exception&) {
        // No random source: send no id rather than a weak one.
        return {};
    }
    return id;
}

std::string loadOrCreate(INIFile& config, bool& created) {
    created = false;
    const std::string stored = config.getStringValue(section, key, "");
    if(isValid(stored)) return stored;
    std::string id = generate();
    if(!isValid(id)) return {};
    config.setStringValue(section, key, id);
    created = true;
    return id;
}

std::string playerRowField(bool reportingClientsHumanPlayer, const std::string& id) {
    if(!reportingClientsHumanPlayer || !isValid(id)) return {};
    return ",\"install_id\":\"" + id + '"';
}

}
