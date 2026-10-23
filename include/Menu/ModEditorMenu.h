#ifndef MODEDITORMENU_H
#define MODEDITORMENU_H

#include "MenuBase.h"
#include <GUI/StaticContainer.h>
#include <GUI/TextButton.h>
#include <GUI/TextBox.h>
#include <GUI/DropDownBox.h>
#include <GUI/Label.h>
#include <FileClasses/INIFile.h>
#include <array>
#include <memory>
#include <string>
#include <vector>

/** Edit a working copy. Immutable content revisions are created only by Save. */
class ModEditorMenu final : public MenuBase {
public:
    explicit ModEditorMenu(const std::string& modName);
    ~ModEditorMenu() override;
    void quit(int returnVal = MENU_QUIT_DEFAULT) override;
    void onChildWindowClose(Window* child) override;
private:
    enum class ValueType { Boolean, Integer, Number, Text };
    void chooseFile();
    void chooseSection();
    void chooseKey();
    void loadSections();
    void loadKeys();
    void loadValue();
    bool applyValue();
    void save();
    std::string modName, modPath;
    std::array<std::unique_ptr<INIFile>, 3> documents;
    std::array<bool, 3> changed{{false,false,false}};
    std::vector<std::string> sections, keys;
    int fileIndex = 0, sectionIndex = -1, keyIndex = -1;
    ValueType valueType = ValueType::Text;
    bool changingSelection = false, modified = false, discardPrompt = false;
    int pendingReturn = MENU_QUIT_DEFAULT;
    StaticContainer windowWidget;
    Label titleLabel, nameLabel, authorLabel, descriptionLabel, fileLabel, sectionLabel, keyLabel, valueLabel, hintLabel, statusLabel;
    TextBox nameText, authorText, descriptionText, valueText;
    DropDownBox fileChoice, sectionChoice, keyChoice;
    TextButton saveButton, backButton;
};
#endif
