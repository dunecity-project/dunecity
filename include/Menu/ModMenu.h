#ifndef MODMENU_H
#define MODMENU_H

#include "MenuBase.h"
#include <GUI/StaticContainer.h>
#include <GUI/TextButton.h>
#include <GUI/TextBox.h>
#include <GUI/ListBox.h>
#include <GUI/Label.h>
#include <mod/ModInfo.h>
#include <vector>

/** Select an explicit working mod; game activation belongs to game setup. */
class ModMenu final : public MenuBase {
public:
    enum class Purpose { ModEditor, MapEditor, AssetEditors };
    explicit ModMenu(Purpose purpose = Purpose::ModEditor);
    ~ModMenu() override;
private:
    void onEdit();
    void onCreateNew();
    void refreshModList(const std::string& select = "");
    void updateModDetails();
    Purpose purpose;
    StaticContainer windowWidget;
    Label titleLabel, instructionsLabel, detailsLabel, newModLabel, statusLabel;
    ListBox modListBox;
    TextBox newModNameTextBox;
    TextButton createButton, editButton, backButton;
    std::vector<ModInfo> mods;
};
#endif
