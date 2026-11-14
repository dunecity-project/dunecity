#include <players/OriginalTeamScript.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Original team VM executes one instruction per tick and preserves delayed state", "[campaign-ai]") {
    OriginalTeamScript::Program program;
    // Synthetic fixture: push10, delay(native0), rewind1, jump0.
    program.offsets = {0, 0, 0, 0, 0};
    program.words = {0x440a, 0x4e00, 0x4c01, 0x8000};
    OriginalTeamScript::State state; program.load(state, 0);
    auto call = [](unsigned function, auto& vm) {
        REQUIRE(function == 0);
        vm.delay = vm.peek() / 5;
        return vm.delay;
    };
    REQUIRE(program.step(state, call)); REQUIRE(state.pc == 1);
    REQUIRE(program.step(state, call)); REQUIRE(state.delay == 2);
    auto restored = state;
    for(int i = 0; i < 20; ++i) {
        REQUIRE(program.step(state, call)); REQUIRE(program.step(restored, call));
        REQUIRE(state.pc == restored.pc); REQUIRE(state.delay == restored.delay);
        REQUIRE(state.sp == restored.sp); REQUIRE(state.stack == restored.stack);
    }
}

TEST_CASE("Original team VM bounds malformed assets and ends Flee", "[campaign-ai]") {
    REQUIRE_THROWS(OriginalTeamScript::Program::parse({}));
    OriginalTeamScript::Program program;
    program.offsets = {0, 0, 0, 0, 0}; program.words = {0x4801};
    OriginalTeamScript::State state;
    REQUIRE_FALSE(program.step(state, [](unsigned, auto&) { return 0; }));
    REQUIRE(state.halted);
    program.words = {0x4c7f}; state = {};
    REQUIRE_THROWS(program.step(state, [](unsigned, auto&) { return 0; }));
    program.words = {0x2f00}; state = {};
    REQUIRE_THROWS(program.step(state, [](unsigned, auto&) { return 0; }));
}
