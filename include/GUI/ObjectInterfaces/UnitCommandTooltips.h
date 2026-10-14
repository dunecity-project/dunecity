#ifndef UNITCOMMANDTOOLTIPS_H
#define UNITCOMMANDTOOLTIPS_H

#include <globals.h>
#include <FileClasses/TextManager.h>

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
    switch(choice) {
        case 0:  return withHotkey(_("Formations off"), "Ctrl+F", false, _("each unit moves on its own"));
        case 1:  return withHotkey(_("Box formation"), "Ctrl+Shift+F", false,
                     _("armour in front, ranged units behind, infantry on the flanks; a tight group keeps its shape"));
        case 2:  return withHotkey(_("Combined arms"), "Ctrl+Shift+F", false,
                     _("tanks in front, launchers behind, quads and trikes on the flanks, the fastest ahead as scouts"));
        case 3:  return withHotkey(_("Line formation"), "Ctrl+Shift+F", false, _("wide and shallow: the most guns facing the target"));
        default: return withHotkey(_("Column formation"), "Ctrl+Shift+F", false, _("narrow and deep, for passes and bridges"));
    }
}

} // namespace UnitCommandTooltips

#endif // UNITCOMMANDTOOLTIPS_H
