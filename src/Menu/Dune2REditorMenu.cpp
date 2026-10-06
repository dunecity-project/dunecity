/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <Menu/Dune2REditorMenu.h>
#include <Menu/Dune2RAssetMenu.h>

#include <Colors.h>
#include <FileClasses/TextManager.h>
#include <globals.h>
#include <mod/ModManager.h>
#include <mod/Workshop.h>
#include <GUI/MsgBox.h>
#include <sand.h>

#include <algorithm>
#include <array>
#include <functional>

namespace {

constexpr std::array<const char*, static_cast<size_t>(GFXManager::EnhancedUnitState::Count)> kStateLabels = {
    "Idle", "Movement", "Combat", "Smoking", "Damaged",
    "Exploded", "Aftermath", "Dissipation", "Return to Idle"
};

constexpr std::array<const char*, 8> kDirectionLabels = {
    "East", "Northeast", "North", "Northwest",
    "West", "Southwest", "South", "Southeast"
};

const char* renderModeLabel(GFXManager::EnhancedRenderMode mode) {
    switch(mode) {
        case GFXManager::EnhancedRenderMode::Layered:       return "Layered";
        case GFXManager::EnhancedRenderMode::Random:        return "Random";
        case GFXManager::EnhancedRenderMode::FullAnimation: return "Full Animation";
    }
    return "Full Animation";
}

} // namespace

class Dune2RPreviewWidget final : public Widget {
public:
    Dune2RPreviewWidget() {
        enableResizing(true, true);
    }

    void setSelection(int newItemID, int newHouseID,
                      GFXManager::EnhancedUnitState newState,
                      int newDirection) {
        itemID = newItemID;
        houseID = newHouseID;
        state = newState;
        direction = newDirection;
    }

    void clearSelection() {
        itemID = -1;
        direction = -1;
    }

    void draw(Point position) override {
        if(!isVisible()) {
            return;
        }

        SDL_Rect panel{position.x, position.y, getSize().x, getSize().y};
        Uint8 oldR = 0, oldG = 0, oldB = 0, oldA = 0;
        SDL_GetRenderDrawColor(renderer, &oldR, &oldG, &oldB, &oldA);
        SDL_BlendMode oldBlend = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(renderer, &oldBlend);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 8, 8, 8, 210);
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 218, 166, 48, 255);
        SDL_RenderDrawRect(renderer, &panel);

        if(itemID >= 0 && direction >= 0) {
            SDL_Rect oldClip{};
            const SDL_bool hadClip = SDL_RenderIsClipEnabled(renderer);
            SDL_RenderGetClipRect(renderer, &oldClip);
            SDL_Rect clip = panel;
            if(hadClip == SDL_TRUE) {
                SDL_Rect intersection{};
                if(SDL_IntersectRect(&oldClip, &panel, &intersection) == SDL_TRUE) {
                    clip = intersection;
                }
            }
            SDL_RenderSetClipRect(renderer, &clip);

            const int previewHouse = houseID >= 0 ? houseID : HOUSE_HARKONNEN;
            const Uint32 duration = pGFXManager->getEnhancedUnitAnimationDuration(
                itemID, previewHouse, state, direction);
            const Uint32 elapsed = duration > 0 ? SDL_GetTicks() % duration : SDL_GetTicks();
            pGFXManager->drawEnhancedUnit(itemID, previewHouse, 2, state, direction,
                                          elapsed,
                                          panel.x + panel.w / 2,
                                          panel.y + panel.h / 2 + 10);

            SDL_RenderSetClipRect(renderer, hadClip == SDL_TRUE ? &oldClip : nullptr);
        }

        SDL_SetRenderDrawBlendMode(renderer, oldBlend);
        SDL_SetRenderDrawColor(renderer, oldR, oldG, oldB, oldA);
    }

private:
    int itemID = -1;
    int houseID = -1;
    GFXManager::EnhancedUnitState state = GFXManager::EnhancedUnitState::Idle;
    int direction = -1;
};

Dune2REditorMenu::Dune2REditorMenu() {
    SDL_Texture* background = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(background);
    resize(getTextureSize(background));
    setWindowWidget(&windowWidget);

    const int panelWidth = std::min(620, getRendererWidth() - 20);
    const int panelHeight = std::min(440, getRendererHeight() - 20);
    const int originX = (getRendererWidth() - panelWidth) / 2;
    const int originY = (getRendererHeight() - panelHeight) / 2;

    titleLabel.setText(_("Dune2R Asset Editor"));
    titleLabel.setTextFontSize(22);
    titleLabel.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&titleLabel, Point(originX + 10, originY + 10),
                           Point(panelWidth - 20, 30));

    introLabel.setText(ModManager::instance().getModInfo(ModManager::instance().getActiveModName()).displayName
        + " - " + _("Choose how packaged sprite animations are rendered."));
    introLabel.setTextFontSize(13);
    introLabel.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&introLabel, Point(originX + 20, originY + 42),
                           Point(panelWidth - 40, 22));

    const int labelX = originX + 28;
    const int fieldX = originX + 132;
    const int fieldWidth = 205;
    const std::array<int, 4> rowY = {
        originY + 82, originY + 126, originY + 170, originY + 214
    };

    unitLabel.setText(_("Unit"));
    stateLabel.setText(_("Motion"));
    directionLabel.setText(_("Facing"));
    modeLabel.setText(_("Renderer"));
    windowWidget.addWidget(&unitLabel, Point(labelX, rowY[0]), Point(95, 25));
    windowWidget.addWidget(&stateLabel, Point(labelX, rowY[1]), Point(95, 25));
    windowWidget.addWidget(&directionLabel, Point(labelX, rowY[2]), Point(95, 25));
    windowWidget.addWidget(&modeLabel, Point(labelX, rowY[3]), Point(95, 25));

    unitDropDown.setOnSelectionChange(
        std::bind(&Dune2REditorMenu::onUnitChanged, this, std::placeholders::_1));
    stateDropDown.setOnSelectionChange(
        std::bind(&Dune2REditorMenu::onStateChanged, this, std::placeholders::_1));
    directionDropDown.setOnSelectionChange(
        std::bind(&Dune2REditorMenu::onDirectionChanged, this, std::placeholders::_1));
    modeDropDown.setOnSelectionChange(
        std::bind(&Dune2REditorMenu::onModeChanged, this, std::placeholders::_1));
    windowWidget.addWidget(&unitDropDown, Point(fieldX, rowY[0]), Point(fieldWidth, 25));
    windowWidget.addWidget(&stateDropDown, Point(fieldX, rowY[1]), Point(fieldWidth, 25));
    windowWidget.addWidget(&directionDropDown, Point(fieldX, rowY[2]), Point(fieldWidth, 25));
    windowWidget.addWidget(&modeDropDown, Point(fieldX, rowY[3]), Point(fieldWidth, 25));

    modeDropDown.addEntry(_("Layered"), static_cast<int>(GFXManager::EnhancedRenderMode::Layered));
    modeDropDown.addEntry(_("Full Animation"), static_cast<int>(GFXManager::EnhancedRenderMode::FullAnimation));
    modeDropDown.addEntry(_("Random"), static_cast<int>(GFXManager::EnhancedRenderMode::Random));

    previewWidget = std::make_unique<Dune2RPreviewWidget>();
    windowWidget.addWidget(previewWidget.get(), Point(originX + panelWidth - 255, originY + 78),
                           Point(225, 225));

    statusLabel.setTextFontSize(13);
    statusLabel.setAlignment(static_cast<Alignment_Enum>(Alignment_Left | Alignment_Top));
    windowWidget.addWidget(&statusLabel, Point(originX + 28, originY + 267),
                           Point(panelWidth - 56, 58));

    applyButton.setText(_("APPLY"));
    applyButton.setOnClick(std::bind(&Dune2REditorMenu::onApply, this));
    resetButton.setText(_("DEFAULT"));
    resetButton.setOnClick(std::bind(&Dune2REditorMenu::onResetSlot, this));
    reloadButton.setText(_("RELOAD MOUNTS"));
    reloadButton.setOnClick(std::bind(&Dune2REditorMenu::onReloadMounts, this));
    assetsButton.setText(_("ASSETS"));
    assetsButton.setOnClick(std::bind(&Dune2REditorMenu::onAssets, this));
    backButton.setText(_("BACK"));
    backButton.setOnClick(std::bind(&Dune2REditorMenu::onBack, this));
    saveVersionButton.setText(_("Save Version"));
    saveVersionButton.setOnClick([this]() { onSaveVersion(false); });
    shareButton.setText(_("Save to metaserver"));
    shareButton.setOnClick([this]() { onSaveVersion(true); });
    windowWidget.addWidget(&saveVersionButton, Point(originX + 25, originY + panelHeight - 80), Point(245,28));
    windowWidget.addWidget(&shareButton, Point(originX + 284, originY + panelHeight - 80), Point(245,28));
    const int buttonY = originY + panelHeight - 42;
    windowWidget.addWidget(&assetsButton, Point(originX + 25, buttonY), Point(90, 28));
    windowWidget.addWidget(&reloadButton, Point(originX + 123, buttonY), Point(112, 28));
    windowWidget.addWidget(&applyButton, Point(originX + 243, buttonY), Point(85, 28));
    windowWidget.addWidget(&resetButton, Point(originX + 336, buttonY), Point(100, 28));
    windowWidget.addWidget(&backButton, Point(originX + 444, buttonY), Point(85, 28));

    rebuildUnitEntries();
}

void Dune2REditorMenu::rebuildUnitEntries() {
    unitDropDown.clearAllEntries();
    units = pGFXManager->getEnhancedUnitEditorInfo();
    for(size_t i = 0; i < units.size(); ++i) {
        std::string label = units[i].sourceUnit;
        try {
            const std::string itemName = getItemNameByID(units[i].itemID);
            if(label.empty()) {
                label = itemName;
            } else if(label != itemName) {
                label += " (" + itemName + ")";
            }
        } catch(const std::exception&) {
            if(label.empty()) {
                label = "Unit " + std::to_string(units[i].itemID);
            }
        }
        unitDropDown.addEntry(label, static_cast<int>(i));
    }

    const bool hasUnits = !units.empty();
    unitDropDown.setEnabled(hasUnits);
    stateDropDown.setEnabled(hasUnits);
    directionDropDown.setEnabled(hasUnits);
    modeDropDown.setEnabled(hasUnits);
    applyButton.setEnabled(hasUnits);
    resetButton.setEnabled(hasUnits);
    if(hasUnits) {
        unitDropDown.setSelectedItem(0);
        rebuildStateEntries();
    } else {
        previewWidget->clearSelection();
        refreshStatus(_("No packaged Dune2R full-unit assets were found."));
    }
}

Dune2REditorMenu::~Dune2REditorMenu() = default;

const GFXManager::EnhancedUnitEditorInfo* Dune2REditorMenu::selectedUnit() const {
    const int index = unitDropDown.getSelectedEntryIntData();
    return index >= 0 && index < static_cast<int>(units.size()) ? &units[index] : nullptr;
}

GFXManager::EnhancedUnitState Dune2REditorMenu::selectedState() const {
    return static_cast<GFXManager::EnhancedUnitState>(
        std::max(0, stateDropDown.getSelectedEntryIntData()));
}

int Dune2REditorMenu::selectedDirection() const {
    return directionDropDown.getSelectedEntryIntData();
}

GFXManager::EnhancedRenderMode Dune2REditorMenu::selectedMode() const {
    const int value = modeDropDown.getSelectedEntryIntData();
    return value >= 0 ? static_cast<GFXManager::EnhancedRenderMode>(value)
                      : GFXManager::EnhancedRenderMode::FullAnimation;
}

void Dune2REditorMenu::onUnitChanged(bool) {
    rebuildStateEntries();
}

void Dune2REditorMenu::onStateChanged(bool) {
    rebuildDirectionEntries();
}

void Dune2REditorMenu::onDirectionChanged(bool) {
    refreshSelection();
}

void Dune2REditorMenu::onModeChanged(bool) {
    refreshStatus();
}

void Dune2REditorMenu::rebuildStateEntries() {
    stateDropDown.clearAllEntries();
    const auto* unit = selectedUnit();
    if(unit == nullptr) {
        rebuildDirectionEntries();
        return;
    }

    for(int state = 0; state < static_cast<int>(GFXManager::EnhancedUnitState::Count); ++state) {
        if(std::any_of(unit->available[state].begin(), unit->available[state].end(),
                       [](bool available) { return available; })) {
            stateDropDown.addEntry(_(kStateLabels[state]), state);
        }
    }
    if(stateDropDown.getNumEntries() > 0) {
        stateDropDown.setSelectedItem(0);
    }
    rebuildDirectionEntries();
}

void Dune2REditorMenu::rebuildDirectionEntries() {
    directionDropDown.clearAllEntries();
    const auto* unit = selectedUnit();
    const int state = stateDropDown.getSelectedEntryIntData();
    if(unit == nullptr || state < 0
       || state >= static_cast<int>(GFXManager::EnhancedUnitState::Count)) {
        refreshSelection();
        return;
    }

    for(int direction = 0; direction < 8; ++direction) {
        if(unit->available[state][direction]) {
            directionDropDown.addEntry(_(kDirectionLabels[direction]), direction);
        }
    }
    if(directionDropDown.getNumEntries() > 0) {
        directionDropDown.setSelectedItem(0);
    }
    refreshSelection();
}

void Dune2REditorMenu::refreshSelection() {
    const auto* unit = selectedUnit();
    const int direction = selectedDirection();
    if(unit == nullptr || direction < 0) {
        previewWidget->clearSelection();
        modeDropDown.setEnabled(false);
        applyButton.setEnabled(false);
        resetButton.setEnabled(false);
        refreshStatus();
        return;
    }

    modeDropDown.setEnabled(true);
    applyButton.setEnabled(true);
    resetButton.setEnabled(true);
    const int lookupHouse = unit->houseID >= 0 ? unit->houseID : HOUSE_HARKONNEN;
    const auto mode = pGFXManager->getEnhancedUnitRenderMode(
        unit->itemID, lookupHouse, selectedState(), direction);
    for(int i = 0; i < modeDropDown.getNumEntries(); ++i) {
        if(modeDropDown.getEntryIntData(i) == static_cast<int>(mode)) {
            modeDropDown.setSelectedItem(i);
            break;
        }
    }
    previewWidget->setSelection(unit->itemID, unit->houseID, selectedState(), direction);
    refreshStatus();
}

void Dune2REditorMenu::refreshStatus(const std::string& message) {
    if(!message.empty()) {
        statusLabel.setText(message);
        return;
    }
    const auto* unit = selectedUnit();
    if(unit == nullptr || selectedDirection() < 0) {
        statusLabel.setText(_("Select an available animation slot."));
        return;
    }

    std::string text = std::string(kStateLabels[static_cast<int>(selectedState())])
                       + " / " + kDirectionLabels[selectedDirection()]
                       + "\nMode: " + renderModeLabel(selectedMode());
    if(selectedMode() == GFXManager::EnhancedRenderMode::Layered) {
        text += _("\nUses independent DuneLegacy chassis/turret rendering.");
    } else if(selectedMode() == GFXManager::EnhancedRenderMode::Random) {
        text += _("\nChooses once per movement, facing, stop, or firing transition.");
    } else {
        text += _("\nUses the packaged complete-unit animation shown at right.");
    }
    statusLabel.setText(text);
}

void Dune2REditorMenu::onApply() {
    const auto* unit = selectedUnit();
    const int direction = selectedDirection();
    if(unit == nullptr || direction < 0) {
        return;
    }
    if(!pGFXManager->setEnhancedUnitRenderMode(unit->itemID, unit->houseID,
                                           selectedState(), direction, selectedMode())) {
        openWindow(MsgBox::create(_("Could not save this animation setting.")));
        return;
    }
    onSaveVersion(false);
}

void Dune2REditorMenu::onResetSlot() {
    const auto* unit = selectedUnit();
    const int direction = selectedDirection();
    if(unit == nullptr || direction < 0) {
        return;
    }
    if(!pGFXManager->setEnhancedUnitRenderMode(unit->itemID, unit->houseID,
                                           selectedState(), direction, GFXManager::EnhancedRenderMode::FullAnimation)) {
        openWindow(MsgBox::create(_("Could not save this animation setting.")));
        return;
    }
    refreshSelection();
    onSaveVersion(false);
}

void Dune2REditorMenu::onReloadMounts() {
    pGFXManager->reloadEnhancedUnitMounts();
    rebuildUnitEntries();
    refreshStatus(_("Mounted Dune2R unit assets reloaded."));
}

void Dune2REditorMenu::onAssets() {
    Dune2RAssetMenu menu;
    menu.showMenu();
    pGFXManager->reloadEnhancedUnitMounts();
    rebuildUnitEntries();
    onSaveVersion(false);
}

void Dune2REditorMenu::onSaveVersion(bool share) {
    try {
        const auto revision = Workshop::saveMod(ModManager::instance().getActiveModName());
        refreshStatus(_("Saved mod version ") + std::to_string(revision.version));
        if(share) Workshop::shareRevision(revision);
    } catch(const std::exception& error) {
        openWindow(MsgBox::create(std::string(_("Could not save or share assets: ")) + error.what()));
    }
}

void Dune2REditorMenu::onBack() {
    quit();
}
