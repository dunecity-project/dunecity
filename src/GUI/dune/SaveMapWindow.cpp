#include <GUI/dune/SaveMapWindow.h>

#include <GUI/MsgBox.h>
#include <GUI/QstBox.h>
#include <GUI/Spacer.h>
#include <FileClasses/TextManager.h>
#include <INIMap/MatchMapExport.h>
#include <Game.h>
#include <GameInitSettings.h>
#include <globals.h>
#include <misc/FileSystem.h>
#include <misc/MenuPalette.h>
#include <misc/string_util.h>

#include <algorithm>
#include <exception>
#include <utility>

SaveMapWindow::SaveMapWindow(const Game& game, int color)
    : Window(0, 0, 0, 0), game(game), color(color) {
    // Keep every control between the desert corner crests at 640x480.
    const int width = std::min(480, getRendererWidth() - 144);
    constexpr int height = 340;
    setBackground(GUIStyle::getInstance().createBackground(width, height));
    setCurrentPosition((getRendererWidth() - width) / 2, (getRendererHeight() - height) / 2, width, height);
    setWindowWidget(&mainHBox);
    mainHBox.addWidget(HSpacer::create(24));
    mainHBox.addWidget(&mainVBox);
    mainHBox.addWidget(HSpacer::create(24));
    mainVBox.addWidget(VSpacer::create(12));

    title.setText(_("Save as Map"));
    title.setTextFontSize(22);
    title.setTextColor(MenuTheme::text, COLOR_TRANSPARENT);
    title.setAlignment(static_cast<Alignment_Enum>(Alignment_HCenter | Alignment_VCenter));
    mainVBox.addWidget(&title, 32);
    mainVBox.addWidget(VSpacer::create(10));

    nameLabel.setText(_("Map name"));
    nameLabel.setTextColor(MenuTheme::text, COLOR_TRANSPARENT);
    mainVBox.addWidget(&nameLabel, 20);
    mapName.setTextColor(color);
    mapName.setMaximumTextLength(64);
    mapName.setForbiddenChars("?*:|<>/\\\"");
    mapName.setText(MatchMapExport::defaultName(game));
    mapName.setOnReturn([this]() { onSave(); });
    mainVBox.addWidget(&mapName, 28);
    mainVBox.addWidget(VSpacer::create(8));

    momentLabel.setText(_("Map state"));
    momentLabel.setTextColor(MenuTheme::text, COLOR_TRANSPARENT);
    mainVBox.addWidget(&momentLabel, 20);
    moment.setColor(color);
    moment.addEntry(_("From the start"), static_cast<int>(MatchMapExport::Moment::Start));
    moment.addEntry(_("From right now"), static_cast<int>(MatchMapExport::Moment::Current));
    moment.setSelectedItem(0);
    moment.setNumVisibleEntries(2);
    moment.setOnSelectionChange([this](bool) { updateDetails(); });
    mainVBox.addWidget(&moment, 28);
    mainVBox.addWidget(VSpacer::create(10));

    details.setTextColor(MenuTheme::muted, COLOR_TRANSPARENT);
    details.setAlignment(Alignment_Left);
    mainVBox.addWidget(&details, 72);
    mainVBox.addWidget(VSpacer::create(8));
    destinationLabel.setTextColor(MenuTheme::text, COLOR_TRANSPARENT);
    destinationLabel.setAlignment(Alignment_Left);
    destinationLabel.setText(MatchMapExport::playerCount(game.getGameInitSettings().getFiledata()) > 1
        ? _("Custom Game > MP User Maps\nAlso available in the map editor.")
        : _("Custom Game > SP User Maps\nAlso available in the map editor."));
    mainVBox.addWidget(&destinationLabel, 36);
    mainVBox.addWidget(VSpacer::create(12));

    saveButton.setText(_("Save"));
    saveButton.setTextColor(color);
    saveButton.setOnClick([this]() { onSave(); });
    cancelButton.setText(_("Cancel"));
    cancelButton.setTextColor(color);
    cancelButton.setOnClick([this]() { onCancel(); });
    buttons.addWidget(&saveButton);
    buttons.addWidget(HSpacer::create(12));
    buttons.addWidget(&cancelButton);
    mainVBox.addWidget(&buttons, 32);
    mainVBox.addWidget(VSpacer::create(12));
    updateDetails();
    mapName.setActive();
}

SaveMapWindow* SaveMapWindow::create(const Game& game, int color) {
    auto* dialog = new SaveMapWindow(game, color);
    dialog->pAllocated = true;
    return dialog;
}

void SaveMapWindow::updateDetails() {
    details.setText(moment.getSelectedIndex() == 0
        ? _("Original terrain, spice and starting armies.\nExports the match's embedded map unchanged.")
        : _("Current terrain, buildings, units and credits.\nCity growth, research and queues are not saved.\nUse Save Game to resume the complete match."));
}

bool SaveMapWindow::handleKeyPress(SDL_KeyboardEvent& key) {
    if(!hasChildWindow() && key.keysym.sym == SDLK_ESCAPE) {
        onCancel();
        return true;
    }
    return Window::handleKeyPress(key);
}

void SaveMapWindow::onCancel() {
    if(auto* parent = dynamic_cast<Window*>(getParent())) parent->closeChildWindow();
}

void SaveMapWindow::showError(const std::string& message) {
    const auto lines = greedyWordWrap(message, std::min(440, getRendererWidth() - 176),
        [](const std::string& line) { return GUIStyle::getInstance().getTextWidth(line, 16); });
    std::string wrapped;
    for(const auto& line : lines) {
        if(!wrapped.empty()) wrapped += '\n';
        wrapped += line;
    }
    openWindow(MsgBox::create(wrapped));
}

void SaveMapWindow::onSave() {
    try {
        const auto selected = moment.getSelectedIndex() == 0
            ? MatchMapExport::Moment::Start : MatchMapExport::Moment::Current;
        pendingData = MatchMapExport::mapData(game, selected);
        pendingPath = MatchMapExport::destination(mapName.getText(), pendingData);
        if(existsFile(pendingPath)) {
            auto* question = QstBox::create(_("A map with this name already exists.\nReplace it?"),
                _("Replace"), _("Cancel"), QSTBOX_BUTTON2);
            question->setTextColor(color);
            openWindow(question);
        } else {
            finishSave(false);
        }
    } catch(const std::exception& error) {
        pendingPath.clear();
        pendingData.clear();
        showError(error.what());
    }
}

void SaveMapWindow::finishSave(bool overwrite) {
    try {
        MatchMapExport::save(pendingPath, pendingData, overwrite);
        savedPath = std::move(pendingPath);
        const bool multiplayer = MatchMapExport::playerCount(pendingData) > 1;
        pendingData.clear();
        openWindow(MsgBox::create(_("Map saved.") + "\n"
            + (multiplayer ? _("Custom Game > MP User Maps") : _("Custom Game > SP User Maps"))
            + "\n" + _("Also available in the map editor.")));
    } catch(const std::exception& error) {
        pendingPath.clear();
        pendingData.clear();
        showError(error.what());
    }
}

void SaveMapWindow::onChildWindowClose(Window* child) {
    if(auto* question = dynamic_cast<QstBox*>(child)) {
        if(question->getPressedButtonID() == QSTBOX_BUTTON1 && !pendingPath.empty()) {
            finishSave(true);
        } else {
            pendingPath.clear();
            pendingData.clear();
        }
    }
}
