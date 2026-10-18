#include <Menu/ModEditorMenu.h>
#include <globals.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <GUI/MsgBox.h>
#include <GUI/QstBox.h>
#include <mod/ModManager.h>
#include <mod/Workshop.h>
#include <sand.h>
#include <misc/string_util.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace {
constexpr const char* filenames[] = {"ObjectData.ini", "GameOptions.ini", "QuantBot Config.ini"};
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}
bool integer(const std::string& value) {
    if(value.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const long long number = std::strtoll(value.c_str(), &end, 10);
    return errno != ERANGE && end != value.c_str() && *end == '\0'
        && number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max();
}
bool number(const std::string& value) {
    if(value.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const double result = std::strtod(value.c_str(), &end);
    return errno != ERANGE && end != value.c_str() && *end == '\0' && std::isfinite(result);
}
}

ModEditorMenu::ModEditorMenu(const std::string& name) : modName(name), modPath(ModManager::instance().getModPath(name)) {
    setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
    resize(getTextureSize(pGFXManager->getUIGraphic(UI_MenuBackground)));
    setWindowWidget(&windowWidget);
    const int width = std::min(740, getSize().x - 40), height = 432;
    const int x = (getSize().x - width) / 2, y = std::max(10, (getSize().y - height) / 2);
    titleLabel.setText(_("Mod Editor"));
    titleLabel.setTextFontSize(22);
    titleLabel.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&titleLabel, Point(x,y), Point(width,32));
    auto row = [&](Label& label, Widget& widget, const char* text, int offset) {
        label.setText(_(text)); label.setTextFontSize(13);
        windowWidget.addWidget(&label, Point(x,y+offset), Point(105,26));
        windowWidget.addWidget(&widget, Point(x+112,y+offset), Point(width-112,26));
    };
    row(nameLabel, nameText, "Name", 42);
    row(authorLabel, authorText, "Author", 75);
    row(descriptionLabel, descriptionText, "Description", 108);
    row(fileLabel, fileChoice, "Edit", 155);
    row(sectionLabel, sectionChoice, "Section", 191);
    row(keyLabel, keyChoice, "Property", 227);
    row(valueLabel, valueText, "Value", 263);
    nameText.setMaximumTextLength(100);
    authorText.setMaximumTextLength(100);
    descriptionText.setMaximumTextLength(300);
    valueText.setMaximumTextLength(512);
    hintLabel.setTextFontSize(12);
    windowWidget.addWidget(&hintLabel, Point(x+112,y+294), Point(width-112,36));
    statusLabel.setTextFontSize(13);
    statusLabel.setText(_("Save creates a version only when content changes."));
    windowWidget.addWidget(&statusLabel, Point(x,y+338), Point(width,46));
    saveButton.setText(_("Save Version"));
    backButton.setText(_("Back"));
    saveButton.setOnClick([this]() { save(); });
    backButton.setOnClick([this]() { quit(); });
    const int buttonWidth = (width-10)/2;
    windowWidget.addWidget(&saveButton, Point(x,y+396), Point(buttonWidth,32));
    windowWidget.addWidget(&backButton, Point(x+width-buttonWidth,y+396), Point(buttonWidth,32));
    const auto info = ModManager::instance().getModInfo(modName);
    nameText.setText(info.displayName);
    authorText.setText(info.author);
    descriptionText.setText(info.description);
    auto dirty = [this](bool) { if(!changingSelection) modified = true; };
    nameText.setOnTextChange(dirty); authorText.setOnTextChange(dirty);
    descriptionText.setOnTextChange(dirty); valueText.setOnTextChange(dirty);
    try {
        for(size_t i = 0; i < documents.size(); ++i) {
            const auto path = std::filesystem::path(modPath) / filenames[i];
            if(!std::filesystem::is_regular_file(path))
                throw std::runtime_error(std::string("Missing working-copy file: ") + filenames[i]);
            documents[i] = std::make_unique<INIFile>(path.string());
        }
        fileChoice.addEntry(_("Units & Buildings"));
        fileChoice.addEntry(_("Game Rules"));
        fileChoice.addEntry(_("AI Behaviour"));
        fileChoice.setSelectedItem(0);
        fileChoice.setOnSelectionChange([this](bool) { chooseFile(); });
        sectionChoice.setOnSelectionChange([this](bool) { chooseSection(); });
        keyChoice.setOnSelectionChange([this](bool) { chooseKey(); });
        sectionChoice.setNumVisibleEntries(6); keyChoice.setNumVisibleEntries(6);
        loadSections();
    } catch(const std::exception& error) {
        statusLabel.setText(error.what()); saveButton.setEnabled(false);
    }
    nameText.setActive();
}
ModEditorMenu::~ModEditorMenu() = default;

void ModEditorMenu::chooseFile() {
    if(changingSelection) return;
    if(!applyValue()) { changingSelection = true; fileChoice.setSelectedItem(fileIndex); changingSelection = false; return; }
    fileIndex = fileChoice.getSelectedIndex(); loadSections();
}
void ModEditorMenu::chooseSection() {
    if(changingSelection) return;
    if(!applyValue()) { changingSelection = true; sectionChoice.setSelectedItem(sectionIndex); changingSelection = false; return; }
    sectionIndex = sectionChoice.getSelectedIndex(); loadKeys();
}
void ModEditorMenu::chooseKey() {
    if(changingSelection) return;
    if(!applyValue()) { changingSelection = true; keyChoice.setSelectedItem(keyIndex); changingSelection = false; return; }
    keyIndex = keyChoice.getSelectedIndex(); loadValue();
}
void ModEditorMenu::loadSections() {
    changingSelection = true;
    sections.clear(); sectionChoice.clearAllEntries();
    for(auto it = documents[fileIndex]->begin(); it != documents[fileIndex]->end(); ++it) {
        if(it->begin() == it->end()) continue;
        sections.push_back(it->getSectionName()); sectionChoice.addEntry(sections.back());
    }
    sectionIndex = sections.empty() ? -1 : 0;
    sectionChoice.setSelectedItem(sectionIndex);
    changingSelection = false;
    loadKeys();
}
void ModEditorMenu::loadKeys() {
    changingSelection = true;
    keys.clear(); keyChoice.clearAllEntries();
    if(sectionIndex >= 0) {
        for(auto it = documents[fileIndex]->begin(sections[sectionIndex]); it != documents[fileIndex]->end(sections[sectionIndex]); ++it) {
            keys.push_back(it->getKeyName()); keyChoice.addEntry(keys.back());
        }
    }
    keyIndex = keys.empty() ? -1 : 0; keyChoice.setSelectedItem(keyIndex);
    changingSelection = false;
    loadValue();
}
void ModEditorMenu::loadValue() {
    changingSelection = true;
    valueText.setEnabled(keyIndex >= 0);
    const auto value = keyIndex < 0 ? "" : documents[fileIndex]->getStringValue(sections[sectionIndex], keys[keyIndex]);
    valueText.setText(value);
    const auto normalized = lower(trim(value));
    valueType = normalized == "true" || normalized == "false" ? ValueType::Boolean
              : integer(normalized) ? ValueType::Integer : number(normalized) ? ValueType::Number : ValueType::Text;
    if(keyIndex >= 0) {
        std::string key = lower(keys[keyIndex]);
        key = key.substr(0, key.find('('));
        if(fileIndex == 0) {
            if(key == "enabled") valueType = ValueType::Boolean;
            else if(key == "maxspeed" || key == "turnspeed") valueType = ValueType::Number;
            else if(key == "builder" || key == "prerequisite") valueType = ValueType::Text;
        } else if(fileIndex == 2) {
            if(key.find("enabled") != std::string::npos) valueType = ValueType::Boolean;
            else if(lower(sections[sectionIndex]) == "unit ratios" || key.find("percent") != std::string::npos
                 || key.find("ratio") != std::string::npos || key.find("militaryvaluemultiplier") != std::string::npos)
                valueType = ValueType::Number;
        }
    }
    const char* hints[] = {"Boolean: true / false or 1 / 0", "Whole number", "Finite decimal number", "Text or a named game value"};
    hintLabel.setText(std::string(_(hints[static_cast<int>(valueType)])) + "\n" + _("Changes stay in this draft until you save."));
    changingSelection = false;
}
bool ModEditorMenu::applyValue() {
    if(keyIndex < 0 || sectionIndex < 0 || !documents[fileIndex]) return true;
    auto value = trim(valueText.getText());
    bool valid = value.find_first_of("\r\n") == std::string::npos;
    if(valueType == ValueType::Boolean) {
        value = lower(value); valid = value == "true" || value == "false" || value == "0" || value == "1";
    } else if(valueType == ValueType::Integer) valid = integer(value);
    else if(valueType == ValueType::Number) valid = number(value);
    if(valid && fileIndex == 0 && valueType == ValueType::Text && !value.empty() && lower(value) != "invalid") {
        const std::string key = lower(keys[keyIndex]).substr(0, keys[keyIndex].find('('));
        if(key == "builder") valid = getItemIDByName(value) != ItemID_Invalid;
        else if(key == "prerequisite") {
            for(const auto& item : splitStringToStringVector(value)) {
                const int id = getItemIDByName(trim(item));
                if(id == ItemID_Invalid || !isStructure(id)) { valid = false; break; }
            }
        }
    }
    if(!valid) {
        openWindow(MsgBox::create(_("Invalid value. Use the value type shown below the field.\nNumbers must be finite and within the supported range.")));
        return false;
    }
    auto& document = *documents[fileIndex];
    if(document.getStringValue(sections[sectionIndex], keys[keyIndex]) != value) {
        document.setStringValue(sections[sectionIndex], keys[keyIndex], value);
        changed[fileIndex] = true; modified = true;
    }
    return true;
}

void ModEditorMenu::save() {
    if(!applyValue()) return;
    if(trim(nameText.getText()).empty()) { openWindow(MsgBox::create(_("Please enter a display name."))); return; }
    namespace fs = std::filesystem;
    // Stage outside the mod tree so temporary files can never enter a revision.
    const fs::path staging = fs::path(modPath).parent_path() / (".workshop-edit-" + modName);
    std::vector<std::string> installed;
    bool committed = false, createdStaging = false;
    try {
        if(fs::exists(staging)) throw std::runtime_error("An earlier save needs recovery: " + staging.string());
        fs::create_directories(staging / "backup");
        createdStaging = true;
        for(size_t i = 0; i < documents.size(); ++i) {
            if(changed[i] && !documents[i]->saveChangesTo((staging / filenames[i]).string()))
                throw std::runtime_error(std::string("Could not write ") + filenames[i]);
        }
        auto info = ModManager::instance().getModInfo(modName);
        const bool metadataChanged = info.displayName != trim(nameText.getText())
            || info.author != authorText.getText() || info.description != descriptionText.getText();
        info.displayName = trim(nameText.getText()); info.author = authorText.getText(); info.description = descriptionText.getText();
        fs::copy_file(fs::path(modPath)/"mod.ini", staging/"mod.ini");
        if(metadataChanged && !ModManager::instance().writeModInfo(staging.string(), info))
            throw std::runtime_error("Could not save mod details.");
        for(int i = 0; i < 4; ++i) {
            const std::string filename = i < 3 ? filenames[i] : "mod.ini";
            if(i < 3 && !changed[i]) continue;
            fs::rename(fs::path(modPath)/filename, staging/"backup"/filename);
            installed.push_back(filename);
            fs::rename(staging/filename, fs::path(modPath)/filename);
        }
        const auto revision = Workshop::saveMod(modName);
        committed = true;
        modified = false; changed.fill(false);
        std::error_code ignored; fs::remove_all(staging, ignored);
        statusLabel.setText(_("Saved version ") + std::to_string(revision.version) + "\n"
            + _("Hosting an online game shares this exact saved version with joiners."));
    } catch(const std::exception& error) {
        std::string message = error.what();
        if(!committed && createdStaging) {
            bool restored = true;
            for(const auto& filename : installed) {
                std::error_code restoreError;
                fs::copy_file(staging/"backup"/filename, fs::path(modPath)/filename, fs::copy_options::overwrite_existing, restoreError);
                if(restoreError) { restored = false; message += "\nRecovery copy: " + (staging/"backup"/filename).string(); }
            }
            // Keep recovery copies only when restoring the original files failed.
            if(restored) { std::error_code ignored; fs::remove_all(staging, ignored); }
        }
        openWindow(MsgBox::create(std::string(_("Save failed: ")) + message));
    }
}
void ModEditorMenu::quit(int returnVal) {
    if(modified && !discardPrompt) {
        pendingReturn = returnVal; discardPrompt = true;
        openWindow(QstBox::create(_("Discard unsaved mod changes?"), _("Discard"), _("Keep Editing"), QSTBOX_BUTTON2));
    } else if(!modified) MenuBase::quit(returnVal);
}
void ModEditorMenu::onChildWindowClose(Window* child) {
    if(discardPrompt) {
        if(auto* question = dynamic_cast<QstBox*>(child)) {
            discardPrompt = false;
            if(question->getPressedButtonID() == QSTBOX_BUTTON1) { modified = false; MenuBase::quit(pendingReturn); }
        }
    }
}
