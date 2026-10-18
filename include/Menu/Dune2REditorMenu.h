/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef DUNE2REDITORMENU_H
#define DUNE2REDITORMENU_H

#include "MenuBase.h"

#include <FileClasses/GFXManager.h>
#include <GUI/DropDownBox.h>
#include <GUI/Label.h>
#include <GUI/StaticContainer.h>
#include <GUI/TextButton.h>

#include <memory>
#include <vector>

class Dune2RPreviewWidget;

class Dune2REditorMenu final : public MenuBase {
public:
    Dune2REditorMenu();
    ~Dune2REditorMenu() override;

    Dune2REditorMenu(const Dune2REditorMenu&) = delete;
    Dune2REditorMenu(Dune2REditorMenu&&) = delete;
    Dune2REditorMenu& operator=(const Dune2REditorMenu&) = delete;
    Dune2REditorMenu& operator=(Dune2REditorMenu&&) = delete;

private:
    void onUnitChanged(bool interactive);
    void onStateChanged(bool interactive);
    void onDirectionChanged(bool interactive);
    void onModeChanged(bool interactive);
    void onApply();
    void onResetSlot();
    void onReloadMounts();
    void onAssets();
    void onSaveVersion();
    void onBack();
    void rebuildStateEntries();
    void rebuildDirectionEntries();
    void refreshSelection();
    void refreshStatus(const std::string& message = "");
    void rebuildUnitEntries();

    const GFXManager::EnhancedUnitEditorInfo* selectedUnit() const;
    GFXManager::EnhancedUnitState selectedState() const;
    int selectedDirection() const;
    GFXManager::EnhancedRenderMode selectedMode() const;

    StaticContainer windowWidget;
    Label titleLabel;
    Label introLabel;
    Label unitLabel;
    Label stateLabel;
    Label directionLabel;
    Label modeLabel;
    Label statusLabel;
    DropDownBox unitDropDown;
    DropDownBox stateDropDown;
    DropDownBox directionDropDown;
    DropDownBox modeDropDown;
    TextButton applyButton;
    TextButton resetButton;
    TextButton reloadButton;
    TextButton assetsButton;
    TextButton backButton;
    TextButton saveVersionButton;
    std::unique_ptr<Dune2RPreviewWidget> previewWidget;
    std::vector<GFXManager::EnhancedUnitEditorInfo> units;
};

#endif // DUNE2REDITORMENU_H
