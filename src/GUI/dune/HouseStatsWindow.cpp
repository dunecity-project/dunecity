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

#include <GUI/dune/HouseStatsWindow.h>
#include <GUI/dune/ObservedHouseSelector.h>

#include <globals.h>
#include <Game.h>
#include <House.h>
#include <sand.h>
#include <players/QuantBot.h>
#include <dunecity/CitySimulation.h>

#include <FileClasses/TextManager.h>
#include <misc/format.h>
#include <misc/MenuPalette.h>

#include <algorithm>

namespace {

constexpr int kStatsWindowWidth = 620;
constexpr int kStatsWindowHeight = 460;
constexpr int kRowHeight = 18;
constexpr int kBodyFontSize = 12;
constexpr int kAllocationNameWidth = 100;
constexpr int kAllocationPerformanceWidth = 102;
constexpr int kAllocationGoalWidth = 62;
constexpr int kAllocationColumnsWidth =
    kAllocationNameWidth + kAllocationPerformanceWidth + kAllocationGoalWidth;
/// Gap kept between a column's widest content and its neighbour.
constexpr int kColumnPadding = 8;
/// The window's own left and right margins, and the gap between the allocation
/// column and the unit table. Named so the table can work out what is left.
constexpr int kSideMargin = 12;
constexpr int kColumnsGap = 10;
/// Narrowest the type column may become. Longer names keep their ellipsis.
constexpr int kMinNameColumnWidth = 76;
/// The two non-combat city aircraft and the two upgraded variants the player
/// asked to keep out of the unit table.
constexpr int kHiddenUnitTypes[] = {
    Unit_AmbientAirplane, Unit_AmbientHelicopter, Unit_RocketTrike, Unit_EliteLauncher
};

/// True for a type the unit table never lists, whatever its counters say.
bool isHiddenFromUnitTable(int itemID) {
    return std::find(std::begin(kHiddenUnitTypes), std::end(kHiddenUnitTypes), itemID)
           != std::end(kHiddenUnitTypes);
}

Uint32 centeredCoordinate(int available, int extent) {
    return static_cast<Uint32>(std::max(0, (available - extent) / 2));
}

void configureSectionHeading(Label& label, const std::string& text) {
    label.setText(text);
    label.setTextFontSize(14);
    label.setTextColor(MenuTheme::accent, COLOR_TRANSPARENT);
}

void configureValueLabel(Label& label, Alignment_Enum alignment = Alignment_Left,
                         int fontSize = kBodyFontSize) {
    label.setTextColor(COLOR_WHITE);
    label.setTextFontSize(fontSize);
    label.setAlignment(alignment);
}

/// The QuantBot running this house autonomously, if any. A house with no bot -
/// a human player's, or one run by another controller - simply has no snapshot.
const QuantBot* houseQuantBot(const House* house) {
    if(house == nullptr) return nullptr;
    for(const auto& player : house->getPlayerList())
        if(const auto* bot = dynamic_cast<const QuantBot*>(player.get())) return bot;
    return nullptr;
}

} // namespace

HouseStatsWindow::HouseStatsWindow()
 : Window(centeredCoordinate(getRendererWidth(), std::min(kStatsWindowWidth, getRendererWidth() - 8)),
          centeredCoordinate(getRendererHeight(), std::min(kStatsWindowHeight, getRendererHeight() - 8)),
          std::min(kStatsWindowWidth, getRendererWidth() - 8),
          std::min(kStatsWindowHeight, getRendererHeight() - 8)) {

    // Non-modal like the budget window: a read-only readout must never take the
    // map away from the player or the observer.
    setModal(false);

    selectedHouseID = ObservedHouse::initialSelection();

    setWindowWidget(&rootHBox);
    rootHBox.addWidget(HSpacer::create(kSideMargin));
    rootHBox.addWidget(&mainVBox);
    rootHBox.addWidget(HSpacer::create(kSideMargin));

    mainVBox.addWidget(VSpacer::create(8));
    titleLabel.setText(_("Stats"));
    titleLabel.setAlignment(Alignment_HCenter);
    titleLabel.setTextColor(MenuTheme::text, COLOR_TRANSPARENT);
    titleLabel.setTextFontSize(20);
    mainVBox.addWidget(&titleLabel, 26);

    configureValueLabel(houseLabel, Alignment_Left, 14);
    houseHBox.addWidget(&houseLabel);
    nextHouseButton.setText(_("Next house"));
    nextHouseButton.setTooltipText(_("Show the next house in this match (observers only)."));
    nextHouseButton.setOnClick(std::bind(&HouseStatsWindow::onNextHouse, this));
    // A participant has exactly one house to look at, so the control is not
    // offered at all rather than offered and ignored.
    nextHouseButton.setVisible(ObservedHouse::mayBrowse());
    nextHouseButton.setEnabled(ObservedHouse::mayBrowse());
    houseHBox.addWidget(&nextHouseButton, 120);
    mainVBox.addWidget(&houseHBox, 24);
    mainVBox.addWidget(VSpacer::create(4));

    configureSectionHeading(summaryHeadingLabel, _("House summary"));
    leftVBox.addWidget(&summaryHeadingLabel, 18);
    for(auto* label : {&unitsBuiltLabel, &spiceHarvestedLabel, &taxCollectedLabel, &populationLabel}) {
        configureValueLabel(*label);
        leftVBox.addWidget(label, kRowHeight);
    }
    leftVBox.addWidget(VSpacer::create(6));

    configureSectionHeading(allocationHeadingLabel, _("Army allocation"));
    leftVBox.addWidget(&allocationHeadingLabel, 18);
    configureValueLabel(allocationStateLabel);
    leftVBox.addWidget(&allocationStateLabel, kRowHeight);
    configureValueLabel(allocationNameHeader, Alignment_Left, 11);
    configureValueLabel(allocationPerformanceHeader, Alignment_Right, 11);
    configureValueLabel(allocationGoalHeader, Alignment_Right, 11);
    allocationNameHeader.setText(_("Type"));
    allocationPerformanceHeader.setText(_("Performance"));
    allocationGoalHeader.setText(_("Goal %"));
    for(auto* label : {&allocationNameHeader, &allocationPerformanceHeader, &allocationGoalHeader})
        label->setTextColor(MenuTheme::accent, COLOR_TRANSPARENT);
    allocationHeader.addWidget(&allocationNameHeader, kAllocationNameWidth);
    allocationHeader.addWidget(&allocationPerformanceHeader, kAllocationPerformanceWidth);
    allocationHeader.addWidget(&allocationGoalHeader, kAllocationGoalWidth);
    leftVBox.addWidget(&allocationHeader, kRowHeight);
    for(auto& row : allocationRows) {
        configureValueLabel(row.name, Alignment_Left, 11);
        configureValueLabel(row.performance, Alignment_Right, 11);
        configureValueLabel(row.goal, Alignment_Right, 11);
        row.box.addWidget(&row.name, kAllocationNameWidth);
        row.box.addWidget(&row.performance, kAllocationPerformanceWidth);
        row.box.addWidget(&row.goal, kAllocationGoalWidth);
        leftVBox.addWidget(&row.box, kRowHeight);
    }
    allocationNoteLabel.setTextColor(COLOR_LIGHTGREY);
    allocationNoteLabel.setTextFontSize(10);
    allocationNoteLabel.setAlignment(Alignment_Left);
    leftVBox.addWidget(&allocationNoteLabel, 60);
    leftVBox.addWidget(Spacer::create());

    configureSectionHeading(tableHeadingLabel, _("Units"));
    tableVBox.addWidget(&tableHeadingLabel, 18);
    measureTableColumns();
    configureValueLabel(headerName);
    configureValueLabel(headerActive, Alignment_Right);
    configureValueLabel(headerKills, Alignment_Right);
    configureValueLabel(headerDeaths, Alignment_Right);
    configureValueLabel(headerDamage, Alignment_Right);
    headerName.setText(_("Type"));
    headerActive.setText(_("Active"));
    headerKills.setText(_("Kills"));
    headerDeaths.setText(_("Deaths"));
    headerDamage.setText(_("Damage"));
    for(auto* header : {&headerName, &headerActive, &headerKills, &headerDeaths, &headerDamage})
        header->setTextColor(MenuTheme::accent, COLOR_TRANSPARENT);
    tableHeaderHBox.addWidget(&headerName, nameColumnWidth);
    tableHeaderHBox.addWidget(&headerActive, activeColumnWidth);
    tableHeaderHBox.addWidget(&headerKills, killsColumnWidth);
    tableHeaderHBox.addWidget(&headerDeaths, deathsColumnWidth);
    tableHeaderHBox.addWidget(&headerDamage, damageColumnWidth);
    tableVBox.addWidget(&tableHeaderHBox, kRowHeight);

    // One page of rows, sized to the space this window actually has. A longer
    // list is paged rather than clipped, so every row stays fully readable at
    // the smallest supported display.
    const int tableHeight = std::max(kRowHeight * 3, getSize().y - 190);
    buildRows(std::max(3, tableHeight / kRowHeight));

    configureValueLabel(emptyStateLabel);
    emptyStateLabel.setText("");
    tableVBox.addWidget(&emptyStateLabel, kRowHeight);
    tableVBox.addWidget(Spacer::create());

    previousPageButton.setText("<");
    previousPageButton.setOnClick(std::bind(&HouseStatsWindow::onPreviousPage, this));
    nextPageButton.setText(">");
    nextPageButton.setOnClick(std::bind(&HouseStatsWindow::onNextPage, this));
    configureValueLabel(pageLabel, Alignment_HCenter);
    pagingHBox.addWidget(&previousPageButton, 32);
    pagingHBox.addWidget(&pageLabel);
    pagingHBox.addWidget(&nextPageButton, 32);
    tableVBox.addWidget(&pagingHBox, 24);

    columnsHBox.addWidget(&leftVBox);
    columnsHBox.addWidget(HSpacer::create(kColumnsGap));
    columnsHBox.addWidget(&tableVBox);
    mainVBox.addWidget(&columnsHBox);

    closeButton.setText(_("Close"));
    closeButton.setOnClick(std::bind(&HouseStatsWindow::onClose, this));
    buttonsHBox.addWidget(Spacer::create());
    buttonsHBox.addWidget(&closeButton, 120);
    buttonsHBox.addWidget(Spacer::create());
    mainVBox.addWidget(&buttonsHBox, 30);
    mainVBox.addWidget(VSpacer::create(8));

    refresh();
}

HouseStatsWindow::~HouseStatsWindow() = default;

void HouseStatsWindow::measureTableColumns() {
    auto& style = GUIStyle::getInstance();
    // A count column has to hold its own header and the widest number it can
    // carry, measured in the font actually in use, so that no translated header
    // overflows and no number ever has to be shortened.
    auto countColumn = [&](const std::string& header, const std::string& widestNumber) {
        return static_cast<int>(std::max(style.getTextWidth(header, kBodyFontSize),
                                         style.getTextWidth(widestNumber, kBodyFontSize)))
               + kColumnPadding;
    };
    activeColumnWidth = countColumn(_("Active"), "00000");
    killsColumnWidth = countColumn(_("Kills"), "000000");
    deathsColumnWidth = countColumn(_("Deaths"), "000000");
    damageColumnWidth = countColumn(_("Damage"), "00000000");

    // What the counts do not need is the type column's. The allocation column
    // on the left keeps its own width plus one padding of slack, so a fifth
    // count column can never squeeze it.
    const int available = getSize().x - 2 * kSideMargin - kColumnsGap
                          - kAllocationColumnsWidth - kColumnPadding;
    nameColumnWidth = std::max(kMinNameColumnWidth,
                               available - activeColumnWidth - killsColumnWidth
                                   - deathsColumnWidth - damageColumnWidth);
}

void HouseStatsWindow::buildRows(int rowCount) {
    rowsPerPage = rowCount;
    rows.clear();
    rows.reserve(static_cast<size_t>(rowCount));
    for(int i = 0; i < rowCount; ++i) {
        auto row = std::make_unique<UnitRow>();
        configureValueLabel(row->name);
        for(auto* count : {&row->active, &row->kills, &row->deaths, &row->damage})
            configureValueLabel(*count, Alignment_Right);
        row->box.addWidget(&row->name, nameColumnWidth);
        row->box.addWidget(&row->active, activeColumnWidth);
        row->box.addWidget(&row->kills, killsColumnWidth);
        row->box.addWidget(&row->deaths, deathsColumnWidth);
        row->box.addWidget(&row->damage, damageColumnWidth);
        tableVBox.addWidget(&row->box, kRowHeight);
        rows.push_back(std::move(row));
    }
}

void HouseStatsWindow::onClose() {
    Window* pParentWindow = dynamic_cast<Window*>(getParent());
    if(pParentWindow != nullptr) {
        pParentWindow->closeChildWindow();
    }
}

void HouseStatsWindow::onNextHouse() {
    const int next = ObservedHouse::next(selectedHouseID);
    if(next == selectedHouseID) return;      // A participant stays on their own house.
    selectedHouseID = next;
    page = 0;
    ledgerSampled = false;
    refresh();
}

void HouseStatsWindow::onPreviousPage() {
    if(page > 0) {
        --page;
        refreshTable();
    }
}

void HouseStatsWindow::onNextPage() {
    const int pages = std::max(1, (static_cast<int>(ledger.size()) + rowsPerPage - 1) / rowsPerPage);
    if(page + 1 < pages) {
        ++page;
        refreshTable();
    }
}

std::string HouseStatsWindow::fitText(const std::string& text, int width, int fontSize) {
    if(GUIStyle::getInstance().getTextWidth(text, fontSize) <= width) return text;
    std::string shortened = text;
    while(!shortened.empty()
          && GUIStyle::getInstance().getTextWidth(shortened + "...", fontSize) > width) {
        shortened.pop_back();
    }
    return shortened + "...";
}

void HouseStatsWindow::draw(Point position) {
    refresh();
    Window::draw(position);
}

void HouseStatsWindow::refresh() {
    const House* house = ObservedHouse::resolve(selectedHouseID);
    if(house != nullptr) selectedHouseID = house->getHouseID();
    houseLabel.setText(fmt::sprintf("%s: %s", _("House"), ObservedHouse::name(house)));
    refreshSummary(house);
    refreshAllocation(house);

    // The ledger is an array walk over the unit types, not over the world, and
    // it is resampled on a low cadence rather than every frame.
    const Uint32 now = currentGame != nullptr ? currentGame->getGameCycleCount() : 0;
    if(!ledgerSampled || lastLedgerHouseID != selectedHouseID
       || now - lastLedgerCycle >= MILLI2CYCLES(1000)) {
        refreshLedger(house);
        lastLedgerCycle = now;
        lastLedgerHouseID = selectedHouseID;
        ledgerSampled = true;
    }
    refreshTable();
}

void HouseStatsWindow::refreshSummary(const House* house) {
    if(house == nullptr) {
        for(auto* label : {&unitsBuiltLabel, &spiceHarvestedLabel, &taxCollectedLabel, &populationLabel})
            label->setText("-");
        return;
    }

    // The refinery counter stores gross income after this house's fixed
    // multiplier. Undo it to show spice delivered rather than multiplied cash.
    unitsBuiltLabel.setText(fmt::sprintf("%s: %d", _("Units produced"), house->getNumBuiltUnits()));
    spiceHarvestedLabel.setText(fmt::sprintf("%s: %d", _("Spice harvested"),
        (house->getHarvestedSpice() / house->getSpiceIncomeMultiplier()).lround()));

    auto* citySim = currentGame != nullptr ? currentGame->getCitySimulation() : nullptr;
    const bool cityEconomy = citySim != nullptr && citySim->isInitialized();
    if(cityEconomy) {
        taxCollectedLabel.setText(fmt::sprintf("%s: %d", _("Tax collected"),
                                               house->getCityTaxReceipts().lround()));
        // This house's own live city population, never the map's and never the
        // viewer's: the per-house city state, at the same display scale the
        // sidebar pill uses.
        const auto& state = citySim->getHouseState(house->getHouseID());
        populationLabel.setText(fmt::sprintf("%s: %d", _("Population"),
            state.getTotalPop() * DuneCity::CitySimulation::kPopDisplayMultiplier));
    } else {
        // No city economy in this match: there is no tax and no population to
        // report, and inventing one would be worse than saying so.
        taxCollectedLabel.setText(fmt::sprintf("%s: %s", _("Tax collected"), _("N/A")));
        populationLabel.setText(fmt::sprintf("%s: %s", _("Population"), _("N/A")));
    }
}

void HouseStatsWindow::refreshAllocation(const House* house) {
    auto clearRows = [&]() {
        for(auto& row : allocationRows) {
            row.name.setText(""); row.performance.setText(""); row.goal.setText("");
        }
        for(auto* label : {&allocationNameHeader, &allocationPerformanceHeader, &allocationGoalHeader})
            label->setVisible(false);
    };
    const QuantBot* bot = houseQuantBot(house);
    if(bot == nullptr) {
        allocationStateLabel.setText(_("No QuantBot on this house"));
        clearRows();
        allocationNoteLabel.setText("");
        return;
    }

    const auto& snapshot = bot->getAllocationSnapshot();
    if(!snapshot.ready) {
        // A normal save does not carry this observational snapshot, so after a
        // load it is honestly reported as pending until the next build pass.
        allocationStateLabel.setText(bot->getDifficultyName() + " - "
                                     + _("awaiting production pass"));
        clearRows();
        allocationNoteLabel.setText(_("Performance and Goal appear at the next\nproduction pass of this house."));
        return;
    }

    allocationStateLabel.setText(bot->getDifficultyName()
        + fmt::sprintf(" | %s %d | ", _("cycle"), static_cast<int>(snapshot.cycle))
        + (snapshot.learning ? _("measured") : _("opening")));

    for(auto* label : {&allocationNameHeader, &allocationPerformanceHeader, &allocationGoalHeader})
        label->setVisible(true);
    for(size_t slot = 0; slot < snapshot.slots.size(); ++slot) {
        const auto& entry = snapshot.slots[slot];
        std::string name = slot == QuantBot::AllocationSnapshot::kSpecialGroupSlot
            ? std::string(_("Special heavies")) : getItemNameByID(entry.itemID);
        auto& row = allocationRows[slot];
        row.name.setText(fitText(name, kAllocationNameWidth - 4, 11));
        if(!entry.allocated) {
            // Not producible in that pass, so this house has no intended share
            // for it. Showing a goal here would be inventing one.
            row.performance.setText("-");
            row.goal.setText("-");
        } else {
            row.performance.setText(fmt::sprintf("x%.1f", static_cast<double>(entry.score) / 1000000.0));
            row.goal.setText(fmt::sprintf("%d.%02d", entry.targetBps / 100, entry.targetBps % 100));
        }
    }

    // Short notes, so the meaning is stated without a paragraph of text in a
    // narrow column. Kept free of per-cent signs: the numbers above carry those.
    std::string note = std::string(_("Performance: production score as a multiplier."))
        + "\n" + _("Goal %: intended share of total army value.")
        + fmt::sprintf("\n%s %d.%02d%%", _("Infantry cap (separate):"),
                       snapshot.infantryQuotaBps / 100, snapshot.infantryQuotaBps % 100);
    if(snapshot.antiAirFloorBps > 0)
        note += fmt::sprintf("\n%s %d.%02d%%", _("Anti-air floor:"),
                             snapshot.antiAirFloorBps / 100, snapshot.antiAirFloorBps % 100);
    else note += "\n" + std::string(_("- = not allocated in this pass."));
    allocationNoteLabel.setText(note);
}

void HouseStatsWindow::refreshLedger(const House* house) {
    ledger.clear();
    if(house == nullptr || currentGame == nullptr) return;
    const int houseID = house->getHouseID();
    for(int item = 0; item < Num_ItemID; ++item) {
        if(!isUnit(item)) continue;
        // Excluded before any counter is read: these types stay out of the table
        // even when they are enabled, owned, or carry ledger history.
        if(isHiddenFromUnitTable(item)) continue;
        const auto& reward = house->getCombatReward(item);
        const int deaths = house->getNumLostItems(item);
        // Registered units of this type from the existing house census.
        // Constant time per type and no world scan, so the
        // column costs the same whatever the army size.
        const int active = house->getNumItems(item);
        // Kills credited to this type as the attacker, the real hit points it
        // removed, and the units of this type this house lost. getNumKilledItems()
        // counts victims of that type and is deliberately not used here.
        const int64_t damageHp = reward.hpRemovedMilli / 1000;
        // A type with registered units stays listed even where production of
        // it is disabled.
        const bool anyData = active > 0 || reward.kills > 0 || deaths > 0 || damageHp > 0;
        if(!anyData && !currentGame->objectData.data[item][houseID].enabled) continue;
        ledger.push_back({static_cast<Uint32>(item), getItemNameByID(item),
                          active, reward.kills, deaths, damageHp});
    }
}

void HouseStatsWindow::refreshTable() {
    const int pages = std::max(1, (static_cast<int>(ledger.size()) + rowsPerPage - 1) / rowsPerPage);
    page = std::clamp(page, 0, pages - 1);
    const size_t first = static_cast<size_t>(page) * static_cast<size_t>(rowsPerPage);
    for(size_t i = 0; i < rows.size(); ++i) {
        const size_t index = first + i;
        auto& row = *rows[i];
        if(index >= ledger.size()) {
            for(auto* label : {&row.name, &row.active, &row.kills, &row.deaths, &row.damage})
                label->setText("");
            continue;
        }
        const auto& entry = ledger[index];
        row.name.setText(fitText(entry.name, nameColumnWidth - 4, kBodyFontSize));
        row.active.setText(std::to_string(entry.active));
        row.kills.setText(std::to_string(entry.kills));
        row.deaths.setText(std::to_string(entry.deaths));
        row.damage.setText(std::to_string(entry.damageHp));
    }
    emptyStateLabel.setText(ledger.empty() ? _("No unit records yet") : "");
    pageLabel.setText(fmt::sprintf("%d / %d", page + 1, pages));
    previousPageButton.setEnabled(page > 0);
    nextPageButton.setEnabled(page + 1 < pages);
    previousPageButton.setVisible(pages > 1);
    nextPageButton.setVisible(pages > 1);
}
