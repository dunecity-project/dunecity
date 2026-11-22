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

#ifndef RESTARTDIFFICULTYWINDOW_H
#define RESTARTDIFFICULTYWINDOW_H

#include <GUI/Window.h>
#include <GUI/HBox.h>
#include <GUI/VBox.h>
#include <GUI/Label.h>
#include <GUI/TextButton.h>
#include <GUI/DropDownBox.h>

#include <string>
#include <vector>

/**
    The in-game menu's "Restart on New Difficulty" dialog: a list of enemy difficulties
    (EnemyDifficultyPolicy.h), Restart and Cancel. The menu reads getChosenClass() when the
    dialog closes; it is empty unless the player pressed Restart.
*/
class RestartDifficultyWindow : public Window {
public:
    /// classes: the selectable enemy classes in list order; current: the one played now ("" if none).
    RestartDifficultyWindow(const std::vector<std::string>& classes, const std::string& current, int color);
    static RestartDifficultyWindow* create(const std::vector<std::string>& classes, const std::string& current, int color);

    bool handleKeyPress(SDL_KeyboardEvent& key) override;
    void onRestart();
    void onCancel();
    const std::string& getChosenClass() const { return chosen; }

private:
    std::vector<std::string> classes;
    std::string chosen;

    HBox mainHBox;
    VBox mainVBox;
    HBox buttons;
    Label title;
    Label currentLabel;
    Label difficultyLabel;
    DropDownBox difficulty;
    Label note;
    TextButton restartButton;
    TextButton cancelButton;
};

#endif // RESTARTDIFFICULTYWINDOW_H
