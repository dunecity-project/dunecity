#ifndef SAVEMAPWINDOW_H
#define SAVEMAPWINDOW_H

#include <GUI/Window.h>
#include <GUI/HBox.h>
#include <GUI/VBox.h>
#include <GUI/Label.h>
#include <GUI/TextBox.h>
#include <GUI/TextButton.h>
#include <GUI/DropDownBox.h>

#include <string>

class Game;

/// Export an ordinary custom map without changing the running match.
class SaveMapWindow final : public Window {
public:
    SaveMapWindow(const Game& game, int color);
    static SaveMapWindow* create(const Game& game, int color);

    bool handleKeyPress(SDL_KeyboardEvent& key) override;
    void onChildWindowClose(Window* child) override;
    void onSave();
    const std::string& getSavedPath() const { return savedPath; }

private:
    void updateDetails();
    void finishSave(bool overwrite);
    void onCancel();
    void showError(const std::string& message);

    const Game& game;
    int color;
    std::string pendingPath;
    std::string pendingData;
    std::string savedPath;

    HBox mainHBox;
    VBox mainVBox;
    HBox buttons;
    Label title;
    Label nameLabel;
    TextBox mapName;
    Label momentLabel;
    DropDownBox moment;
    Label details;
    Label destinationLabel;
    TextButton saveButton;
    TextButton cancelButton;
};

#endif // SAVEMAPWINDOW_H
