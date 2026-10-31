// Diagnostic-only observations for tornie-gameplay-matrix.py; never linked into the game.
#include <FileClasses/FileManager.h>
#include <Map.h>
#include <Tile.h>
#include <structures/BuilderBase.h>
#include <units/HarvesterHelpers.h>
#include <array>

inline std::string tornieColouredSpiceFixture(const std::string& map) {
    if(!ModManager::instance().isTornieFamilyContentActive())
        throw std::runtime_error("Coloured-spice fixture requires a Tornie-family mod");
    std::istringstream input(map);
    std::ostringstream output;
    std::string line;
    bool terrain = false;
    while(std::getline(input, line)) {
        if(!line.empty() && line.front() == '[') terrain = line == "[MAP]";
        if(terrain && line.size() > 4 && line[3] == '='
           && line[0] >= '0' && line[0] <= '9' && line[1] >= '0' && line[1] <= '9'
           && line[2] >= '0' && line[2] <= '9') {
            const int row = std::stoi(line.substr(0, 3));
            for(std::size_t x = 4; x < line.size(); ++x) {
                const bool thick = line[x] == '+';
                if(!thick && line[x] != '~') continue;
                const bool white = (row / 6 + static_cast<int>(x) / 6) % 2 != 0;
                line[x] = white ? (thick ? 'W' : 'w') : (thick ? 'L' : 'l');
            }
        }
        output << line << '\n';
    }
    return output.str();
}

class TornieCampaignAudit {
public:
    explicit TornieCampaignAudit(Game& game) : enabled(std::getenv("BALANCE_TORNIE_AUDIT") != nullptr) {
        if(!enabled) return;
        const auto& init = game.getGameInitSettings();
        SDL_Log("TORNIE_AUDIT_SETUP: mod=%s house=%d mission=%d filename=%s cycle=%u",
                ModManager::instance().getActiveModName().c_str(), init.getHouseID(), init.getMission(),
                init.getFilename().c_str(), game.getGameCycleCount());
        if(isCampaignGameType(init.getGameType()) && ModManager::instance().isTornieFamilyContentActive()) {
            INIFile mission(pFileManager->openCampaignFile(init.getFilename()).get());
            const std::string region = std::string("REGION") + getHouseScenarioLetter(init.getHouseID()) + ".INI";
            INIFile regions(pFileManager->openCampaignFile(region).get());
            std::string authoredOpponents;
            for(int h = 0; h < NUM_PLAYER_HOUSES; ++h) {
                if(h == init.getHouseID() || !mission.hasSection(getHouseNameByNumber(static_cast<HOUSETYPE>(h)))) continue;
                if(!authoredOpponents.empty()) authoredOpponents += ',';
                authoredOpponents += std::to_string(h);
            }
            SDL_Log("TORNIE_AUDIT_AUTHORED: opponents=%s", authoredOpponents.c_str());
            SDL_Log("TORNIE_AUDIT_MISSION: recipe=%d declared_opponents=%d opening=%s region=%s primary=%s support=%s late=%s final=%s",
                    mission.getIntValue("BASIC", "CampaignRecipe", 0), mission.getIntValue("BASIC", "OpponentCount", 0),
                    mission.getStringValue("BASIC", "OpeningStyle", "normal").c_str(), region.c_str(),
                    mission.getStringValue("BASIC", "PrimaryOpponent", "none").c_str(),
                    mission.getStringValue("BASIC", "SupportOpponent", "none").c_str(),
                    mission.getStringValue("BASIC", "LateOpponent", "none").c_str(),
                    mission.getStringValue("BASIC", "FinalOpponent", "none").c_str());
        }
        int opponents = 0;
        for(int h = 0; h < NUM_PLAYER_HOUSES; ++h) {
            const House* house = game.getHouse(h);
            if(!house) continue;
            started[h] = house->getNumStructures() + house->getNumUnits() > 0;
            initialUnits[h] = house->getNumBuiltUnits();
            initialStructures[h] = house->getNumBuiltStructures();
            for(const auto* structure : structureList) {
                if(structure->getOwner() != house || !structure->isABuilder()
                   || structure->getItemID() == Structure_ConstructionYard || structure->getItemID() == Structure_StarPort) continue;
                ++initialFactories[h];
                if(!house->hasPower()) continue;
                const auto* factory = static_cast<const BuilderBase*>(structure);
                for(const auto& option : factory->getBuildList()) {
                    const Uint32 item = option.itemID;
                    if(!isUnit(item) || item == Unit_MCV || isHarvesterLikeUnit(item)
                       || (item == Unit_Carryall && house->hasCarryalls())
                       || !factory->isAvailableToBuild(item) || house->isUnitLimitReached(item)) continue;
                    const int price = game.objectData.data[item][factory->getTechnologyHouseID()].price;
                    if(price > 0 && (house->getCredits() >= price || house->hasRefinery())) {
                        ++initialOperatingFactories[h];
                        break;
                    }
                }
            }
            if(started[h] && h != init.getHouseID()) ++opponents;
            SDL_Log("TORNIE_AUDIT_START: house=%d team=%d units=%d structures=%d credits=%d active=%d factories=%d operating_factories=%d power=%d/%d",
                    h, house->getTeamID(), house->getNumUnits(), house->getNumStructures(), house->getCredits(), int(started[h]),
                    initialFactories[h], initialOperatingFactories[h], house->getProducedPower(), house->getPowerRequirement());
            for(int item = ItemID_Invalid + 1; item < Num_ItemID; ++item) {
                built[h][item] = house->getNumBuiltItems(item);
                damage[h][item] = house->getNumItemDamageInflicted(item);
                if(house->getNumItems(item))
                    SDL_Log("TORNIE_AUDIT_INITIAL_ITEM: house=%d item=%d count=%d", h, item, house->getNumItems(item));
            }
        }
        int lilac = 0, white = 0, green = 0, red = 0;
        for(int y = 0; y < currentGameMap->getSizeY(); ++y)
            for(int x = 0; x < currentGameMap->getSizeX(); ++x) {
                const Tile* tile = currentGameMap->getTile(x, y);
                lilac += tile->isPaleLilacSpice(); white += tile->isWhiteSpice();
                green += tile->isGreenSpice(); red += tile->isRedSpice();
            }
        SDL_Log("TORNIE_AUDIT_MAP: opponents=%d lilac=%d white=%d green=%d red=%d", opponents, lilac, white, green, red);
    }

    void finish(Game& game) const {
        if(!enabled) return;
        for(int h = 0; h < NUM_PLAYER_HOUSES; ++h) {
            const House* house = game.getHouse(h);
            if(!house) continue;
            SDL_Log("TORNIE_AUDIT_HOUSE: house=%d team=%d started=%d alive=%d units=%d structures=%d built_units=%d built_structures=%d kills=%d losses=%d initial_factories=%d initial_operating_factories=%d",
                    h, house->getTeamID(), int(started[h]), int(house->isAlive()), house->getNumUnits(), house->getNumStructures(),
                    house->getNumBuiltUnits() - initialUnits[h], house->getNumBuiltStructures() - initialStructures[h],
                    house->getKillValue(), house->getLossValue(), initialFactories[h], initialOperatingFactories[h]);
            for(int item = ItemID_Invalid + 1; item < Num_ItemID; ++item) {
                const int made = house->getNumBuiltItems(item) - built[h][item];
                const Sint32 inflicted = house->getNumItemDamageInflicted(item) - damage[h][item];
                if(made || inflicted)
                    SDL_Log("TORNIE_AUDIT_ITEM: house=%d item=%d built=%d damage=%d alive=%d", h, item, made, inflicted, house->getNumItems(item));
            }
        }
    }

private:
    bool enabled;
    std::array<bool, NUM_PLAYER_HOUSES> started{};
    std::array<int, NUM_PLAYER_HOUSES> initialUnits{}, initialStructures{}, initialFactories{}, initialOperatingFactories{};
    std::array<std::array<int, Num_ItemID>, NUM_PLAYER_HOUSES> built{};
    std::array<std::array<Sint32, Num_ItemID>, NUM_PLAYER_HOUSES> damage{};
};
