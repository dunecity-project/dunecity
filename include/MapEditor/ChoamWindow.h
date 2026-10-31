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

#ifndef CHOAMWINDOW_H
#define CHOAMWINDOW_H

#include <MapEditor/MapData.h>

#include <GUI/Window.h>
#include <GUI/HBox.h>
#include <GUI/VBox.h>
#include <GUI/Label.h>
#include <GUI/DropDownBox.h>
#include <GUI/RadioButton.h>
#include <GUI/TextButton.h>
#include <GUI/Checkbox.h>
#include <GUI/ScrollView.h>
#include <GUI/dune/DigitsTextBox.h>
#include <misc/SDL2pp.h>

#include <array>
#include <vector>

// forward declaration
class MapEditor;


class  ChoamWindow : public Window
{
public:

    ChoamWindow(MapEditor* pMapEditor, HOUSETYPE currentHouse);


    /**
        This static method creates a dynamic choam window.
        The idea behind this method is to simply create a new dialog on the fly and
        add it as a child window of some other window. If the window gets closed it will be freed.
        \param  pMapEditor  pointer to the currently running map editor
        \param  house       the currently selected house; used for button colors, etc.
        \return The new dialog box (will be automatically destroyed when it's closed)
    */
    static ChoamWindow* create(MapEditor* pMapEditor, HOUSETYPE house) {
        ChoamWindow* dlg = new ChoamWindow(pMapEditor, house);
        dlg->pAllocated = true;
        return dlg;
    }


private:
    inline static constexpr std::array allChoamUnits{
        Unit_Carryall, Unit_ChemicalCarryall, Unit_Ornithopter, Unit_Harvester, Unit_MCV,
        Unit_Trike, Unit_RaiderTrike, Unit_Quad, Unit_Tank, Unit_Launcher, Unit_SiegeTank,
        Unit_Devastator, Unit_Deviator, Unit_SonicTank, Unit_RaiderQuad, Unit_RangedTank,
        Unit_SlowTank, Unit_CapacityHarvester, Unit_FastHarvester, Unit_TameSandworm,
        Unit_EliteOrnithopter, Unit_FlameQuad, Unit_GasInfantry, Unit_FlameTrooper,
        Unit_RocketTrike, Unit_SonicTrike, Unit_FlameTank, Unit_EliteLauncher,
        Unit_EliteSiegeTank, Unit_ChemicalSiegeTank, Unit_RebelHarvester};


    void onCancel();
    void onOK();

    void onUnitCheckbox(int itemID);


    HBox    mainHBox;
    VBox    mainVBox;
    VBox    centralVBox;
    ScrollView rowsScroll;
    VBox    rowsVBox;

    Label   Label_Explanation;


    struct ChoamRow {
        HBox            HBox_Unit;
        Checkbox        Checkbox_Unit1;
        DigitsTextBox   TextBox_Unit1;
        Checkbox        Checkbox_Unit2;
        DigitsTextBox   TextBox_Unit2;
    };

    std::array<ChoamRow, (allChoamUnits.size() + 1) / 2> choamRows;
    std::vector<ItemID_enum> choamUnits_;

    HBox        buttonHBox;

    Label       titleLabel;
    TextButton  cancelButton;
    TextButton  okButton;

    MapEditor*  pMapEditor;

    HOUSETYPE   house;
    Uint32      color;
};


#endif // CHOAMWINDOW_H
