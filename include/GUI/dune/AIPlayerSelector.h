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

#ifndef AIPLAYERSELECTOR_H
#define AIPLAYERSELECTOR_H

#include <FileClasses/TextManager.h>
#include <GUI/DropDownBox.h>
#include <players/PlayerFactory.h>
#include <globals.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

/**
    Three drop-down boxes that choose one AI player class: AI, Difficulty and Role
    (docs/ai-roles.md). The menu owning the selector positions the three boxes itself.

    The AI box can start with special entries ("None", "Open", "Human", ...), identified by a
    caller-chosen code. While one is selected, Difficulty and Role are disabled and
    getPlayerClass() is empty. Difficulty and Role are disabled when the chosen AI offers only
    one value, and Role can be hidden for menus that only pick opponents.
*/
class AIPlayerSelector {
public:
    AIPlayerSelector() {
        // Refilling a box selects in it; only the user's own choices reach the callbacks.
        aiBox.setOnSelectionChange([this](bool interactive) { if(!updating) onAIChanged(interactive); });
        difficultyBox.setOnSelectionChange([this](bool interactive) { if(!updating) onDifficultyChanged(interactive); });
        roleBox.setOnSelectionChange([this](bool interactive) { if(!updating) notify(interactive); });
    }

    AIPlayerSelector(const AIPlayerSelector&) = delete;
    AIPlayerSelector& operator=(const AIPlayerSelector&) = delete;

    /// Adds a leading special entry to the AI box. Call before setFamilies().
    void addSpecial(const std::string& label, int code) {
        specials.push_back({label, code});
    }

    /// Fills the AI box: the special entries, then these AI families (all registered ones if empty).
    void setFamilies(const std::vector<std::string>& wanted = {}) {
        families = wanted.empty() ? PlayerFactory::getAIFamilies() : wanted;
        Updating guard(updating);
        aiBox.clearAllEntries();
        int index = 0;
        for(const auto& special : specials) aiBox.addEntry(special.first, index++);
        for(const auto& family : families) aiBox.addEntry(_(family), index++);
        aiBox.setSelectedItem(0);
        refill(std::string(), AIRole::Both);
    }

    /// Menus that only choose opponents hide the role box; every AI then plays Both.
    void setShowRole(bool show) {
        Updating guard(updating);
        showRole = show;
        roleBox.setVisible(show);
        refill(difficultyBox.getSelectedEntry(), selectedRole());
    }

    void setOnChange(std::function<void(bool interactive)> callback) { onChange = std::move(callback); }

    void setEnabled(bool enabled) {
        this->enabled = enabled;
        aiBox.setEnabled(enabled);
        Updating guard(updating);
        refill(currentDifficulty(), selectedRole());
    }

    void selectSpecial(int code) {
        for(size_t i = 0; i < specials.size(); i++) {
            if(specials[i].second == code) {
                Updating guard(updating);
                aiBox.setSelectedItem(static_cast<int>(i));
                refill(std::string(), AIRole::Both);
                return;
            }
        }
    }

    /// Selects the AI, difficulty and role of a registered class. Returns false if it is not offered.
    bool selectPlayerClass(const std::string& playerclass) {
        const auto* data = PlayerFactory::getByPlayerClass(playerclass);
        if(data == nullptr) return false;
        const auto it = std::find(families.begin(), families.end(), data->getFamily());
        if(it == families.end()) return false;
        Updating guard(updating);
        aiBox.setSelectedItem(static_cast<int>(specials.size() + (it - families.begin())));
        refill(data->getDifficulty(), data->getRole());
        return getPlayerClass() == playerclass;
    }

    bool isSpecialSelected() const {
        const int index = aiBox.getSelectedIndex();
        return index >= 0 && index < static_cast<int>(specials.size());
    }

    /// The code of the selected special entry, or fallback if an AI is selected.
    int getSpecial(int fallback = -1) const {
        return isSpecialSelected() ? specials[aiBox.getSelectedIndex()].second : fallback;
    }

    /// The selected AI's player class; empty while a special entry is selected.
    std::string getPlayerClass() const {
        const auto* data = selectedData();
        return data != nullptr ? data->getPlayerClass() : std::string();
    }

    /// The factory index of the selected class, or -1.
    int getPlayerClassIndex() const {
        const std::string playerclass = getPlayerClass();
        return playerclass.empty() ? -1 : PlayerFactory::getIndexByPlayerClass(playerclass);
    }

    /// The role as the Role box names it, translated; partner AIs carry it in their player name.
    static std::string roleLabel(AIRole role) {
        switch(role) {
            case AIRole::Builder: return _("Builder");
            case AIRole::Fighter: return _("Fighter");
            case AIRole::Both:
            default:              return _("Builder + Fighter");
        }
    }

    DropDownBox aiBox;
    DropDownBox difficultyBox;
    DropDownBox roleBox;

private:
    struct Updating {
        explicit Updating(bool& flag) : flag(flag), previous(flag) { flag = true; }
        ~Updating() { flag = previous; }
        bool& flag;
        bool previous;
    };

    std::string selectedFamily() const {
        const int index = aiBox.getSelectedIndex() - static_cast<int>(specials.size());
        return index >= 0 && index < static_cast<int>(families.size()) ? families[index] : std::string();
    }

    std::string currentDifficulty() const {
        const int index = difficultyBox.getSelectedIndex();
        return index >= 0 && index < static_cast<int>(difficulties.size()) ? difficulties[index] : std::string();
    }

    AIRole selectedRole() const {
        const int index = roleBox.getSelectedIndex();
        return index >= 0 && index < static_cast<int>(roles.size()) ? roles[index] : AIRole::Both;
    }

    const PlayerFactory::PlayerData* selectedData() const {
        const std::string family = selectedFamily();
        if(family.empty()) return nullptr;
        return PlayerFactory::find(family, currentDifficulty(), showRole ? selectedRole() : AIRole::Both);
    }

    /// Rebuilds the difficulty and role boxes for the selected AI, keeping the wanted values if offered.
    void refill(const std::string& wantedDifficulty, AIRole wantedRole) {
        const std::string family = selectedFamily();
        difficulties = family.empty() ? std::vector<std::string>() : PlayerFactory::getDifficulties(family);
        difficultyBox.clearAllEntries();
        for(size_t i = 0; i < difficulties.size(); i++)
            difficultyBox.addEntry(difficulties[i].empty() ? std::string("-") : _(difficulties[i]), static_cast<int>(i));
        if(difficulties.empty()) difficultyBox.addEntry("-", 0);
        const auto difficulty = std::find(difficulties.begin(), difficulties.end(), wantedDifficulty);
        difficultyBox.setSelectedItem(difficulty != difficulties.end() ? static_cast<int>(difficulty - difficulties.begin()) : 0);
        difficultyBox.setEnabled(enabled && difficulties.size() > 1);
        refillRoles(wantedRole);
    }

    void refillRoles(AIRole wantedRole) {
        const std::string family = selectedFamily();
        roles = family.empty() || !showRole ? std::vector<AIRole>() : PlayerFactory::getRoles(family, currentDifficulty());
        roleBox.clearAllEntries();
        for(size_t i = 0; i < roles.size(); i++) roleBox.addEntry(roleLabel(roles[i]), static_cast<int>(i));
        if(roles.empty()) roleBox.addEntry("-", 0);
        const auto role = std::find(roles.begin(), roles.end(), wantedRole);
        roleBox.setSelectedItem(role != roles.end() ? static_cast<int>(role - roles.begin()) : 0);
        roleBox.setEnabled(enabled && roles.size() > 1);
    }

    void onAIChanged(bool interactive) {
        {
            Updating guard(updating);
            refill(currentDifficulty(), selectedRole());
        }
        notify(interactive);
    }

    void onDifficultyChanged(bool interactive) {
        {
            Updating guard(updating);
            refillRoles(selectedRole());
        }
        notify(interactive);
    }

    void notify(bool interactive) {
        if(onChange) onChange(interactive);
    }

    std::vector<std::pair<std::string, int>> specials;
    std::vector<std::string> families;
    std::vector<std::string> difficulties;
    std::vector<AIRole> roles;
    bool showRole = true;
    bool enabled = true;
    bool updating = false;
    std::function<void(bool)> onChange;
};

#endif // AIPLAYERSELECTOR_H
