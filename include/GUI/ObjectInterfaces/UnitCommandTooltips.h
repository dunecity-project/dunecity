#ifndef UNITCOMMANDTOOLTIPS_H
#define UNITCOMMANDTOOLTIPS_H

#include <globals.h>
#include <FileClasses/TextManager.h>
#include <FormationPlanner.h>

#include <string>

/**
    Tooltips for the unit command and mode buttons, each naming the effective hotkey from the
    player's bindings (GitHub #87, misc/Hotkeys.h, docs/rts-controls.md). With the WASD camera
    on, an unmodified W/A/S/D binding needs Shift, and the tooltip says so; an unbound action
    says "unbound". Built when the panel is built, like every other tooltip.
*/
namespace UnitCommandTooltips {

/// The effective key of 'action' for display, e.g. "Shift+G", or "unbound".
inline std::string hotkeyText(Hotkeys::Action action) {
    const std::string name = Hotkeys::displayName(settings.hotkeys.effective(action, settings.general.wasdCamera));
    return name.empty() ? _("unbound") : name;
}

/// " (Hotkey: U)" for the structure and sidebar tooltips; empty when the action is unbound.
inline std::string hotkeySuffix(Hotkeys::Action action) {
    const std::string name = Hotkeys::displayName(settings.hotkeys.effective(action, settings.general.wasdCamera));
    return name.empty() ? std::string() : " (" + _("Hotkey") + ": " + name + ")";
}

inline std::string withHotkey(const std::string& label, Hotkeys::Action action, const std::string& help) {
    return label + " (" + hotkeyText(action) + ")" + (help.empty() ? std::string() : ": " + help);
}

inline std::string move() {
    return withHotkey(_("Move"), Hotkeys::Action::Move, _("walk to a position; Shift+click queues waypoints"));
}
inline std::string attackMove() {
    return withHotkey(_("Attack Move"), Hotkeys::Action::AttackMove,
        _("click ground to walk there fighting, an enemy or a spice bloom to attack it, Ctrl+click ground to fire at it; Shift queues"));
}
inline std::string patrol() {
    return withHotkey(_("Patrol"), Hotkeys::Action::Patrol, _("attack-move back and forth from here; Shift+click adds patrol points"));
}
inline std::string guard() {
    return withHotkey(_("Guard"), Hotkeys::Action::Guard, _("Unit will not move from location"));
}
inline std::string areaGuard() {
    return withHotkey(_("Area Guard"), Hotkeys::Action::AreaGuard, _("Unit will engage any unit within guard range"));
}
inline std::string hold() {
    return withHotkey(_("Hold"), Hotkeys::Action::Hold, _("never move, fire only at enemies within weapon range"));
}
inline std::string ambush() {
    return withHotkey(_("Ambush"), Hotkeys::Action::Ambush, _("Unit will not move until enemy unit spotted"));
}
inline std::string hunt() {
    return withHotkey(_("Hunt"), Hotkeys::Action::Hunt, _("Unit will immediately start to engage an enemy unit"));
}
inline std::string retreat() {
    return withHotkey(_("Retreat"), Hotkeys::Action::Retreat, _("Unit will retreat back to base"));
}
/// 'help' differs between the single-unit and the group panel.
inline std::string sabotage(const std::string& help) {
    return withHotkey(_("Sabotage"), Hotkeys::Action::Sabotage, help);
}
inline std::string deploy() {
    return withHotkey(_("Build a new construction yard"), Hotkeys::Action::DeployMCV, std::string());
}
inline std::string destruct() {
    return withHotkey(_("Self-destruct this unit"), Hotkeys::Action::SelfDestruct, std::string());
}
inline std::string heal() {
    return withHotkey(_("Heal an allied unit"), Hotkeys::Action::Heal, std::string());
}
inline std::string movementPaths() {
    return withHotkey(_("Show or hide movement paths for selected units"), Hotkeys::Action::ToggleMovementPaths, std::string());
}
inline std::string stop() {
    return withHotkey(_("Stop"), Hotkeys::Action::Stop, _("unit will not move or attack"));
}
inline std::string carryallDrop() {
    return withHotkey(_("Request Carryall drop"), Hotkeys::Action::CarryallDrop, _("fly the unit to a position"));
}
/// ZONES: Capture on an own zone occupies it; another house's garrison requires Attack first.
inline std::string capture(bool zones = false) {
    return withHotkey(_("Capture"), Hotkeys::Action::Capture, zones
        ? _("take over an enemy building or zone; attack enemy garrisons first; on your own R/C/I zone, garrison it (soldiers fire out from inside); Shift queues")
        : _("take over a building; Shift queues"));
}
inline std::string returnHarvester() {
    return withHotkey(_("Return harvester"), Hotkeys::Action::Hold, _("deliver spice to a refinery; Shift queues"));
}
inline std::string repairUnit() {
    return withHotkey(_("Repair"), Hotkeys::Action::Repair, _("send this unit to a repair yard"));
}
/// Infantry in a city game with Hospital Care (dunecity/HospitalCare.h, repairButtonFor).
inline std::string healAtHospital() {
    return withHotkey(_("Heal"), Hotkeys::Action::Repair, _("send this soldier to a Hospital"));
}
inline std::string needsHospital() {
    return _("Needs a Hospital: soldiers heal only at a Hospital, which a city of 256 residents gets");
}
inline std::string saboteurCannotHeal() {
    return _("A Saboteur cannot be healed");
}

/// The unit panel's formation buttons (docs/formations.md): Off, then the shapes in
/// FormationPlanner::Shape order. A shape also forms the selected units up where they stand.
inline std::string formationChoice(int choice) {
    if(choice <= 0) return withHotkey(_("Formations off"), Hotkeys::Action::ToggleFormationMoves, _("each unit moves on its own"));
    const auto shape = [](const std::string& label, const std::string& help) {
        return withHotkey(label, Hotkeys::Action::NextFormationShape, help + "; " + _("forms the selection up where it stands"));
    };
    switch(FormationPlanner::shapeAt(choice - 1)) {
        case FormationPlanner::Shape::Pincer:
            return shape(_("Pincer formation"), _("two wings with a gap between them, armour in front, MCVs in the gap"));
        case FormationPlanner::Shape::Column:
            return shape(_("Column formation"), _("narrow and deep, for passes and bridges"));
        case FormationPlanner::Shape::Staggered:
            return shape(_("Staggered formation"), _("a loose box, a free tile between units: less splash damage, a wide front"));
        case FormationPlanner::Shape::Echelon:
            return shape(_("Echelon formation"), _("a slanted front, the left flank leading"));
        case FormationPlanner::Shape::Box:
        default:
            return shape(_("Box formation"),
                _("armour in front, ranged units behind, infantry on the flanks; on a move a tight group keeps its shape"));
    }
}

} // namespace UnitCommandTooltips

#endif // UNITCOMMANDTOOLTIPS_H
