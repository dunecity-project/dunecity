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

#ifndef PLAYERFACTORY_H
#define PLAYERFACTORY_H

#include <players/Player.h>
#include <players/AIRole.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <utility>

class PlayerFactory {
public:
    class PlayerData {
    public:
        /**
            \param family       the AI the menus offer ("QuantBot"); empty for the human player
            \param difficulty   the difficulty label within the family; empty if it has none
            \param role         what the AI does for its house (docs/ai-roles.md)
        */
        PlayerData( const std::string& playerclass, const std::string& name,
                    const std::function<std::unique_ptr<Player> (House*, const std::string&)>& pCreate,
                    const std::function<std::unique_ptr<Player> (InputStream&, House*)>& pLoad,
                    const std::string& family = "", const std::string& difficulty = "", AIRole role = AIRole::Both)
         : playerclass(playerclass), name(name), family(family), difficulty(difficulty), role(role), pCreate(pCreate), pLoad(pLoad) {
        }

        const std::string& getPlayerClass() const {
            return playerclass;
        }

        const std::string& getName() const {
            return name;
        }

        const std::string& getFamily() const { return family; }
        const std::string& getDifficulty() const { return difficulty; }
        AIRole getRole() const { return role; }

        std::unique_ptr<Player> create(House* associatedHouse, const std::string& playername) const {
            auto pPlayer = pCreate(associatedHouse, playername);
            pPlayer->setPlayerclass(playerclass);
            return pPlayer;
        }

        std::unique_ptr<Player> load(InputStream& stream, House* associatedHouse) const {
            auto pPlayer = pLoad(stream, associatedHouse);
            pPlayer->setPlayerclass(playerclass);
            return pPlayer;
        }

    private:
        std::string playerclass;
        std::string name;
        std::string family;
        std::string difficulty;
        AIRole role;
        std::function<std::unique_ptr<Player>(House*, const std::string&)> pCreate;
        std::function<std::unique_ptr<Player>(InputStream&, House*)> pLoad;
    };

    static const std::vector<PlayerData>& getList() {
        if(playerDataList.empty()) {
            registerAllPlayers();
        }
        return playerDataList;
    }

    static const PlayerData* getByIndex(int index) {
        if(playerDataList.empty()) {
            registerAllPlayers();
        }

        if(index < 0 || index >= static_cast<int>(playerDataList.size())) {
            return nullptr;
        }

        return &playerDataList[index];
    }

    static const PlayerData* getByPlayerClass(const std::string& playerclass) {
        if(playerDataList.empty()) {
            registerAllPlayers();
        }

        for(auto& player_data : playerDataList) {
            if(player_data.getPlayerClass() == playerclass) {
                return &player_data;
            }
        }

        // A retired class (old save, replay or setting) resolves to its replacement. Its entry
        // carries the replacement's class string, so a loaded or created player saves as that.
        for(auto& [retiredClass, player_data] : retiredPlayerDataList) {
            if(retiredClass == playerclass) {
                return &player_data;
            }
        }

        return nullptr;
    }

    static int getIndexByPlayerClass(const std::string& playerclass) {
        if(playerDataList.empty()) {
            registerAllPlayers();
        }

        for(unsigned int i = 0; i < playerDataList.size(); i++) {
            if(playerDataList[i].getPlayerClass() == playerclass) {
                return i;
            }
        }

        return -1;
    }

    /// The AI families in registration order. The human player is not one.
    static std::vector<std::string> getAIFamilies() {
        std::vector<std::string> families;
        for(const auto& data : getList()) {
            if(!data.getFamily().empty()
               && std::find(families.begin(), families.end(), data.getFamily()) == families.end()) {
                families.push_back(data.getFamily());
            }
        }
        return families;
    }

    /// The difficulty labels of one family in registration order; a single "" if it has none.
    static std::vector<std::string> getDifficulties(const std::string& family) {
        std::vector<std::string> difficulties;
        for(const auto& data : getList()) {
            if(data.getFamily() == family
               && std::find(difficulties.begin(), difficulties.end(), data.getDifficulty()) == difficulties.end()) {
                difficulties.push_back(data.getDifficulty());
            }
        }
        return difficulties;
    }

    /// The roles registered for one family and difficulty, Both first.
    static std::vector<AIRole> getRoles(const std::string& family, const std::string& difficulty) {
        std::vector<AIRole> roles;
        for(AIRole role : {AIRole::Both, AIRole::Builder, AIRole::Fighter}) {
            if(find(family, difficulty, role) != nullptr) roles.push_back(role);
        }
        return roles;
    }

    static const PlayerData* find(const std::string& family, const std::string& difficulty, AIRole role) {
        for(const auto& data : getList()) {
            if(!family.empty() && data.getFamily() == family && data.getDifficulty() == difficulty && data.getRole() == role) {
                return &data;
            }
        }
        return nullptr;
    }

private:

    static void registerAllPlayers();

    static std::vector<PlayerData> playerDataList;
    /// Retired class string -> its replacement, consulted only by getByPlayerClass. Never listed.
    static std::vector<std::pair<std::string, PlayerData>> retiredPlayerDataList;
};

#endif //PLAYERFACTORY_H
