#ifndef ORIGINAL_TEAM_SCRIPT_H
#define ORIGINAL_TEAM_SCRIPT_H

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

// Bounded execution of the instruction subset used by Dune II's TEAM.EMC.
// Format/semantics: Dune Dynasty src/script/script.c (GPL-2.0-or-later).
// Script bytes remain in the user's original game data, not in this header.
namespace OriginalTeamScript {
struct State {
    std::uint32_t pc = 0;
    std::uint16_t delay = 0;
    std::uint16_t result = 0;
    std::uint8_t sp = 15;
    bool halted = false;
    std::array<std::uint16_t, 15> stack{};

    void push(std::uint16_t value) {
        if(sp == 0 || sp > stack.size()) throw std::runtime_error("Campaign team script stack overflow");
        stack[--sp] = value;
    }
    std::uint16_t peek() const {
        if(sp >= stack.size()) throw std::runtime_error("Campaign team script stack underflow");
        return stack[sp];
    }
    std::uint16_t pop() { const auto value = peek(); ++sp; return value; }
};

struct Program {
    std::vector<std::uint16_t> offsets;
    std::vector<std::uint16_t> words;

    static Program parse(const std::vector<std::uint8_t>& bytes) {
        auto be32 = [&](std::size_t pos) {
            if(pos + 4 > bytes.size()) throw std::runtime_error("Truncated campaign team script");
            return (std::uint32_t(bytes[pos]) << 24) | (std::uint32_t(bytes[pos+1]) << 16)
                | (std::uint32_t(bytes[pos+2]) << 8) | bytes[pos+3];
        };
        if(bytes.size() < 12 || bytes.size() > 1024 * 1024 || be32(0) != 0x464f524d
            || be32(8) != 0x454d4332
            // Westwood EMC declares the whole file; standard IFF excludes 8 bytes.
            || (be32(4) != bytes.size() && be32(4) != bytes.size() - 8))
            throw std::runtime_error("Invalid campaign TEAM.EMC container");
        Program program;
        for(std::size_t pos = 12; pos < bytes.size();) {
            const auto tag = be32(pos), size = be32(pos + 4);
            pos += 8;
            if(size > bytes.size() - pos) throw std::runtime_error("Invalid campaign team script chunk");
            if(tag == 0x4f524452 || tag == 0x44415441) { // ORDR / DATA
                auto& output = tag == 0x4f524452 ? program.offsets : program.words;
                if(size % 2 || !output.empty()) throw std::runtime_error("Invalid campaign team word table");
                for(std::size_t i = 0; i < size; i += 2)
                    output.push_back((std::uint16_t(bytes[pos+i]) << 8) | bytes[pos+i+1]);
            }
            pos += size + size % 2;
            if(pos > bytes.size()) throw std::runtime_error("Missing campaign team chunk padding");
        }
        if(program.offsets.size() < 5 || program.words.empty() || program.words.size() > 65536)
            throw std::runtime_error("Missing campaign team program");
        for(const auto offset : program.offsets)
            if(offset >= program.words.size()) throw std::runtime_error("Invalid campaign team entry point");
        return program;
    }

    void load(State& state, unsigned action) const {
        if(action >= offsets.size()) throw std::runtime_error("Invalid campaign team action");
        // Original Script_Reset resets execution/stack pointers, not delay or
        // the backing stack. Preserve those bytes for exact continuation.
        state.pc = offsets[action]; state.sp = 15; state.halted = false;
    }

    template<class Function>
    bool step(State& state, Function function) const {
        if(state.halted) return true;
        if(state.delay) { --state.delay; return true; }
        const auto read = [&]() {
            if(state.pc >= words.size()) throw std::runtime_error("Campaign team instruction out of range");
            return words[state.pc++];
        };
        const std::uint16_t encoded = read();
        unsigned opcode = (encoded >> 8) & 31;
        std::uint16_t parameter = 0;
        if(encoded & 0x8000) { opcode = 0; parameter = encoded & 0x7fff; }
        else if(encoded & 0x4000) parameter = static_cast<std::int8_t>(encoded & 0xff);
        else if(encoded & 0x2000) parameter = read();
        switch(opcode) {
            case 0: state.pc = parameter; break;
            case 1: state.result = parameter; break;
            case 2:
                if(parameter != 0) throw std::runtime_error("Unsupported campaign team call frame");
                state.push(state.result); break;
            case 3: case 4: state.push(parameter); break;
            case 8:
                if(parameter == 0) state.result = state.pop();
                else if(parameter == 1) {
                    // Flee ends immediately. The original reports stack
                    // underflow here and stops this team's script.
                    state.halted = true; return false;
                } else throw std::runtime_error("Invalid campaign team return");
                break;
            case 12:
                if(parameter > 15 || state.sp + parameter > 15)
                    throw std::runtime_error("Campaign team stack rewind out of range");
                state.sp += parameter; break;
            case 14:
                if(parameter > 14) throw std::runtime_error("Unsupported campaign team function");
                state.result = function(parameter, state); break;
            case 15: if(state.pop() == 0) state.pc = parameter & 0x7fff; break;
            case 16:
                if(parameter != 0) throw std::runtime_error("Unsupported campaign team unary operation");
                state.push(state.pop() == 0); break;
            case 17: {
                const auto right = static_cast<std::int16_t>(state.pop());
                const auto left = static_cast<std::int16_t>(state.pop());
                switch(parameter) {
                    case 2: state.push(left == right); break;
                    case 3: state.push(left != right); break;
                    case 4: state.push(left < right); break;
                    case 5: state.push(left <= right); break;
                    case 6: state.push(left > right); break;
                    case 7: state.push(left >= right); break;
                    default: throw std::runtime_error("Unsupported campaign team binary operation");
                }
                break;
            }
            default: throw std::runtime_error("Unsupported campaign team instruction");
        }
        return true;
    }
};
} // namespace OriginalTeamScript
#endif
