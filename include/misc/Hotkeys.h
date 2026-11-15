#ifndef MISC_HOTKEYS_H
#define MISC_HOTKEYS_H

#include <SDL_keyboard.h>

#include <array>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
    The configurable in-game command shortcuts (GitHub #87, docs/rts-controls.md "Hotkeys").

    One registry names every action, its stable [Hotkeys] key in the player's config file, its
    English label and its default key. The defaults are the classic shortcuts (A Attack Move,
    S Stop, M Move, ...); commands that had none take the remaining letters.
    Game::handleKeyInput dispatches through match(), the
    unit/structure tooltips and the Options "Hotkeys" tab read the same bindings, so a rebound
    action never answers to its old key and every tooltip shows the effective key.

    Fixed shortcuts (digits for control groups and city overlays, F1-F12, Space, Enter, Escape,
    Tab, arrows, +/-, Print Screen, Ctrl+P, system Cmd/Windows-key chords) are not configurable:
    only letters and a few punctuation/navigation keys can be bound.
*/
namespace Hotkeys {

enum class Action : int {
    Move, AttackMove, Patrol, Stop, Guard, AreaGuard, Hold, Ambush, Hunt, Retreat, Sabotage,
    Capture, CarryallDrop, DeployMCV, SelfDestruct, Heal,
    Repair, Upgrade, PlaceStructure,
    NextConstructionYard, NextFactory, SelectOrnithopters, SelectSameTypeOnScreen, SelectSameTypeOnMap,
    SelectAllCombatUnits,
    ToggleFormationMoves, NextFormationShape, ToggleMovementPaths,
    CityZoneTool, CityRoadTool, CityBudget, CityStatsOverlay,
    ToggleGameTime,
    Count
};
constexpr int actionCount = static_cast<int>(Action::Count);

namespace Mod {
constexpr Uint8 Shift = 1;
constexpr Uint8 Ctrl = 2;
constexpr Uint8 Alt = 4;
}

struct Binding {
    SDL_Keycode key = SDLK_UNKNOWN;
    Uint8 mods = 0;

    bool bound() const { return key != SDLK_UNKNOWN; }
    bool operator==(const Binding& other) const { return key == other.key && mods == other.mods; }
    bool operator!=(const Binding& other) const { return !(*this == other); }
};

struct ActionInfo {
    Action action;
    const char* iniKey;         ///< Stable key in the [Hotkeys] config section
    const char* label;          ///< English label; callers translate it
    Binding defaultBinding;
    /// Shift held on top of the binding still runs the action. Shift then carries its own
    /// meaning (queue the order at the click, queue a harvester return) instead of being part
    /// of the shortcut. Never true for actions whose Shift chord is another default.
    bool shiftIntent;
};

inline const std::array<ActionInfo, actionCount>& actions() {
    static const std::array<ActionInfo, actionCount> table = {{
        // The classic shortcuts (Game::handleKeyInput before the registry) keep their keys;
        // the commands that had none (marked +) take the letters those left free.
        {Action::Move,                   "Move",                    "Move",                          {SDLK_m, 0},                      true},
        {Action::AttackMove,             "Attack Move",             "Attack Move",                   {SDLK_a, 0},                      true},
        {Action::Patrol,                 "Patrol",                  "Patrol",                        {SDLK_q, 0},                      true},
        {Action::Stop,                   "Stop",                    "Stop",                          {SDLK_s, 0},                      true},
        {Action::Guard,                  "Guard",                   "Guard",                         {SDLK_e, 0},                      false},  // +
        {Action::AreaGuard,              "Area Guard",              "Area Guard",                    {SDLK_x, 0},                      false},  // +
        {Action::Hold,                   "Hold",                    "Hold / return harvesters",      {SDLK_h, 0},                      true},
        {Action::Ambush,                 "Ambush",                  "Ambush",                        {SDLK_b, 0},                      false},  // +
        {Action::Hunt,                   "Hunt",                    "Hunt",                          {SDLK_n, 0},                      false},  // +
        {Action::Retreat,                "Retreat",                 "Retreat",                       {SDLK_k, 0},                      false},  // +
        {Action::Sabotage,               "Sabotage",                "Sabotage (Tornie mods)",        {SDLK_j, 0},                      false},  // +
        {Action::Capture,                "Capture",                 "Capture",                       {SDLK_c, 0},                      true},
        {Action::CarryallDrop,           "Carryall Drop",           "Request Carryall drop",         {SDLK_d, 0},                      true},
        {Action::DeployMCV,              "Deploy MCV",              "Deploy MCV",                    {SDLK_y, 0},                      false},  // +
        {Action::SelfDestruct,           "Self-Destruct",           "Self-destruct Devastator",      {SDLK_DELETE, 0},                 false},  // +
        {Action::Heal,                   "Heal",                    "Heal (Tornie mods)",            {SDLK_l, 0},                      false},  // +
        {Action::Repair,                 "Repair",                  "Repair",                        {SDLK_r, 0},                      true},
        {Action::Upgrade,                "Upgrade",                 "Upgrade structure",             {SDLK_u, 0},                      true},
        {Action::PlaceStructure,         "Place Structure",         "Place structure",               {SDLK_p, 0},                      true},
        {Action::NextConstructionYard,   "Next Construction Yard",  "Select next Construction Yard", {SDLK_g, 0},                      true},
        {Action::NextFactory,            "Next Factory",            "Select next factory",           {SDLK_f, 0},                      true},
        {Action::SelectOrnithopters,     "Select Ornithopters",     "Select all ornithopters",       {SDLK_o, 0},                      true},
        {Action::SelectSameTypeOnScreen, "Select Same Type",        "Select same type on screen",    {SDLK_t, 0},                      false},
        {Action::SelectSameTypeOnMap,    "Select Same Type On Map", "Select same type on map",       {SDLK_t, Mod::Ctrl},              false},
        {Action::SelectAllCombatUnits,   "Select Combat Units",     "Select all combat units",       {SDLK_a, Mod::Ctrl},              true},
        {Action::ToggleFormationMoves,   "Formation Moves",         "Formation moves on/off",        {SDLK_f, Mod::Ctrl},              false},
        {Action::NextFormationShape,     "Next Formation Shape",    "Next formation shape",          {SDLK_f, Mod::Ctrl | Mod::Shift}, false},
        {Action::ToggleMovementPaths,    "Movement Paths",          "Show movement paths",           {SDLK_v, 0},                      false},  // +
        {Action::CityZoneTool,           "City Zone Tool",          "City zone tool",                {SDLK_z, 0},                      true},
        {Action::CityRoadTool,           "City Road Tool",          "City road tool",                {SDLK_r, Mod::Shift},             false},
        {Action::CityBudget,             "City Budget",             "City budget",                   {SDLK_b, Mod::Shift},             false},
        {Action::CityStatsOverlay,       "City Statistics",         "City statistics overlay",       {SDLK_c, Mod::Shift},             false},
        {Action::ToggleGameTime,         "Show Game Time",          "Show game time",                {SDLK_t, Mod::Shift},             false},
    }};
    return table;
}

inline const ActionInfo& info(Action action) { return actions()[static_cast<int>(action)]; }

struct NamedKey { SDL_Keycode key; const char* configName; const char* displayName; };

/// Non-letter keys that can be bound. Config names avoid INI comment and quote characters.
inline const std::array<NamedKey, 15>& namedKeys() {
    static const std::array<NamedKey, 15> keys = {{
        {SDLK_LEFTBRACKET, "LeftBracket", "["}, {SDLK_RIGHTBRACKET, "RightBracket", "]"},
        {SDLK_SEMICOLON, "Semicolon", ";"}, {SDLK_QUOTE, "Quote", "'"}, {SDLK_COMMA, "Comma", ","},
        {SDLK_PERIOD, "Period", "."}, {SDLK_SLASH, "Slash", "/"}, {SDLK_BACKSLASH, "Backslash", "\\"},
        {SDLK_BACKQUOTE, "Backquote", "`"}, {SDLK_INSERT, "Insert", "Insert"}, {SDLK_DELETE, "Delete", "Delete"},
        {SDLK_HOME, "Home", "Home"}, {SDLK_END, "End", "End"}, {SDLK_PAGEUP, "PageUp", "Page Up"},
        {SDLK_PAGEDOWN, "PageDown", "Page Down"},
    }};
    return keys;
}

inline bool isBindableKey(SDL_Keycode key) {
    if(key >= SDLK_a && key <= SDLK_z) return true;
    for(const NamedKey& named : namedKeys()) if(named.key == key) return true;
    return false;
}

inline Uint8 modifiersFrom(Uint16 sdlMods) {
    return static_cast<Uint8>(((sdlMods & KMOD_SHIFT) ? Mod::Shift : 0) | ((sdlMods & KMOD_CTRL) ? Mod::Ctrl : 0)
                              | ((sdlMods & KMOD_ALT) ? Mod::Alt : 0));
}

namespace detail {
inline std::string format(const Binding& binding, bool display) {
    const std::string unbound = display ? std::string() : std::string("None");
    if(!binding.bound()) return unbound;
    std::string text;
    if(binding.mods & Mod::Ctrl) text += "Ctrl+";
    if(binding.mods & Mod::Alt) text += "Alt+";
    if(binding.mods & Mod::Shift) text += "Shift+";
    if(binding.key >= SDLK_a && binding.key <= SDLK_z) return text + static_cast<char>(std::toupper(binding.key));
    for(const NamedKey& named : namedKeys())
        if(named.key == binding.key) return text + (display ? named.displayName : named.configName);
    return unbound;
}

inline std::string lower(std::string text) {
    for(char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
}

/// Config form, e.g. "Ctrl+Shift+F", "Semicolon", "None".
inline std::string toConfigString(const Binding& binding) { return detail::format(binding, false); }
/// Player-facing form, e.g. "Shift+G", "Page Up"; empty when unbound.
inline std::string displayName(const Binding& binding) { return detail::format(binding, true); }

/// Parses the config form. "None" (or an empty value) is an explicitly unbound action;
/// anything unreadable or not bindable is nullopt.
inline std::optional<Binding> parse(const std::string& text) {
    std::vector<std::string> parts;
    size_t start = 0;
    while(true) {
        const size_t plus = text.find('+', start);
        const std::string part = text.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        const size_t first = part.find_first_not_of(" \t");
        const size_t last = part.find_last_not_of(" \t");
        parts.push_back(first == std::string::npos ? std::string() : part.substr(first, last - first + 1));
        if(plus == std::string::npos) break;
        start = plus + 1;
    }
    if(parts.size() == 1 && (parts[0].empty() || detail::lower(parts[0]) == "none")) return Binding{};
    Binding binding;
    for(size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string mod = detail::lower(parts[i]);
        Uint8 bit = 0;
        if(mod == "shift") bit = Mod::Shift;
        else if(mod == "ctrl" || mod == "control") bit = Mod::Ctrl;
        else if(mod == "alt") bit = Mod::Alt;
        if(bit == 0 || (binding.mods & bit)) return std::nullopt;
        binding.mods |= bit;
    }
    const std::string key = detail::lower(parts.back());
    if(key.size() == 1 && key[0] >= 'a' && key[0] <= 'z') {
        binding.key = static_cast<SDL_Keycode>(key[0]);
        return binding;
    }
    for(const NamedKey& named : namedKeys()) {
        if(detail::lower(named.configName) == key) {
            binding.key = named.key;
            return binding;
        }
    }
    return std::nullopt;
}

/// Why a key chord cannot be bound, or nullptr.
inline const char* reservedReason(const Binding& binding) {
    if(!binding.bound()) return nullptr;
    if(!isBindableKey(binding.key)) return "This key cannot be used for a command";
    if(binding.key == SDLK_p && (binding.mods & Mod::Ctrl)) return "Ctrl+P takes a screenshot";
    return nullptr;
}

/// Why a pressed key is one of the fixed shortcuts, or nullptr.
inline const char* fixedKeyReason(SDL_Keycode key) {
    if(key >= SDLK_0 && key <= SDLK_9) return "0-9 are control groups (Shift+1-8: city overlays)";
    if(key >= SDLK_F1 && key <= SDLK_F12) return "F1-F12 are fixed (zoom, time skip, sound, FPS)";
    switch(key) {
        case SDLK_SPACE: return "Space pauses the match";
        case SDLK_RETURN: case SDLK_KP_ENTER: return "Enter opens the chat";
        case SDLK_TAB: return "Tab is reserved";
        case SDLK_UP: case SDLK_DOWN: case SDLK_LEFT: case SDLK_RIGHT: return "Arrow keys scroll the map";
        case SDLK_MINUS: case SDLK_KP_MINUS: case SDLK_PLUS: case SDLK_KP_PLUS: case SDLK_EQUALS:
            return "+ and - change the game speed";
        case SDLK_PRINTSCREEN: case SDLK_SYSREQ: return "Print Screen takes a screenshot";
        case SDLK_BACKSPACE: return "Backspace is reserved";
        default: return nullptr;
    }
}

/// The fixed shortcuts, listed on the Options tab (explicit lines: it sits in a 540 px panel).
inline const char* fixedShortcutsSummary() {
    return "Fixed: 0-9 select groups, Ctrl+0-9 set, Alt+0-9 add; Shift+1-8 city overlays;\n"
           "F1-F3 zoom, F4-F6 skip time, F10/F11 sound/music, F12 FPS; Space pause;\n"
           "+/- game speed; Enter chat; Esc menu; arrows scroll; Ctrl+P screenshot.\n"
           "With the WASD camera on, W/A/S/D scroll and hotkeys on them need Shift.";
}

/// The WASD camera is off unless the player switches it on (Options > Controls). While it is
/// on, W/A/S/D scroll the map, so an unmodified binding on one of them (by default Attack
/// Move A, Stop S and Carryall drop D) needs Shift; every other key is unchanged.
inline bool needsShiftWithWasd(SDL_Keycode key) {
    return key == SDLK_w || key == SDLK_a || key == SDLK_s || key == SDLK_d;
}

/// Whether held W/A/S/D scroll the map now: only with the WASD camera on and no modifier held,
/// so Shift+A (Attack Move while the camera is on) and Ctrl+A never scroll.
inline bool wasdScrolls(bool wasdCamera, Uint16 sdlMods) {
    return wasdCamera && !(sdlMods & (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT | KMOD_GUI));
}

inline Binding effective(Binding binding, bool wasdCamera) {
    if(wasdCamera && binding.bound() && binding.mods == 0 && needsShiftWithWasd(binding.key)) binding.mods = Mod::Shift;
    return binding;
}

class Bindings {
public:
    static Bindings defaults() {
        Bindings result;
        for(const ActionInfo& action : actions()) result.set(action.action, action.defaultBinding);
        return result;
    }

    const Binding& get(Action action) const { return bindings[static_cast<int>(action)]; }
    void set(Action action, const Binding& binding) { bindings[static_cast<int>(action)] = binding; }
    Binding effective(Action action, bool wasdCamera) const { return Hotkeys::effective(get(action), wasdCamera); }

    /// The other action that 'binding' for 'action' would collide with in either camera mode.
    std::optional<Action> conflictFor(Action action, const Binding& binding) const {
        if(!binding.bound()) return std::nullopt;
        for(const ActionInfo& other : actions()) {
            if(other.action == action || !get(other.action).bound()) continue;
            for(const bool wasd : {false, true})
                if(Hotkeys::effective(binding, wasd) == effective(other.action, wasd)) return other.action;
        }
        return std::nullopt;
    }

    std::vector<std::pair<Action, Action>> conflicts() const {
        std::vector<std::pair<Action, Action>> result;
        for(int i = 0; i < actionCount; ++i) {
            const auto first = static_cast<Action>(i);
            for(int j = i + 1; j < actionCount; ++j) {
                const auto second = static_cast<Action>(j);
                if(!get(first).bound() || !get(second).bound()) continue;
                for(const bool wasd : {false, true}) {
                    if(effective(first, wasd) == effective(second, wasd)) {
                        result.emplace_back(first, second);
                        break;
                    }
                }
            }
        }
        return result;
    }

    struct Match {
        Action action;
        bool extraShift;    ///< Shift was held on top of a binding without Shift (shiftIntent)
    };

    /// The action a key press runs, or nullopt. An exact chord wins; otherwise Shift held on
    /// top of an unshifted binding reaches an action that accepts Shift as intent. 'available'
    /// lets unavailable actions (city tools without a city) fall through. System chords
    /// (Cmd/Windows key) never run a command.
    std::optional<Match> match(SDL_Keycode key, Uint16 sdlMods, bool wasdCamera,
                               const std::function<bool(Action)>& available) const {
        if((sdlMods & KMOD_GUI) || !isBindableKey(key)) return std::nullopt;
        const Binding pressed{key, modifiersFrom(sdlMods)};
        for(const ActionInfo& action : actions())
            if(effective(action.action, wasdCamera) == pressed && available(action.action)) return Match{action.action, false};
        if(pressed.mods & Mod::Shift) {
            const Binding relaxed{key, static_cast<Uint8>(pressed.mods & ~Mod::Shift)};
            for(const ActionInfo& action : actions())
                if(action.shiftIntent && effective(action.action, wasdCamera) == relaxed && available(action.action))
                    return Match{action.action, true};
        }
        return std::nullopt;
    }

    /// Every action's config value, for the [Hotkeys] section.
    std::map<std::string, std::string> toEntries() const {
        std::map<std::string, std::string> entries;
        for(const ActionInfo& action : actions()) entries[action.iniKey] = toConfigString(get(action.action));
        return entries;
    }

    bool operator==(const Bindings& other) const { return bindings == other.bindings; }
    bool operator!=(const Bindings& other) const { return !(*this == other); }

private:
    std::array<Binding, actionCount> bindings{};
};

struct LoadResult {
    Bindings bindings;
    std::vector<std::string> warnings;
    /// The saved section was an earlier build's untouched default set; the current defaults apply.
    bool replacedRetiredDefaults = false;
};

/// Options > Apply writes every action, so a profile can hold a full copy of the defaults of
/// the build that saved it without the player ever choosing a key. These are those earlier
/// default sets, in registry order of their day: the single-key layout around WASD published
/// in 1.0.870, and the first registry layout (classic keys, G Guard, Y Construction Yard).
struct RetiredDefault { const char* iniKey; const char* value; };
inline const std::array<std::array<RetiredDefault, 25>, 2>& retiredDefaultSets() {
    static const std::array<std::array<RetiredDefault, 25>, 2> sets = {{
        {{{"Move", "V"}, {"Attack Move", "Q"}, {"Patrol", "Z"}, {"Stop", "E"}, {"Guard", "F"}, {"Area Guard", "G"},
          {"Hold", "X"}, {"Capture", "C"}, {"Carryall Drop", "T"}, {"Repair", "R"}, {"Upgrade", "U"},
          {"Place Structure", "B"}, {"Next Construction Yard", "Y"}, {"Next Factory", "H"},
          {"Select Ornithopters", "O"}, {"Select Same Type", "N"}, {"Select Same Type On Map", "J"},
          {"Select Combat Units", "K"}, {"Formation Moves", "M"}, {"Next Formation Shape", "I"},
          {"City Zone Tool", "L"}, {"City Road Tool", "P"}, {"City Budget", "LeftBracket"},
          {"City Statistics", "RightBracket"}, {"Show Game Time", "Backslash"}}},
        {{{"Move", "M"}, {"Attack Move", "A"}, {"Patrol", "Q"}, {"Stop", "S"}, {"Guard", "G"}, {"Area Guard", "Shift+G"},
          {"Hold", "H"}, {"Capture", "C"}, {"Carryall Drop", "D"}, {"Repair", "R"}, {"Upgrade", "U"},
          {"Place Structure", "P"}, {"Next Construction Yard", "Y"}, {"Next Factory", "F"},
          {"Select Ornithopters", "O"}, {"Select Same Type", "T"}, {"Select Same Type On Map", "Ctrl+T"},
          {"Select Combat Units", "Ctrl+A"}, {"Formation Moves", "Ctrl+F"}, {"Next Formation Shape", "Ctrl+Shift+F"},
          {"City Zone Tool", "Z"}, {"City Road Tool", "Shift+R"}, {"City Budget", "Shift+B"},
          {"City Statistics", "Shift+C"}, {"Show Game Time", "Shift+T"}}},
    }};
    return sets;
}

/// True when the saved entries are exactly one retired default set: the same actions, each on
/// that set's key, and nothing else. One changed, cleared or added entry makes it the player's.
inline bool isRetiredDefaultSet(const std::map<std::string, std::string>& entries) {
    for(const auto& set : retiredDefaultSets()) {
        if(entries.size() != set.size()) continue;
        bool same = true;
        for(const RetiredDefault& saved : set) {
            const auto entry = entries.find(saved.iniKey);
            const auto was = parse(saved.value);
            const auto is = entry == entries.end() ? std::nullopt : parse(entry->second);
            if(!was || !is || *was != *is) { same = false; break; }
        }
        if(same) return true;
    }
    return false;
}

/// Reads the [Hotkeys] entries. Missing actions keep their default, unreadable or reserved
/// values fall back to the default, and a collision unbinds the defaulted action (or the later
/// one when both were configured), so the result never has two actions on one chord. A section
/// that is an untouched copy of an earlier build's defaults gets the current defaults instead;
/// any other saved section is the player's own and is kept as it is.
inline LoadResult load(const std::map<std::string, std::string>& entries) {
    LoadResult result{Bindings::defaults(), {}};
    if(isRetiredDefaultSet(entries)) {
        result.replacedRetiredDefaults = true;
        return result;
    }
    std::array<bool, actionCount> configured{};
    for(const ActionInfo& action : actions()) {
        const auto entry = entries.find(action.iniKey);
        if(entry == entries.end()) continue;
        const auto parsed = parse(entry->second);
        if(!parsed) {
            result.warnings.push_back(std::string("Hotkey '") + action.iniKey + "' = '" + entry->second
                                      + "' is not a key; using the default");
            continue;
        }
        if(const char* why = reservedReason(*parsed)) {
            result.warnings.push_back(std::string("Hotkey '") + action.iniKey + "' = '" + entry->second
                                      + "': " + why + "; using the default");
            continue;
        }
        result.bindings.set(action.action, *parsed);
        configured[static_cast<int>(action.action)] = true;
    }
    for(auto conflicts = result.bindings.conflicts(); !conflicts.empty(); conflicts = result.bindings.conflicts()) {
        const auto [first, second] = conflicts.front();
        const bool firstConfigured = configured[static_cast<int>(first)];
        const bool secondConfigured = configured[static_cast<int>(second)];
        const Action drop = (secondConfigured && !firstConfigured) ? first : second;
        const Action keep = drop == first ? second : first;
        result.warnings.push_back(std::string("Hotkey '") + info(drop).iniKey + "' collides with '"
                                  + info(keep).iniKey + "'; it is now unbound");
        result.bindings.set(drop, Binding{});
    }
    return result;
}

enum class AssignOutcome { Assigned, Unchanged, Rejected };

struct AssignResult {
    AssignOutcome outcome;
    std::optional<Action> displaced;    ///< The action that held this exact chord and is now unbound
    std::string reason;                 ///< English reason for a rejection
};

/// Binds 'action' to 'binding' (Binding{} clears it). The same chord held by another action
/// moves here and leaves that action unbound; a chord that only collides through the WASD
/// camera's Shift rule, or a reserved one, is refused.
inline AssignResult assign(Bindings& bindings, Action action, const Binding& binding) {
    if(bindings.get(action) == binding) return {AssignOutcome::Unchanged, std::nullopt, {}};
    if(const char* why = reservedReason(binding)) return {AssignOutcome::Rejected, std::nullopt, why};
    std::optional<Action> displaced;
    if(const auto other = bindings.conflictFor(action, binding)) {
        if(bindings.get(*other) != binding) {
            const bool withWasd = Hotkeys::effective(binding, true) == bindings.effective(*other, true);
            return {AssignOutcome::Rejected, std::nullopt,
                    std::string("Collides with ") + info(*other).label
                        + (withWasd ? " when the WASD camera is on" : " when the WASD camera is off")};
        }
        bindings.set(*other, Binding{});
        displaced = *other;
    }
    bindings.set(action, binding);
    return {AssignOutcome::Assigned, displaced, {}};
}

enum class CaptureKind { Cancel, Waiting, Rejected, Captured };

struct Capture {
    CaptureKind kind;
    Binding binding;
    std::string reason;     ///< English reason for a rejection
};

/// Interprets one key press while the Options tab waits for a new shortcut: Escape cancels,
/// a lone modifier keeps waiting, fixed and system keys are refused with the reason.
inline Capture capture(SDL_Keycode key, Uint16 sdlMods) {
    switch(key) {
        case SDLK_ESCAPE: return {CaptureKind::Cancel, {}, {}};
        case SDLK_LSHIFT: case SDLK_RSHIFT: case SDLK_LCTRL: case SDLK_RCTRL: case SDLK_LALT: case SDLK_RALT:
        case SDLK_LGUI: case SDLK_RGUI: case SDLK_CAPSLOCK: case SDLK_MODE: case SDLK_NUMLOCKCLEAR:
            return {CaptureKind::Waiting, {}, {}};
        default: break;
    }
    if(sdlMods & KMOD_GUI) return {CaptureKind::Rejected, {}, "Cmd and Windows-key shortcuts belong to the system"};
    if(const char* why = fixedKeyReason(key)) return {CaptureKind::Rejected, {}, why};
    const Binding binding{key, modifiersFrom(sdlMods)};
    if(const char* why = reservedReason(binding)) return {CaptureKind::Rejected, {}, why};
    return {CaptureKind::Captured, binding, {}};
}

} // namespace Hotkeys

#endif // MISC_HOTKEYS_H
