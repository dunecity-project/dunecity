#ifndef UNALIGNED_H
#define UNALIGNED_H

#include <SDL_endian.h>
#include <cstring>

// Integer loads from byte buffers (file formats, packets) at offsets with no alignment
// guarantee. Dereferencing such an offset as a Uint16* or Uint32* is undefined behaviour.

inline Uint16 loadLE16(const void* p) {
    Uint16 value;
    std::memcpy(&value, p, sizeof(value));
    return SDL_SwapLE16(value);
}

inline Uint32 loadLE32(const void* p) {
    Uint32 value;
    std::memcpy(&value, p, sizeof(value));
    return SDL_SwapLE32(value);
}

inline Uint32 loadBE32(const void* p) {
    Uint32 value;
    std::memcpy(&value, p, sizeof(value));
    return SDL_SwapBE32(value);
}

#endif // UNALIGNED_H
