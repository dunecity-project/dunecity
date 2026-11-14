#include <catch2/catch_test_macros.hpp>
#include <misc/Hotkeys.h>

#include <set>

using Hotkeys::Action;
using Hotkeys::Binding;
namespace Mod = Hotkeys::Mod;

namespace {
const auto always = [](Action) { return true; };

std::optional<Action> pressed(const Hotkeys::Bindings& bindings, SDL_Keycode key, Uint16 mods = KMOD_NONE,
                              bool wasd = false, const std::function<bool(Action)>& available = always) {
    const auto match = bindings.match(key, mods, wasd, available);
    return match ? std::optional<Action>(match->action) : std::nullopt;
}
}

TEST_CASE("Defaults use distinct single keys around WASD without camera conflicts", "[hotkeys]") {
    const auto defaults = Hotkeys::Bindings::defaults();
    REQUIRE(defaults.conflicts().empty());
    std::set<std::string> keys;
    for(int i = 0; i < Hotkeys::actionCount; ++i) {
        const auto& info = Hotkeys::actions()[i];
        REQUIRE(static_cast<int>(info.action) == i);
        REQUIRE(keys.insert(info.iniKey).second);
        REQUIRE(info.defaultBinding.bound());
        REQUIRE(info.defaultBinding.mods == 0);
        REQUIRE_FALSE(Hotkeys::needsShiftWithWasd(info.defaultBinding.key));
        REQUIRE(Hotkeys::reservedReason(info.defaultBinding) == nullptr);
        for(bool wasd : {false, true}) {
            REQUIRE(defaults.effective(info.action, wasd) == info.defaultBinding);
            REQUIRE(pressed(defaults, info.defaultBinding.key, KMOD_NONE, wasd) == info.action);
        }
    }
    REQUIRE(pressed(defaults, SDLK_q, KMOD_NONE, true) == Action::AttackMove);
    REQUIRE(pressed(defaults, SDLK_e, KMOD_NONE, true) == Action::Stop);
    REQUIRE(pressed(defaults, SDLK_r, KMOD_NONE, true) == Action::Repair);
    REQUIRE(pressed(defaults, SDLK_f, KMOD_NONE, true) == Action::Guard);
    REQUIRE(pressed(defaults, SDLK_g, KMOD_NONE, true) == Action::AreaGuard);
    REQUIRE(pressed(defaults, SDLK_z, KMOD_NONE, true) == Action::Patrol);
    REQUIRE(pressed(defaults, SDLK_x, KMOD_NONE, true) == Action::Hold);
    REQUIRE(pressed(defaults, SDLK_c, KMOD_NONE, true) == Action::Capture);
    REQUIRE(pressed(defaults, SDLK_v, KMOD_NONE, true) == Action::Move);
    REQUIRE(pressed(defaults, SDLK_y, KMOD_NONE, true) == Action::NextConstructionYard);
    for(auto key : {SDLK_w, SDLK_a, SDLK_s, SDLK_d}) {
        REQUIRE_FALSE(pressed(defaults, key, KMOD_NONE, true));
        REQUIRE_FALSE(pressed(defaults, key, KMOD_LSHIFT, true));
    }
}

TEST_CASE("Optional Shift still queues orders and custom modifiers still work", "[hotkeys]") {
    auto bindings = Hotkeys::Bindings::defaults();
    const auto queuedHold = bindings.match(SDLK_x, KMOD_LSHIFT, true, always);
    REQUIRE(queuedHold);
    REQUIRE(queuedHold->action == Action::Hold);
    REQUIRE(queuedHold->extraShift);
    REQUIRE_FALSE(bindings.match(SDLK_x, KMOD_NONE, true, always)->extraShift);
    REQUIRE_FALSE(pressed(bindings, SDLK_f, KMOD_LCTRL));
    REQUIRE_FALSE(pressed(bindings, SDLK_f, KMOD_LGUI));
    REQUIRE_FALSE(pressed(bindings, SDLK_q, KMOD_LGUI));
    REQUIRE_FALSE(pressed(bindings, SDLK_5));
    REQUIRE_FALSE(pressed(bindings, SDLK_SPACE));
    REQUIRE(pressed(bindings, SDLK_f, KMOD_CAPS | KMOD_NUM) == Action::Guard);
    Hotkeys::assign(bindings, Action::Guard, {SDLK_f, Mod::Ctrl});
    REQUIRE(pressed(bindings, SDLK_f, KMOD_RCTRL, true) == Action::Guard);
    REQUIRE_FALSE(pressed(bindings, SDLK_f, KMOD_NONE, true));
}

TEST_CASE("Custom camera-key bindings retain their explicit WASD handling", "[hotkeys]") {
    auto bindings = Hotkeys::Bindings::defaults();
    Hotkeys::assign(bindings, Action::Stop, {SDLK_s, 0});
    REQUIRE(pressed(bindings, SDLK_s) == Action::Stop);
    REQUIRE_FALSE(pressed(bindings, SDLK_s, KMOD_NONE, true));
    REQUIRE(pressed(bindings, SDLK_s, KMOD_LSHIFT, true) == Action::Stop);
    REQUIRE(pressed(bindings, SDLK_q, KMOD_NONE, true) == Action::AttackMove);
}

TEST_CASE("Unavailable city actions do not intercept a custom Shift order", "[hotkeys]") {
    auto bindings = Hotkeys::Bindings::defaults();
    Hotkeys::assign(bindings, Action::CityStatsOverlay, {SDLK_c, Mod::Shift});
    const auto noCity = [](Action action) { return action != Action::CityStatsOverlay; };
    REQUIRE(pressed(bindings, SDLK_c, KMOD_LSHIFT) == Action::CityStatsOverlay);
    REQUIRE(pressed(bindings, SDLK_c, KMOD_LSHIFT, false, noCity) == Action::Capture);
}

TEST_CASE("A rebound action answers only to its new key", "[hotkeys]") {
    auto bindings = Hotkeys::Bindings::defaults();
    auto result = Hotkeys::assign(bindings, Action::Guard, Binding{SDLK_DELETE, 0});
    REQUIRE(result.outcome == Hotkeys::AssignOutcome::Assigned);
    REQUIRE_FALSE(result.displaced);
    REQUIRE_FALSE(pressed(bindings, SDLK_f));
    REQUIRE(pressed(bindings, SDLK_DELETE) == Action::Guard);
    REQUIRE(pressed(bindings, SDLK_g, KMOD_NONE) == Action::AreaGuard);

    // Taking a key from another action leaves that action unbound rather than sharing it.
    result = Hotkeys::assign(bindings, Action::Guard, Binding{SDLK_y, 0});
    REQUIRE(result.outcome == Hotkeys::AssignOutcome::Assigned);
    REQUIRE(result.displaced == Action::NextConstructionYard);
    REQUIRE_FALSE(bindings.get(Action::NextConstructionYard).bound());
    REQUIRE(pressed(bindings, SDLK_y) == Action::Guard);
    REQUIRE_FALSE(pressed(bindings, SDLK_DELETE));
    REQUIRE(bindings.conflicts().empty());

    REQUIRE(Hotkeys::assign(bindings, Action::Guard, Binding{SDLK_y, 0}).outcome == Hotkeys::AssignOutcome::Unchanged);
    REQUIRE(Hotkeys::assign(bindings, Action::Guard, Binding{}).outcome == Hotkeys::AssignOutcome::Assigned);
    REQUIRE_FALSE(pressed(bindings, SDLK_y));
}

TEST_CASE("Reserved chords and WASD-only collisions are refused", "[hotkeys]") {
    auto bindings = Hotkeys::Bindings::defaults();
    bindings.set(Action::Stop, {SDLK_s, 0});
    const auto before = bindings;
    // Custom Stop is S, which becomes Shift+S with the WASD camera.
    auto result = Hotkeys::assign(bindings, Action::Guard, Binding{SDLK_s, Mod::Shift});
    REQUIRE(result.outcome == Hotkeys::AssignOutcome::Rejected);
    REQUIRE(result.reason.find("WASD camera is on") != std::string::npos);
    REQUIRE(Hotkeys::assign(bindings, Action::PlaceStructure, Binding{SDLK_p, Mod::Ctrl}).outcome
            == Hotkeys::AssignOutcome::Rejected);
    REQUIRE(Hotkeys::assign(bindings, Action::Guard, Binding{SDLK_5, 0}).outcome == Hotkeys::AssignOutcome::Rejected);
    REQUIRE(bindings == before);
}

TEST_CASE("Hotkey capture cancels, waits for a key and refuses fixed keys", "[hotkeys]") {
    REQUIRE(Hotkeys::capture(SDLK_ESCAPE, KMOD_NONE).kind == Hotkeys::CaptureKind::Cancel);
    REQUIRE(Hotkeys::capture(SDLK_LSHIFT, KMOD_LSHIFT).kind == Hotkeys::CaptureKind::Waiting);
    REQUIRE(Hotkeys::capture(SDLK_RCTRL, KMOD_RCTRL).kind == Hotkeys::CaptureKind::Waiting);
    for(const SDL_Keycode fixed : {SDLK_1, SDLK_F5, SDLK_SPACE, SDLK_RETURN, SDLK_TAB, SDLK_LEFT, SDLK_MINUS, SDLK_PRINTSCREEN}) {
        const auto captured = Hotkeys::capture(fixed, KMOD_NONE);
        REQUIRE(captured.kind == Hotkeys::CaptureKind::Rejected);
        REQUIRE_FALSE(captured.reason.empty());
    }
    REQUIRE(Hotkeys::capture(SDLK_k, KMOD_LGUI).kind == Hotkeys::CaptureKind::Rejected);
    REQUIRE(Hotkeys::capture(SDLK_p, KMOD_LCTRL).kind == Hotkeys::CaptureKind::Rejected);
    const auto shifted = Hotkeys::capture(SDLK_k, KMOD_RSHIFT | KMOD_CAPS);
    REQUIRE(shifted.kind == Hotkeys::CaptureKind::Captured);
    REQUIRE(shifted.binding == Binding{SDLK_k, Mod::Shift});
    const auto punctuation = Hotkeys::capture(SDLK_SEMICOLON, KMOD_LCTRL | KMOD_LALT);
    REQUIRE(punctuation.kind == Hotkeys::CaptureKind::Captured);
    REQUIRE(Hotkeys::displayName(punctuation.binding) == "Ctrl+Alt+;");
}

TEST_CASE("Hotkey config round-trips and recovers from missing or bad values", "[hotkeys]") {
    auto custom = Hotkeys::Bindings::defaults();
    Hotkeys::assign(custom, Action::Guard, Binding{SDLK_SEMICOLON, Mod::Ctrl | Mod::Alt});
    Hotkeys::assign(custom, Action::Stop, Binding{});
    Hotkeys::assign(custom, Action::NextFactory, Binding{SDLK_PAGEDOWN, Mod::Shift});
    const auto entries = custom.toEntries();
    REQUIRE(entries.at("Guard") == "Ctrl+Alt+Semicolon");
    REQUIRE(entries.at("Stop") == "None");
    const auto reloaded = Hotkeys::load(entries);
    REQUIRE(reloaded.warnings.empty());
    REQUIRE(reloaded.bindings == custom);

    const auto empty = Hotkeys::load({});
    REQUIRE(empty.warnings.empty());
    REQUIRE(empty.bindings == Hotkeys::Bindings::defaults());

    const auto bad = Hotkeys::load({{"Guard", "Hyper+G"}, {"Area Guard", "Shift+"}, {"Stop", "F5"},
                                    {"Move", "5"}, {"Place Structure", "Ctrl+P"}, {"Patrol", "shift + shift + q"},
                                    {"Capture", " shift+k "}, {"Unknown Action", "K"}});
    REQUIRE(bad.warnings.size() == 6);
    for(const Action action : {Action::Guard, Action::AreaGuard, Action::Stop, Action::Move,
                               Action::PlaceStructure, Action::Patrol})
        REQUIRE(bad.bindings.get(action) == Hotkeys::info(action).defaultBinding);
    REQUIRE(bad.bindings.get(Action::Capture) == Binding{SDLK_k, Mod::Shift});
}

TEST_CASE("Colliding config entries leave one action on the key", "[hotkeys]") {
    // A configured key wins over another action's default.
    const auto configuredWins = Hotkeys::load({{"Guard", "Y"}});
    REQUIRE(configuredWins.bindings.get(Action::Guard) == Binding{SDLK_y, 0});
    REQUIRE_FALSE(configuredWins.bindings.get(Action::NextConstructionYard).bound());
    REQUIRE(configuredWins.warnings.size() == 1);
    REQUIRE(configuredWins.bindings.conflicts().empty());

    // Two configured actions on one key: the earlier one in the registry keeps it.
    const auto bothConfigured = Hotkeys::load({{"Guard", "K"}, {"Stop", "K"}});
    REQUIRE(bothConfigured.bindings.get(Action::Stop) == Binding{SDLK_k, 0});
    REQUIRE_FALSE(bothConfigured.bindings.get(Action::Guard).bound());
    REQUIRE(bothConfigured.bindings.conflicts().empty());

    // A collision that only exists with the WASD camera is still resolved.
    const auto wasdOnly = Hotkeys::load({{"Guard", "Shift+S"}, {"Stop", "S"}});
    REQUIRE(wasdOnly.bindings.conflicts().empty());
    REQUIRE_FALSE(wasdOnly.bindings.get(Action::Guard).bound());
    REQUIRE(wasdOnly.bindings.get(Action::Stop) == Binding{SDLK_s, 0});
}
