#ifndef UNITCOMMANDTOOLTIPS_H
#define UNITCOMMANDTOOLTIPS_H

#include <globals.h>
#include <FileClasses/TextManager.h>
#include <FormationPlanner.h>

#include <string>

/**
    Tooltips for the unit command and mode buttons, each naming its hotkey (GitHub #87,
    docs/rts-controls.md). With the WASD camera on, the camera letters A, S, D and Q need
    Shift, and the tooltip says so. Built when the panel is built, like every other tooltip.
*/
namespace UnitCommandTooltips {

inline std::string withHotkey(const std::string& label, const std::string& key, bool wasdNeedsShift,
                              const std::string& help) {
    const std::string shown = (wasdNeedsShift && settings.general.wasdCamera) ? "Shift+" + key : key;
    return label + " (" + shown + ")" + (help.empty() ? std::string() : ": " + help);
}

inline std::string move() {
    return withHotkey(_("Move"), "M", false, _("walk to a position; Shift+click queues waypoints"));
}
inline std::string attackMove() {
    return withHotkey(_("Attack Move"), "A", true,
        _("click ground to walk there fighting, an enemy to attack it, Ctrl+click ground to fire at it; Shift queues"));
}
inline std::string patrol() {
    return withHotkey(_("Patrol"), "Q", true, _("attack-move back and forth from here; Shift+click adds patrol points"));
}
inline std::string hold() {
    return withHotkey(_("Hold"), "H", false, _("never move, fire only at enemies within weapon range"));
}
inline std::string stop() {
    return withHotkey(_("Stop"), "S", true, _("unit will not move or attack"));
}
inline std::string carryallDrop() {
    return withHotkey(_("Request Carryall drop"), "D", true, _("fly the unit to a position"));
}
inline std::string capture() {
    return withHotkey(_("Capture"), "C", false, _("take over a building; Shift queues"));
}
inline std::string returnHarvester() {
    return withHotkey(_("Return harvester"), "H", false, _("deliver spice to a refinery; Shift queues"));
}
inline std::string repairUnit() {
    return withHotkey(_("Repair"), "R", false, _("send this unit to a repair yard"));
}

/// The unit panel's formation buttons (docs/formations.md): Off, then the shapes in
/// FormationPlanner::Shape order.
inline std::string formationChoice(int choice) {
    if(choice <= 0) return withHotkey(_("Formations off"), "Ctrl+F", false, _("each unit moves on its own"));
    const std::string key = "Ctrl+Shift+F";
    switch(FormationPlanner::shapeFromIndex(choice - 1)) {
        case FormationPlanner::Shape::Combined:
            return withHotkey(_("Combined arms"), key, false,
                              _("tanks in front, launchers behind, quads and trikes on the flanks, the fastest ahead as scouts"));
        case FormationPlanner::Shape::Line:
            return withHotkey(_("Line formation"), key, false, _("wide and shallow: the most guns facing the target"));
        case FormationPlanner::Shape::Column:
            return withHotkey(_("Column formation"), key, false, _("narrow and deep, for passes and bridges"));
        case FormationPlanner::Shape::Wedge:
            return withHotkey(_("Wedge formation"), key, false, _("an arrowhead: armour at the point and down both sides"));
        case FormationPlanner::Shape::Vee:
            return withHotkey(_("Vee formation"), key, false, _("a concave: the flanks lead, the centre hangs back"));
        case FormationPlanner::Shape::Echelon:
            return withHotkey(_("Echelon formation"), key, false, _("a slanted front, the left flank leading"));
        case FormationPlanner::Shape::Staggered:
            return withHotkey(_("Staggered formation"), key, false,
                              _("a loose box, a free tile between units: less splash damage, a wide front"));
        case FormationPlanner::Shape::Diamond:
            return withHotkey(_("Diamond formation"), key, false, _("armour round the outside, launchers and MCVs inside"));
        case FormationPlanner::Shape::Pincer:
            return withHotkey(_("Pincer formation"), key, false,
                              _("two wings with a gap between them, armour in front, MCVs in the gap"));
        case FormationPlanner::Shape::Box:
        default:
            return withHotkey(_("Box formation"), key, false,
                              _("armour in front, ranged units behind, infantry on the flanks; a tight group keeps its shape"));
    }
}

} // namespace UnitCommandTooltips

#endif // UNITCOMMANDTOOLTIPS_H
