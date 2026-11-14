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

TEST_CASE("Default hotkeys are distinct, bindable and give Guard G, Area Guard Shift+G, Stop S", "[hotkeys]") {
    const auto defaults = Hotkeys::Bindings::defaults();
    REQUIRE(defaults.conflicts().empty());
    std::set<std::string> keys;
    for(int i = 0; i < Hotkeys::actionCount; ++i) {
        const auto& info = Hotkeys::actions()[i];
        REQUIRE(static_cast<int>(info.action) == i);
        REQUIRE(keys.insert(info.iniKey).second);
        REQUIRE(info.defaultBinding.bound());
        REQUIRE(Hotkeys::reservedReason(info.defaultBinding) == nullptr);
    }
    REQUIRE(Hotkeys::displayName(defaults.get(Action::Guard)) == "G");
    REQUIRE(Hotkeys::displayName(defaults.get(Action::AreaGuard)) == "Shift+G");
    REQUIRE(Hotkeys::displayName(defaults.get(Action::Stop)) == "S");
    REQUIRE(Hotkeys::displayName(defaults.effective(Action::Stop, true)) == "Shift+S");
    REQUIRE(Hotkeys::displayName(defaults.get(Action::NextConstructionYard)) == "Y");
}

TEST_CASE("Key presses dispatch to named actions with explicit modifier intent", "[hotkeys]") {
    const auto bindings = Hotkeys::Bindings::defaults();
    REQUIRE(pressed(bindings, SDLK_g) == Action::Guard);
    REQUIRE(pressed(bindings, SDLK_g, KMOD_LSHIFT) == Action::AreaGuard);
    REQUIRE(pressed(bindings, SDLK_s) == Action::Stop);
    REQUIRE(pressed(bindings, SDLK_y) == Action::NextConstructionYard);
    REQUIRE(pressed(bindings, SDLK_t, KMOD_RCTRL) == Action::SelectSameTypeOnMap);
    REQUIRE(pressed(bindings, SDLK_f, KMOD_LCTRL | KMOD_LSHIFT) == Action::NextFormationShape);
    // Shift on top of an order key keeps its queue meaning: it reaches the action, flagged.
    const auto queuedHold = bindings.match(SDLK_h, KMOD_LSHIFT, false, always);
    REQUIRE(queuedHold);
    REQUIRE(queuedHold->action == Action::Hold);
    REQUIRE(queuedHold->extraShift);
    REQUIRE_FALSE(bindings.match(SDLK_h, KMOD_NONE, false, always)->extraShift);
    REQUIRE(pressed(bindings, SDLK_a, KMOD_LCTRL | KMOD_LSHIFT) == Action::SelectAllCombatUnits);
    // Guard never accepts an extra modifier, and system or unknown chords run nothing.
    REQUIRE_FALSE(pressed(bindings, SDLK_g, KMOD_LCTRL));
    REQUIRE_FALSE(pressed(bindings, SDLK_g, KMOD_LGUI));
    REQUIRE_FALSE(pressed(bindings, SDLK_q, KMOD_LGUI));
    REQUIRE_FALSE(pressed(bindings, SDLK_5));
    REQUIRE_FALSE(pressed(bindings, SDLK_SPACE));
    // Caps Lock and Num Lock are not modifiers of a chord.
    REQUIRE(pressed(bindings, SDLK_g, KMOD_CAPS | KMOD_NUM) == Action::Guard);
}

TEST_CASE("The WASD camera moves unmodified W/A/S/D/Q hotkeys to Shift", "[hotkeys]") {
    const auto bindings = Hotkeys::Bindings::defaults();
    REQUIRE_FALSE(pressed(bindings, SDLK_s, KMOD_NONE, true));
    REQUIRE_FALSE(pressed(bindings, SDLK_a, KMOD_NONE, true));
    REQUIRE(pressed(bindings, SDLK_s, KMOD_LSHIFT, true) == Action::Stop);
    REQUIRE_FALSE(bindings.match(SDLK_s, KMOD_LSHIFT, true, always)->extraShift);
    REQUIRE(pressed(bindings, SDLK_a, KMOD_LSHIFT, true) == Action::AttackMove);
    REQUIRE(pressed(bindings, SDLK_q, KMOD_LSHIFT, true) == Action::Patrol);
    REQUIRE(pressed(bindings, SDLK_g, KMOD_NONE, true) == Action::Guard);
    REQUIRE(pressed(bindings, SDLK_a, KMOD_LCTRL, true) == Action::SelectAllCombatUnits);
}

TEST_CASE("Unavailable actions fall through to the action that accepts Shift", "[hotkeys]") {
    const auto bindings = Hotkeys::Bindings::defaults();
    const auto noCity = [](Action action) {
        return action != Action::CityStatsOverlay && action != Action::CityRoadTool
            && action != Action::CityBudget && action != Action::CityZoneTool;
    };
    REQUIRE(pressed(bindings, SDLK_c, KMOD_LSHIFT) == Action::CityStatsOverlay);
    REQUIRE(pressed(bindings, SDLK_c, KMOD_LSHIFT, false, noCity) == Action::Capture);
    REQUIRE(pressed(bindings, SDLK_r, KMOD_LSHIFT, false, noCity) == Action::Repair);
    REQUIRE_FALSE(pressed(bindings, SDLK_b, KMOD_LSHIFT, false, noCity));
    REQUIRE_FALSE(pressed(bindings, SDLK_z, KMOD_NONE, false, noCity));
}

TEST_CASE("A rebound action answers only to its new key", "[hotkeys]") {
    auto bindings = Hotkeys::Bindings::defaults();
    auto result = Hotkeys::assign(bindings, Action::Guard, Binding{SDLK_k, 0});
    REQUIRE(result.outcome == Hotkeys::AssignOutcome::Assigned);
    REQUIRE_FALSE(result.displaced);
    REQUIRE_FALSE(pressed(bindings, SDLK_g));
    REQUIRE(pressed(bindings, SDLK_k) == Action::Guard);
    REQUIRE(pressed(bindings, SDLK_g, KMOD_LSHIFT) == Action::AreaGuard);

    // Taking a key from another action leaves that action unbound rather than sharing it.
    result = Hotkeys::assign(bindings, Action::Guard, Binding{SDLK_y, 0});
    REQUIRE(result.outcome == Hotkeys::AssignOutcome::Assigned);
    REQUIRE(result.displaced == Action::NextConstructionYard);
    REQUIRE_FALSE(bindings.get(Action::NextConstructionYard).bound());
    REQUIRE(pressed(bindings, SDLK_y) == Action::Guard);
    REQUIRE_FALSE(pressed(bindings, SDLK_k));
    REQUIRE(bindings.conflicts().empty());

    REQUIRE(Hotkeys::assign(bindings, Action::Guard, Binding{SDLK_y, 0}).outcome == Hotkeys::AssignOutcome::Unchanged);
    REQUIRE(Hotkeys::assign(bindings, Action::Guard, Binding{}).outcome == Hotkeys::AssignOutcome::Assigned);
    REQUIRE_FALSE(pressed(bindings, SDLK_y));
}

TEST_CASE("Reserved chords and WASD-only collisions are refused", "[hotkeys]") {
    auto bindings = Hotkeys::Bindings::defaults();
    const auto before = bindings;
    // Stop is S, which becomes Shift+S with the WASD camera.
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
    const auto wasdOnly = Hotkeys::load({{"Guard", "Shift+S"}});
    REQUIRE(wasdOnly.bindings.conflicts().empty());
    REQUIRE(wasdOnly.bindings.get(Action::Guard) == Binding{SDLK_s, Mod::Shift});
    REQUIRE_FALSE(wasdOnly.bindings.get(Action::Stop).bound());
}
