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

TEST_CASE("Defaults are the classic shortcuts, with the newer commands on the free letters", "[hotkeys]") {
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
    const auto key = [&](Action action) { return Hotkeys::displayName(defaults.get(action)); };
    // The shortcuts the game had before the registry (Game::handleKeyInput up to 1.0.868).
    REQUIRE(key(Action::AttackMove) == "A");
    REQUIRE(key(Action::Move) == "M");
    REQUIRE(key(Action::Patrol) == "Q");
    REQUIRE(key(Action::Stop) == "S");
    REQUIRE(key(Action::Hold) == "H");
    REQUIRE(key(Action::Capture) == "C");
    REQUIRE(key(Action::CarryallDrop) == "D");
    REQUIRE(key(Action::Repair) == "R");
    REQUIRE(key(Action::Upgrade) == "U");
    REQUIRE(key(Action::PlaceStructure) == "P");
    REQUIRE(key(Action::NextConstructionYard) == "G");
    REQUIRE(key(Action::NextFactory) == "F");
    REQUIRE(key(Action::SelectOrnithopters) == "O");
    REQUIRE(key(Action::SelectSameTypeOnScreen) == "T");
    REQUIRE(key(Action::SelectSameTypeOnMap) == "Ctrl+T");
    REQUIRE(key(Action::SelectAllCombatUnits) == "Ctrl+A");
    REQUIRE(key(Action::ToggleFormationMoves) == "Ctrl+F");
    REQUIRE(key(Action::NextFormationShape) == "Ctrl+Shift+F");
    REQUIRE(key(Action::CityZoneTool) == "Z");
    REQUIRE(key(Action::CityRoadTool) == "Shift+R");
    REQUIRE(key(Action::CityBudget) == "Shift+B");
    REQUIRE(key(Action::CityStatsOverlay) == "Shift+C");
    REQUIRE(key(Action::ToggleGameTime) == "Shift+T");
    // Commands that had no shortcut: one unmodified key each, none of them a classic key.
    const std::set<std::string> classic = {"A", "M", "Q", "S", "H", "C", "D", "R", "U", "P", "G", "F", "O", "T", "Z"};
    const std::vector<std::pair<Action, std::string>> extras = {
        {Action::Guard, "E"}, {Action::AreaGuard, "X"}, {Action::Ambush, "B"}, {Action::Hunt, "N"},
        {Action::Retreat, "K"}, {Action::Sabotage, "J"}, {Action::DeployMCV, "Y"}, {Action::SelfDestruct, "Delete"},
        {Action::Heal, "L"}, {Action::ToggleMovementPaths, "V"}};
    for(const auto& [action, expected] : extras) {
        REQUIRE(key(action) == expected);
        REQUIRE(defaults.get(action).mods == 0);
        REQUIRE(classic.count(expected) == 0);
        REQUIRE_FALSE(Hotkeys::needsShiftWithWasd(defaults.get(action).key));
    }
}

TEST_CASE("No two default commands share a key, with the WASD camera off or on", "[hotkeys]") {
    const auto defaults = Hotkeys::Bindings::defaults();
    for(const bool wasd : {false, true}) {
        std::set<std::string> chords;
        for(const auto& info : Hotkeys::actions()) {
            const auto chord = defaults.effective(info.action, wasd);
            REQUIRE(chords.insert(Hotkeys::toConfigString(chord)).second);
            // Every command answers to the key its tooltip and the Hotkeys tab show.
            REQUIRE(pressed(defaults, chord.key,
                            static_cast<Uint16>(((chord.mods & Mod::Shift) ? KMOD_LSHIFT : 0) | ((chord.mods & Mod::Ctrl) ? KMOD_LCTRL : 0)
                                                | ((chord.mods & Mod::Alt) ? KMOD_LALT : 0)), wasd) == info.action);
        }
    }
}

TEST_CASE("Key presses dispatch to named actions with explicit modifier intent", "[hotkeys]") {
    const auto bindings = Hotkeys::Bindings::defaults();
    REQUIRE(pressed(bindings, SDLK_a) == Action::AttackMove);
    REQUIRE(pressed(bindings, SDLK_s) == Action::Stop);
    REQUIRE(pressed(bindings, SDLK_d) == Action::CarryallDrop);
    REQUIRE(pressed(bindings, SDLK_e) == Action::Guard);
    REQUIRE(pressed(bindings, SDLK_x) == Action::AreaGuard);
    REQUIRE(pressed(bindings, SDLK_g) == Action::NextConstructionYard);
    REQUIRE(pressed(bindings, SDLK_t, KMOD_RCTRL) == Action::SelectSameTypeOnMap);
    REQUIRE(pressed(bindings, SDLK_f, KMOD_LCTRL | KMOD_LSHIFT) == Action::NextFormationShape);
    REQUIRE_FALSE(pressed(bindings, SDLK_w));
    // Shift on top of an order key keeps its queue meaning: it reaches the action, flagged.
    const auto queuedHold = bindings.match(SDLK_h, KMOD_LSHIFT, false, always);
    REQUIRE(queuedHold);
    REQUIRE(queuedHold->action == Action::Hold);
    REQUIRE(queuedHold->extraShift);
    REQUIRE_FALSE(bindings.match(SDLK_h, KMOD_NONE, false, always)->extraShift);
    REQUIRE(pressed(bindings, SDLK_a, KMOD_LCTRL | KMOD_LSHIFT) == Action::SelectAllCombatUnits);
    // Guard, the other modes and self-destruct never accept an extra modifier, and system or
    // unknown chords run nothing.
    REQUIRE_FALSE(pressed(bindings, SDLK_e, KMOD_LCTRL));
    REQUIRE_FALSE(pressed(bindings, SDLK_e, KMOD_LSHIFT));
    for(auto key : {SDLK_x, SDLK_n, SDLK_k, SDLK_y, SDLK_DELETE})
        REQUIRE_FALSE(pressed(bindings, key, KMOD_LSHIFT));
    REQUIRE_FALSE(pressed(bindings, SDLK_e, KMOD_LGUI));
    REQUIRE_FALSE(pressed(bindings, SDLK_q, KMOD_LGUI));
    REQUIRE_FALSE(pressed(bindings, SDLK_5));
    REQUIRE_FALSE(pressed(bindings, SDLK_SPACE));
    // Caps Lock and Num Lock are not modifiers of a chord.
    REQUIRE(pressed(bindings, SDLK_e, KMOD_CAPS | KMOD_NUM) == Action::Guard);
}

TEST_CASE("With the WASD camera on, W/A/S/D scroll and issue no command", "[hotkeys]") {
    const auto bindings = Hotkeys::Bindings::defaults();
    // Off, the default: nothing scrolls and the four keys are ordinary command keys.
    REQUIRE_FALSE(Hotkeys::wasdScrolls(false, KMOD_NONE));
    REQUIRE(pressed(bindings, SDLK_a, KMOD_NONE, false) == Action::AttackMove);
    REQUIRE(pressed(bindings, SDLK_s, KMOD_NONE, false) == Action::Stop);
    REQUIRE(pressed(bindings, SDLK_d, KMOD_NONE, false) == Action::CarryallDrop);
    // On: the plain keys scroll and reach no action; their commands move to Shift.
    REQUIRE(Hotkeys::wasdScrolls(true, KMOD_NONE));
    for(auto key : {SDLK_w, SDLK_a, SDLK_s, SDLK_d}) REQUIRE_FALSE(pressed(bindings, key, KMOD_NONE, true));
    REQUIRE(pressed(bindings, SDLK_a, KMOD_LSHIFT, true) == Action::AttackMove);
    REQUIRE(pressed(bindings, SDLK_s, KMOD_LSHIFT, true) == Action::Stop);
    REQUIRE(pressed(bindings, SDLK_d, KMOD_RSHIFT, true) == Action::CarryallDrop);
    REQUIRE_FALSE(bindings.match(SDLK_s, KMOD_LSHIFT, true, always)->extraShift);
    REQUIRE(Hotkeys::displayName(bindings.effective(Action::AttackMove, true)) == "Shift+A");
    REQUIRE(Hotkeys::displayName(bindings.effective(Action::Stop, true)) == "Shift+S");
    REQUIRE(Hotkeys::displayName(bindings.effective(Action::CarryallDrop, true)) == "Shift+D");
    // A chord never scrolls, so the Shift commands and Ctrl+A do not move the map.
    for(Uint16 mod : {KMOD_LSHIFT, KMOD_RCTRL, KMOD_LALT, KMOD_LGUI}) REQUIRE_FALSE(Hotkeys::wasdScrolls(true, mod));
    REQUIRE(pressed(bindings, SDLK_a, KMOD_LCTRL, true) == Action::SelectAllCombatUnits);
    // Every other command keeps its key: Q is beside W but does not scroll.
    REQUIRE(pressed(bindings, SDLK_q, KMOD_NONE, true) == Action::Patrol);
    REQUIRE(pressed(bindings, SDLK_e, KMOD_NONE, true) == Action::Guard);
    REQUIRE(pressed(bindings, SDLK_x, KMOD_NONE, true) == Action::AreaGuard);
    for(const auto& info : Hotkeys::actions())
        if(!Hotkeys::needsShiftWithWasd(info.defaultBinding.key) || info.defaultBinding.mods != 0)
            REQUIRE(bindings.effective(info.action, true) == info.defaultBinding);
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
    // Shift+B is the city budget; without a city it must not become Ambush (B).
    REQUIRE(pressed(bindings, SDLK_b, KMOD_LSHIFT) == Action::CityBudget);
    REQUIRE_FALSE(pressed(bindings, SDLK_b, KMOD_LSHIFT, false, noCity));
    REQUIRE(pressed(bindings, SDLK_b, KMOD_NONE, false, noCity) == Action::Ambush);
    REQUIRE_FALSE(pressed(bindings, SDLK_z, KMOD_NONE, false, noCity));
}

TEST_CASE("A rebound action answers only to its new key", "[hotkeys]") {
    auto bindings = Hotkeys::Bindings::defaults();
    // Semicolon is free by default (I became Garrison's in the Zone Garrisons follow-up).
    auto result = Hotkeys::assign(bindings, Action::Guard, Binding{SDLK_SEMICOLON, 0});
    REQUIRE(result.outcome == Hotkeys::AssignOutcome::Assigned);
    REQUIRE_FALSE(result.displaced);
    REQUIRE_FALSE(pressed(bindings, SDLK_e));
    REQUIRE(pressed(bindings, SDLK_SEMICOLON) == Action::Guard);
    REQUIRE(pressed(bindings, SDLK_x) == Action::AreaGuard);

    // Taking a key from another action leaves that action unbound rather than sharing it.
    result = Hotkeys::assign(bindings, Action::Guard, Binding{SDLK_g, 0});
    REQUIRE(result.outcome == Hotkeys::AssignOutcome::Assigned);
    REQUIRE(result.displaced == Action::NextConstructionYard);
    REQUIRE_FALSE(bindings.get(Action::NextConstructionYard).bound());
    REQUIRE(pressed(bindings, SDLK_g) == Action::Guard);
    REQUIRE_FALSE(pressed(bindings, SDLK_SEMICOLON));
    REQUIRE(bindings.conflicts().empty());

    REQUIRE(Hotkeys::assign(bindings, Action::Guard, Binding{SDLK_g, 0}).outcome == Hotkeys::AssignOutcome::Unchanged);
    REQUIRE(Hotkeys::assign(bindings, Action::Guard, Binding{}).outcome == Hotkeys::AssignOutcome::Assigned);
    REQUIRE_FALSE(pressed(bindings, SDLK_g));
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
    // W is free: with the camera off it is an ordinary command key, with it on it needs Shift.
    REQUIRE(Hotkeys::assign(bindings, Action::Guard, Binding{SDLK_w, 0}).outcome == Hotkeys::AssignOutcome::Assigned);
    REQUIRE(pressed(bindings, SDLK_w) == Action::Guard);
    REQUIRE_FALSE(pressed(bindings, SDLK_w, KMOD_NONE, true));
    REQUIRE(pressed(bindings, SDLK_w, KMOD_LSHIFT, true) == Action::Guard);
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
    Hotkeys::assign(custom, Action::AttackMove, Binding{SDLK_w, 0});
    const auto entries = custom.toEntries();
    REQUIRE(entries.at("Guard") == "Ctrl+Alt+Semicolon");
    REQUIRE(entries.at("Stop") == "None");
    REQUIRE(entries.at("Attack Move") == "W");
    REQUIRE(entries.size() == static_cast<size_t>(Hotkeys::actionCount));
    const auto reloaded = Hotkeys::load(entries);
    REQUIRE(reloaded.warnings.empty());
    REQUIRE(reloaded.bindings == custom);
    REQUIRE_FALSE(reloaded.replacedRetiredDefaults);

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

TEST_CASE("Mod-only commands fall through without their content", "[hotkeys]") {
    auto bindings = Hotkeys::Bindings::defaults();
    const auto baseGame = [](Action action) { return action != Action::Sabotage && action != Action::Heal; };
    REQUIRE(pressed(bindings, SDLK_j) == Action::Sabotage);
    REQUIRE(pressed(bindings, SDLK_l) == Action::Heal);
    REQUIRE_FALSE(pressed(bindings, SDLK_j, KMOD_NONE, false, baseGame));
    REQUIRE_FALSE(pressed(bindings, SDLK_l, KMOD_NONE, false, baseGame));
    REQUIRE(pressed(bindings, SDLK_b, KMOD_NONE, false, baseGame) == Action::Ambush);
}

TEST_CASE("A customised profile is left exactly as the player saved it", "[hotkeys]") {
    // A profile saved by 1.0.870 (every action written on Apply) with one key of the player's
    // own: Stop moved from E to Semicolon. Nothing in it changes, classic keys included.
    std::map<std::string, std::string> saved;
    for(const auto& entry : Hotkeys::retiredDefaultSets()[0]) saved[entry.iniKey] = entry.value;
    REQUIRE(Hotkeys::isRetiredDefaultSet(saved));
    saved["Stop"] = "Semicolon";
    REQUIRE_FALSE(Hotkeys::isRetiredDefaultSet(saved));
    const auto loaded = Hotkeys::load(saved);
    REQUIRE_FALSE(loaded.replacedRetiredDefaults);
    for(const auto& [iniKey, value] : saved) {
        bool found = false;
        for(const auto& info : Hotkeys::actions()) {
            if(iniKey != info.iniKey) continue;
            found = true;
            REQUIRE(Hotkeys::toConfigString(loaded.bindings.get(info.action)) == value);
        }
        REQUIRE(found);
    }
    REQUIRE(pressed(loaded.bindings, SDLK_q) == Action::AttackMove);
    REQUIRE(pressed(loaded.bindings, SDLK_SEMICOLON) == Action::Stop);
    REQUIRE_FALSE(pressed(loaded.bindings, SDLK_a));
    // Commands newer than the profile keep a default only where the saved keys leave it free:
    // B, N, K, J, Y, L and V are the player's, Self-destruct's Delete is not taken.
    for(const Action newer : {Action::Ambush, Action::Hunt, Action::Retreat, Action::Sabotage, Action::DeployMCV,
                              Action::Heal, Action::ToggleMovementPaths})
        REQUIRE_FALSE(loaded.bindings.get(newer).bound());
    REQUIRE(loaded.bindings.get(Action::SelfDestruct) == Binding{SDLK_DELETE, 0});
    REQUIRE(loaded.warnings.size() == 7);
    REQUIRE(loaded.bindings.conflicts().empty());

    // A single saved key is a customised profile too: it wins over the newer default on it.
    const auto older = Hotkeys::load({{"Guard", "B"}});
    REQUIRE(older.bindings.get(Action::Guard) == Binding{SDLK_b, 0});
    REQUIRE_FALSE(older.bindings.get(Action::Ambush).bound());
    REQUIRE(older.warnings.size() == 1);
    REQUIRE(older.bindings.get(Action::AttackMove) == Binding{SDLK_a, 0});
    REQUIRE(pressed(older.bindings, SDLK_b) == Action::Guard);
}

TEST_CASE("A profile that only holds an earlier version's defaults gets the current defaults", "[hotkeys]") {
    for(const auto& set : Hotkeys::retiredDefaultSets()) {
        std::map<std::string, std::string> saved;
        for(const auto& entry : set) saved[entry.iniKey] = entry.value;
        REQUIRE(saved.size() == set.size());
        const auto loaded = Hotkeys::load(saved);
        REQUIRE(loaded.replacedRetiredDefaults);
        REQUIRE(loaded.warnings.empty());
        REQUIRE(loaded.bindings == Hotkeys::Bindings::defaults());
        // Case and spacing of the saved text do not matter, the keys do.
        saved["Move"] = std::string(" ") + Hotkeys::detail::lower(saved["Move"]) + " ";
        REQUIRE(Hotkeys::load(saved).replacedRetiredDefaults);
        // Cleared, added or missing entries are the player's doing.
        auto cleared = saved; cleared["Guard"] = "None";
        REQUIRE_FALSE(Hotkeys::load(cleared).replacedRetiredDefaults);
        auto extra = saved; extra["Ambush"] = "Semicolon";
        REQUIRE_FALSE(Hotkeys::load(extra).replacedRetiredDefaults);
        auto missing = saved; missing.erase("Upgrade");
        REQUIRE_FALSE(Hotkeys::load(missing).replacedRetiredDefaults);
    }
    // 1.0.870: Q Attack, E Stop, V Move ... becomes A Attack, S Stop, M Move.
    std::map<std::string, std::string> published;
    for(const auto& entry : Hotkeys::retiredDefaultSets()[0]) published[entry.iniKey] = entry.value;
    REQUIRE(published.at("Attack Move") == "Q");
    REQUIRE(pressed(Hotkeys::load(published).bindings, SDLK_a) == Action::AttackMove);
    // The current defaults, saved by Apply, are not a retired set and load as themselves.
    const auto current = Hotkeys::Bindings::defaults().toEntries();
    REQUIRE_FALSE(Hotkeys::isRetiredDefaultSet(current));
    REQUIRE(Hotkeys::load(current).bindings == Hotkeys::Bindings::defaults());
    REQUIRE(Hotkeys::load({}).bindings == Hotkeys::Bindings::defaults());
    REQUIRE_FALSE(Hotkeys::load({}).replacedRetiredDefaults);
}

TEST_CASE("Colliding config entries leave one action on the key", "[hotkeys]") {
    // A configured key wins over another action's default.
    const auto configuredWins = Hotkeys::load({{"Guard", "G"}});
    REQUIRE(configuredWins.bindings.get(Action::Guard) == Binding{SDLK_g, 0});
    REQUIRE_FALSE(configuredWins.bindings.get(Action::NextConstructionYard).bound());
    REQUIRE(configuredWins.warnings.size() == 1);
    REQUIRE(configuredWins.bindings.conflicts().empty());

    // Two configured actions on one key: the earlier one in the registry keeps it.
    const auto bothConfigured = Hotkeys::load({{"Guard", "I"}, {"Stop", "I"}});
    REQUIRE(bothConfigured.bindings.get(Action::Stop) == Binding{SDLK_i, 0});
    REQUIRE_FALSE(bothConfigured.bindings.get(Action::Guard).bound());
    REQUIRE(bothConfigured.bindings.conflicts().empty());

    // A collision that only exists with the WASD camera is still resolved: the configured
    // Shift+S wins over the default Stop (S, Shift+S with the camera on).
    const auto wasdOnly = Hotkeys::load({{"Guard", "Shift+S"}});
    REQUIRE(wasdOnly.bindings.conflicts().empty());
    REQUIRE(wasdOnly.bindings.get(Action::Guard) == Binding{SDLK_s, Mod::Shift});
    REQUIRE_FALSE(wasdOnly.bindings.get(Action::Stop).bound());
    const auto wasdBoth = Hotkeys::load({{"Guard", "Shift+S"}, {"Stop", "S"}});
    REQUIRE(wasdBoth.bindings.conflicts().empty());
    REQUIRE_FALSE(wasdBoth.bindings.get(Action::Guard).bound());
    REQUIRE(wasdBoth.bindings.get(Action::Stop) == Binding{SDLK_s, 0});
}
