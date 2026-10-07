#include <Menu/ModMenu.h>
#include <Menu/ModEditorMenu.h>
#include <Menu/Dune2REditorMenu.h>
#include <MapEditor/MapEditor.h>
#include <globals.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/INIFile.h>
#include <filesystem>
#include <GUI/MsgBox.h>
#include <mod/ModManager.h>
#include <mod/Workshop.h>
#include <algorithm>
#include <cctype>

namespace {
bool immutableMod(const std::string& name) {
    const auto file = std::filesystem::path(ModManager::instance().getModPath(name)) / "workshop-revision.ini";
    return std::filesystem::is_regular_file(file) && INIFile(file.string()).getBoolValue("Workshop", "Immutable", false);
}
bool bundledMod(const std::string& name) {
    return name == "vanilla" || name == "dunecity" || name == "Dune2R" || name == "Tornie";
}
}

ModMenu::ModMenu(Purpose purpose) : purpose(purpose) {
    setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
    resize(getTextureSize(pGFXManager->getUIGraphic(UI_MenuBackground)));
    setWindowWidget(&windowWidget);
    const int width = std::min(720, getSize().x - 40);
    const int height = std::min(520, getSize().y - 32);
    const int x = (getSize().x - width) / 2, y = (getSize().y - height) / 2;
    titleLabel.setText(purpose == Purpose::ModEditor ? _("Mod Editor") :
                       purpose == Purpose::MapEditor ? _("Map Editor - Choose Mod") : _("Asset Editors - Choose Mod"));
    titleLabel.setTextFontSize(22);
    titleLabel.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&titleLabel, Point(x,y), Point(width,36));
    instructionsLabel.setText(purpose == Purpose::ModEditor
        ? _("Choose a draft to edit, or make a copy of any mod below.")
        : purpose == Purpose::MapEditor
            ? _("Choose the units, buildings and rules to use while editing maps.")
            : _("Choose a Dune2R mod to preview sprites and edit animation settings."));
    instructionsLabel.setTextFontSize(13);
    instructionsLabel.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&instructionsLabel, Point(x,y+38), Point(width,36));
    const int listHeight = height - 235;
    modListBox.setOnSelectionChange([this](bool) { updateModDetails(); });
    windowWidget.addWidget(&modListBox, Point(x,y+80), Point(width,listHeight));
    detailsLabel.setTextFontSize(13);
    windowWidget.addWidget(&detailsLabel, Point(x,y+85+listHeight), Point(width,40));
    newModLabel.setText(_("New copy name:"));
    newModLabel.setTextFontSize(13);
    windowWidget.addWidget(&newModLabel, Point(x,y+height-100), Point(120,28));
    newModNameTextBox.setMaximumTextLength(64);
    windowWidget.addWidget(&newModNameTextBox, Point(x+125,y+height-100), Point(width-275,28));
    createButton.setText(_("Create Copy"));
    createButton.setOnClick([this]() { onCreateNew(); });
    windowWidget.addWidget(&createButton, Point(x+width-140,y+height-100), Point(140,28));
    metaserverButton.setText(_("Load from metaserver"));
    metaserverButton.setTooltipText(_("Download mods published on the metaserver"));
    metaserverButton.setOnClick([this]() { onLoadFromMetaserver(); });
    const int metaserverWidth = std::max(150, metaserverButton.getMinimumSize().x + 8);
    windowWidget.addWidget(&metaserverButton, Point(x+width-metaserverWidth,y+height-66), Point(metaserverWidth,28));
    statusLabel.setTextFontSize(12);
    windowWidget.addWidget(&statusLabel, Point(x,y+height-68), Point(width-metaserverWidth-10,26));
    editButton.setText(purpose == Purpose::ModEditor ? _("Edit Draft") : _("Open Editor"));
    editButton.setOnClick([this]() { onEdit(); });
    windowWidget.addWidget(&editButton, Point(x,y+height-32), Point(width/2-8,32));
    backButton.setText(_("Back"));
    backButton.setOnClick([this]() { quit(); });
    windowWidget.addWidget(&backButton, Point(x+width/2+8,y+height-32), Point(width/2-8,32));
    refreshModList();
    modListBox.setActive();
}

ModMenu::~ModMenu() = default;

void ModMenu::refreshModList(const std::string& select) {
    modListBox.clearAllEntries();
    mods.clear();
    for(const auto& mod : ModManager::instance().listModChoices()) {
        if(purpose == Purpose::AssetEditors && ModManager::instance().getContentBase(mod.name) != "Dune2R") continue;
        mods.push_back(mod);
    }
    int selected = 0;
    for(size_t i = 0; i < mods.size(); ++i) {
        modListBox.addEntry(mods[i].selectionLabel() + (bundledMod(mods[i].name) ? _("  (bundled)") : immutableMod(mods[i].name) ? _("  (shared version)") : _("  (draft)")));
        if(mods[i].matchesSelectionName(select)) selected = static_cast<int>(i);
    }
    if(!mods.empty()) modListBox.setSelectedItem(selected);
    updateModDetails();
}

void ModMenu::updateModDetails() {
    const int index = modListBox.getSelectedIndex();
    const bool selected = index >= 0 && index < static_cast<int>(mods.size());
    // Asset editors can inspect protected mods. Their download manager guards
    // immutable writes, and shared-version render preferences live in user config.
    editButton.setEnabled(selected && (purpose != Purpose::ModEditor
        || (!bundledMod(mods[index].name) && !immutableMod(mods[index].name))));
    createButton.setEnabled(selected);
    if(!selected) { detailsLabel.setText(_("No compatible mods installed.")); return; }
    const auto& mod = mods[index];
    detailsLabel.setText(mod.displayName + "  |  " + mod.author + "\n" + mod.description);
    if(purpose == Purpose::AssetEditors) {
        statusLabel.setText(immutableMod(mod.name)
            ? _("Shared version: inspect sprites and set render preferences. Downloads stay disabled.")
            : _("Preview sprites, set render preferences and download asset packs into this mod."));
        return;
    }
    statusLabel.setText((purpose != Purpose::MapEditor && bundledMod(mod.name)) || immutableMod(mod.name)
        ? _("Bundled and shared versions are protected. Create a copy to edit.")
        : _("The selected mod is used only for this editor session."));
}

void ModMenu::onLoadFromMetaserver() {
    const int index = modListBox.getSelectedIndex();
    const std::string selected = index >= 0 && index < static_cast<int>(mods.size()) ? mods[index].name : std::string();
    try {
        Workshop::openMetaserverMods();
    } catch(const std::exception& error) {
        openWindow(MsgBox::create(std::string(_("Could not open the metaserver mods: ")) + error.what()));
    }
    // Downloads install read-only snapshots; refresh so they appear (and stay protected from editing).
    refreshModList(selected);
}

void ModMenu::onCreateNew() {
    const int index = modListBox.getSelectedIndex();
    if(index < 0 || index >= static_cast<int>(mods.size())) return;
    const std::string name = newModNameTextBox.getText();
    if(name.empty() || name.front() == ' ' || name.back() == ' '
       || std::any_of(name.begin(), name.end(), [](unsigned char c) {
           return !std::isalnum(c) && c != ' ' && c != '-' && c != '_';
       })) {
        openWindow(MsgBox::create(_("Enter a name using letters, numbers, spaces, - or _.\nDo not start or end with a space.")));
        return;
    }
    auto& manager = ModManager::instance();
    if(manager.modExists(name)) { openWindow(MsgBox::create(_("A mod with that name already exists."))); return; }
    if(!manager.createMod(name, mods[index].name)) {
        openWindow(MsgBox::create(_("Could not create the mod copy. Check available disk space.")));
        return;
    }
    newModNameTextBox.setText("");
    refreshModList(name);
    onEdit();
}

void ModMenu::onEdit() {
    const int index = modListBox.getSelectedIndex();
    if(index < 0 || index >= static_cast<int>(mods.size())) return;
    const std::string name = mods[index].name;
    if(purpose == Purpose::ModEditor) {
        if(bundledMod(name) || immutableMod(name)) return;
        ModEditorMenu(name).showMenu();
    } else {
        // Asset editors enforce their own write guards. Both editors restore
        // the previous active mod and rules when the session ends.
        auto& manager = ModManager::instance();
        const std::string previous = manager.getActiveModName();
        const auto previousOptions = effectiveGameOptions;
        if(!manager.setActiveMod(name)) {
            openWindow(MsgBox::create(_("Could not load the selected working mod.")));
            return;
        }
        effectiveGameOptions = manager.loadEffectiveGameOptions(settings.gameOptions);
        try {
            if(purpose == Purpose::MapEditor) MapEditor().RunEditor();
            else Dune2REditorMenu().showMenu();
        } catch(const std::exception& error) {
            manager.setActiveMod(previous);
            effectiveGameOptions = previousOptions;
            openWindow(MsgBox::create(std::string(_("Could not open editor: ")) + error.what()));
            return;
        }
        if(!manager.setActiveMod(previous))
            openWindow(MsgBox::create(_("The previous game mod could not be restored.\nChoose your mod when starting the next game.")));
        effectiveGameOptions = previousOptions;
    }
    refreshModList(name);
}
