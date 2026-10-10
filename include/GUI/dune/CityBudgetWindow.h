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

#ifndef CITYBUDGETWINDOW_H
#define CITYBUDGETWINDOW_H

#include <GUI/Window.h>
#include <GUI/HBox.h>
#include <GUI/VBox.h>
#include <GUI/TextButton.h>
#include <GUI/Label.h>
#include <GUI/Spacer.h>

/// City budget window. Player can adjust the tax rate and police-funding
/// share, review the resulting forecast, and confirm both changes together.
class CityBudgetWindow : public Window
{
public:
    CityBudgetWindow();
    virtual ~CityBudgetWindow();

    void draw(Point position) override;

    void onCancel();
    void onTaxIncrease();
    void onTaxDecrease();
    void onPoliceIncrease();
    void onPoliceDecrease();
    void onConfirm();
    /// Browse to the next house in the match. Observers only; a participant
    /// stays on their own house whatever invokes this.
    void onNextHouse();
    /// True while this window is a read-only view of a house that is not ours.
    bool isReadOnly() const;

    static CityBudgetWindow* create() {
        CityBudgetWindow* dlg = new CityBudgetWindow();
        dlg->pAllocated = true;
        return dlg;
    }

private:
    void updateDisplay();
    void updateAllocationLabels();

    HBox rootHBox;
    HBox allocationPairHBox, detailsHBox;
    VBox forecastVBox, statusVBox;
    VBox mainVBox;
    Label titleLabel;

    HBox houseHBox;
    Label houseLabel;
    TextButton nextHouseButton;

    HBox summaryHBox;
    Label yearLabel;
    Label treasuryLabel;

    Label allocationsHeadingLabel;
    HBox taxHBox;
    Label taxLabel;
    TextButton taxMinus;
    Label taxValueLabel;
    TextButton taxPlus;

    HBox policeHBox;
    Label policeLabel;
    TextButton policeMinus;
    Label policeValueLabel;
    TextButton policePlus;

    Label forecastHeadingLabel;
    Label incomeLabel;
    Label policeCostLabel;
    Label policeStationCostLabel;
    Label rocketTurretCostLabel;
    Label gunTurretCostLabel;
    Label roadCostLabel;
    Label netLabel;
    Label perSecondLabel;

    Label cityStatusHeadingLabel;
    Label totalPopLabel;
    Label unemploymentLabel;
    Label resPopLabel;
    Label comPopLabel;
    Label indPopLabel;
    Label servicesLabel;
    Label environmentLabel;
    Label crimeTrafficLabel;

    HBox buttonsHBox;
    TextButton confirmButton;
    TextButton cancelButton;

    int pendingPolicePercent = 100;
    int pendingTaxRate = 7;
    /// Which house this window is showing. Window state only: it never changes
    /// pLocalHouse, pLocalPlayer or any control identity.
    int selectedHouseID = 0;
};

#endif // CITYBUDGETWINDOW_H
