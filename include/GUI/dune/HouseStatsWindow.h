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

#ifndef HOUSESTATSWINDOW_H
#define HOUSESTATSWINDOW_H

#include <GUI/Window.h>
#include <GUI/HBox.h>
#include <GUI/VBox.h>
#include <GUI/TextButton.h>
#include <GUI/Label.h>
#include <GUI/Spacer.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

class House;

/**
    Read-only match statistics for one house: a cumulative house summary, the
    per-unit-type combat ledger, and - for a house a QuantBot is running - the
    performance scores and intended army-value shares that bot's last production
    pass actually used.

    Everything displayed is read from live in-memory counters or from an
    observational allocation snapshot. This window issues no command, recomputes
    no learning, and changes no control identity: an observer may page through
    the houses in the match, a participant only ever sees their own.
*/
class HouseStatsWindow : public Window
{
public:
    HouseStatsWindow();
    virtual ~HouseStatsWindow();

    void draw(Point position) override;

    void onClose();
    /// Browse to the next house in the match. Does nothing for a participant.
    void onNextHouse();
    void onPreviousPage();
    void onNextPage();

    static HouseStatsWindow* create() {
        HouseStatsWindow* dlg = new HouseStatsWindow();
        dlg->pAllocated = true;
        return dlg;
    }

private:
    /// One row of the unit table. Four aligned columns, so nothing is clipped
    /// and no column depends on the width of another row's text.
    struct UnitRow {
        HBox box;
        Label name, kills, deaths, damage;
    };
    struct AllocationRow {
        HBox box;
        Label name, performance, goal;
    };

    void buildRows(int rowCount);
    void refresh();
    /// Rebuilds the cached list of unit types with their ledger values. Bounded:
    /// one pass over the unit item range, never over the world.
    void refreshLedger(const House* house);
    void refreshSummary(const House* house);
    void refreshAllocation(const House* house);
    void refreshTable();
    /// Shortens \a text with an ellipsis until it fits \a width at \a fontSize.
    static std::string fitText(const std::string& text, int width, int fontSize);

    struct LedgerEntry {
        Uint32 itemID = 0;
        std::string name;
        uint64_t kills = 0;
        int deaths = 0;
        int64_t damageHp = 0;
    };

    HBox rootHBox;
    VBox mainVBox;
    Label titleLabel;

    HBox houseHBox;
    Label houseLabel;
    TextButton nextHouseButton;

    HBox columnsHBox;
    VBox leftVBox, tableVBox;

    Label summaryHeadingLabel;
    Label unitsBuiltLabel, spiceHarvestedLabel, taxCollectedLabel, populationLabel;

    Label allocationHeadingLabel;
    Label allocationStateLabel;
    HBox allocationHeader;
    Label allocationNameHeader, allocationPerformanceHeader, allocationGoalHeader;
    std::array<AllocationRow, 8> allocationRows;
    Label allocationNoteLabel;

    Label tableHeadingLabel;
    HBox tableHeaderHBox;
    Label headerName, headerKills, headerDeaths, headerDamage;
    std::vector<std::unique_ptr<UnitRow>> rows;
    Label emptyStateLabel;

    HBox pagingHBox;
    TextButton previousPageButton;
    Label pageLabel;
    TextButton nextPageButton;

    HBox buttonsHBox;
    TextButton closeButton;

    int selectedHouseID = 0;
    int page = 0;
    int rowsPerPage = 0;
    std::vector<LedgerEntry> ledger;
    Uint32 lastLedgerCycle = 0;
    bool ledgerSampled = false;
    int lastLedgerHouseID = -1;
};

#endif // HOUSESTATSWINDOW_H
