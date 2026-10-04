/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GAMESTATEDIGEST_H
#define GAMESTATEDIGEST_H

/**
    A periodic fingerprint of the deterministic simulation state.

    What this is for: lockstep only works while every peer computes the same thing from the same
    commands. Without a digest a divergence shows up as two players disagreeing about the game
    with no way to tell when it started. Packet counters cannot do this job - they say that
    messages arrived, not that the simulations agree.

    What goes in, in exactly this order and these widths:

      - the game cycle,
      - the shared random generator's seed,
      - for every house slot: whether it exists, its credits, its structure count, its unit count,
        its spice income multiplier,
      - the number of live objects,
      - for every object in ascending object id: id, item id, original house, owner house,
        raw fixed-point health, tile x, tile y.

    What is deliberately left out: anything drawn, anything measured in wall-clock time, anything
    that depends on which player is local, and every cache or lookup table that is derived from
    the state above. Those differ legitimately between two peers of one match.

    The hash is FNV-1a over explicitly sized little-endian fields, so it does not depend on the
    host's endianness, on struct padding, or on the order a container happens to iterate in.
*/

// No SDL, no ENet, no game headers: the standalone relay harness compiles this unchanged where
// size_t is 32 bits.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace GameStateDigest {

/// One peer's view of the simulation at one game cycle.
struct Digest {
    std::uint32_t gameCycle   = 0;
    std::uint32_t randomSeed  = 0;
    std::uint32_t objectCount = 0;
    std::uint64_t objectHash  = 0;
    std::uint64_t houseHash   = 0;

    bool operator==(const Digest& other) const {
        return gameCycle == other.gameCycle && randomSeed == other.randomSeed
            && objectCount == other.objectCount && objectHash == other.objectHash
            && houseHash == other.houseHash;
    }
    bool operator!=(const Digest& other) const { return !(*this == other); }

    /// True when the two digests describe the same cycle but disagree about it.
    bool divergesFrom(const Digest& other) const {
        return gameCycle == other.gameCycle && *this != other;
    }
};

/// FNV-1a, fed only explicitly sized values in an explicit order.
class Hasher {
public:
    void mixUint8(std::uint8_t value) {
        state_ ^= static_cast<std::uint64_t>(value);
        state_ *= 1099511628211ULL;
    }

    void mixUint32(std::uint32_t value) {
        for(int shift = 0; shift < 32; shift += 8) {
            mixUint8(static_cast<std::uint8_t>((value >> shift) & 0xFF));
        }
    }

    void mixUint64(std::uint64_t value) {
        for(int shift = 0; shift < 64; shift += 8) {
            mixUint8(static_cast<std::uint8_t>((value >> shift) & 0xFF));
        }
    }

    /// Signed values are mixed through their two's complement bit pattern, not through a cast
    /// that would be implementation defined for negative numbers.
    void mixInt32(std::int32_t value) { mixUint32(static_cast<std::uint32_t>(value)); }
    void mixInt64(std::int64_t value) { mixUint64(static_cast<std::uint64_t>(value)); }

    std::uint64_t value() const { return state_; }

private:
    std::uint64_t state_ = 14695981039346656037ULL;
};

/// Bytes on the wire. Carried in the relay diagnostic envelope, never as a game packet.
constexpr std::size_t kEncodedSize = 4 + 4 + 4 + 8 + 8;

inline void encode(const Digest& digest, std::uint8_t* out) {
    std::size_t position = 0;
    const auto writeUint32 = [&](std::uint32_t value) {
        for(int shift = 0; shift < 32; shift += 8) {
            out[position++] = static_cast<std::uint8_t>((value >> shift) & 0xFF);
        }
    };
    const auto writeUint64 = [&](std::uint64_t value) {
        for(int shift = 0; shift < 64; shift += 8) {
            out[position++] = static_cast<std::uint8_t>((value >> shift) & 0xFF);
        }
    };

    writeUint32(digest.gameCycle);
    writeUint32(digest.randomSeed);
    writeUint32(digest.objectCount);
    writeUint64(digest.objectHash);
    writeUint64(digest.houseHash);
}

inline bool decode(const std::uint8_t* data, std::size_t length, Digest& digest) {
    if(data == nullptr || length != kEncodedSize) {
        return false;
    }

    std::size_t position = 0;
    const auto readUint32 = [&]() {
        std::uint32_t value = 0;
        for(int shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(data[position++]) << shift;
        }
        return value;
    };
    const auto readUint64 = [&]() {
        std::uint64_t value = 0;
        for(int shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(data[position++]) << shift;
        }
        return value;
    };

    digest.gameCycle   = readUint32();
    digest.randomSeed  = readUint32();
    digest.objectCount = readUint32();
    digest.objectHash  = readUint64();
    digest.houseHash   = readUint64();
    return true;
}

/// Short, stable text for a log line or a save-to-file comparison.
inline std::string describe(const Digest& digest) {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "cycle %u seed %08x objects %u %016llx/%016llx",
                 static_cast<unsigned>(digest.gameCycle),
                 static_cast<unsigned>(digest.randomSeed),
                 static_cast<unsigned>(digest.objectCount),
                 static_cast<unsigned long long>(digest.objectHash),
                 static_cast<unsigned long long>(digest.houseHash));
    return std::string(buffer);
}

/// How often a digest is produced during a match, in game cycles.
constexpr std::uint32_t kDigestIntervalCycles = 200;

/// How many of our own recent digests are kept so a peer's can be matched to one.
constexpr std::size_t kDigestHistory = 16;

} // namespace GameStateDigest

#endif // GAMESTATEDIGEST_H
