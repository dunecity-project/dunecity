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

#include <GUI/dune/RestartDifficultyWindow.h>

#include <globals.h>

#include <FileClasses/TextManager.h>
#include <GUI/GUIStyle.h>
#include <GUI/Spacer.h>
#include <misc/DrawingRectHelper.h>
#include <misc/MenuPalette.h>
#include <players/EnemyDifficultyPolicy.h>

#include <algorithm>

RestartDifficultyWindow::RestartDifficultyWindow(const std::vector<std::string>& classes, const std::string& current, int color)
    : Window(0, 0, 0, 0), classes(classes) {
    // Between the desert corner crests at 640x480, as the Save as Map dialog.
    const int width = std::min(440, getRendererWidth() - 144);
    constexpr int height = 248;
    setBackground(GUIStyle::getInstance().createBackground(width, height));
    setCurrentPosition((getRendererWidth() - width) / 2, (getRendererHeight() - height) / 2, width, height);
    setWindowWidget(&mainHBox);
    mainHBox.addWidget(HSpacer::create(24));
    mainHBox.addWidget(&mainVBox);
    mainHBox.addWidget(HSpacer::create(24));
    mainVBox.addWidget(VSpacer::create(12));

    title.setText(_("Restart on New Difficulty"));
    title.setTextFontSize(22);
    title.setTextColor(MenuTheme::text, COLOR_TRANSPARENT);
    title.setAlignment(static_cast<Alignment_Enum>(Alignment_HCenter | Alignment_VCenter));
    mainVBox.addWidget(&title, 32);
    mainVBox.addWidget(VSpacer::create(8));

    currentLabel.setText(_("Enemy AI now:") + " " + (current.empty() ? std::string("-") : EnemyDifficultyPolicy::label(current)));
    currentLabel.setTextColor(MenuTheme::muted, COLOR_TRANSPARENT);
    currentLabel.setAlignment(Alignment_Left);
    mainVBox.addWidget(&currentLabel, 20);
    mainVBox.addWidget(VSpacer::create(6));

    difficultyLabel.setText(_("New enemy AI"));
    difficultyLabel.setTextColor(MenuTheme::text, COLOR_TRANSPARENT);
    mainVBox.addWidget(&difficultyLabel, 20);
    difficulty.setColor(color);
    for(size_t i = 0; i < this->classes.size(); ++i)
        difficulty.addEntry(EnemyDifficultyPolicy::label(this->classes[i]), static_cast<int>(i));
    const auto selected = std::find(this->classes.begin(), this->classes.end(), current);
    difficulty.setSelectedItem(selected != this->classes.end() ? static_cast<int>(selected - this->classes.begin()) : 0);
    difficulty.setNumVisibleEntries(static_cast<int>(this->classes.size()));
    mainVBox.addWidget(&difficulty, 28);
    mainVBox.addWidget(VSpacer::create(10));

    note.setText(_("The game starts again from the beginning.\nYour partner and allied AI stay as they are."));
    note.setTextColor(MenuTheme::muted, COLOR_TRANSPARENT);
    note.setAlignment(Alignment_Left);
    mainVBox.addWidget(&note, 40);
    mainVBox.addWidget(VSpacer::create(10));

    restartButton.setText(_("Restart"));
    restartButton.setTextColor(color);
    restartButton.setOnClick([this]() { onRestart(); });
    cancelButton.setText(_("Cancel"));
    cancelButton.setTextColor(color);
    cancelButton.setOnClick([this]() { onCancel(); });
    buttons.addWidget(&restartButton);
    buttons.addWidget(HSpacer::create(12));
    buttons.addWidget(&cancelButton);
    mainVBox.addWidget(&buttons, 32);
    mainVBox.addWidget(VSpacer::create(12));
}

RestartDifficultyWindow* RestartDifficultyWindow::create(const std::vector<std::string>& classes, const std::string& current, int color) {
    auto* dialog = new RestartDifficultyWindow(classes, current, color);
    dialog->pAllocated = true;
    return dialog;
}

bool RestartDifficultyWindow::handleKeyPress(SDL_KeyboardEvent& key) {
    if(key.keysym.sym == SDLK_ESCAPE) {
        onCancel();
        return true;
    }
    return Window::handleKeyPress(key);
}

void RestartDifficultyWindow::onRestart() {
    const int index = difficulty.getSelectedEntryIntData();
    if(index < 0 || index >= static_cast<int>(classes.size())) return;
    chosen = classes[index];
    if(auto* parent = dynamic_cast<Window*>(getParent())) parent->closeChildWindow();
}

void RestartDifficultyWindow::onCancel() {
    chosen.clear();
    if(auto* parent = dynamic_cast<Window*>(getParent())) parent->closeChildWindow();
}
