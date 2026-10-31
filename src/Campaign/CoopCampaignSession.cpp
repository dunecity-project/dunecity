#include <Campaign/CoopCampaignSession.h>
#include <DataTypes.h>

#include <algorithm>
#include <charconv>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace coop {
namespace {
using Section = std::map<std::string, std::string>;
constexpr std::size_t MaxDataSize = 8 * 1024 * 1024;

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if(first == std::string::npos) return "";
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

std::string lower(std::string value) {
    for(char& c : value) if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}

std::optional<Section> readSection(const std::string& data, const std::string& name) {
    if(data.size() > MaxDataSize) throw std::invalid_argument("Coop data is too large");
    std::istringstream input(data);
    std::optional<Section> result;
    std::string line;
    bool active = false;
    while(std::getline(input, line)) {
        line = trim(line);
        if(line.empty() || line[0] == ';' || line[0] == '#') continue;
        if(line.front() == '[' && line.back() == ']') {
            active = lower(trim(line.substr(1, line.size() - 2))) == lower(name);
            if(active) {
                if(result) throw std::invalid_argument("Duplicate coop metadata section");
                result.emplace();
            }
        } else if(active) {
            const auto separator = line.find('=');
            if(separator == std::string::npos) throw std::invalid_argument("Invalid coop metadata line");
            const auto key = lower(trim(line.substr(0, separator)));
            if(key.empty() || !result->emplace(key, trim(line.substr(separator + 1))).second)
                throw std::invalid_argument("Duplicate or empty coop metadata key");
        }
    }
    return result;
}

const std::string& require(const Section& section, const std::string& key) {
    const auto found = section.find(lower(key));
    if(found == section.end()) throw std::invalid_argument("Missing coop field: " + key);
    return found->second;
}

template<typename Integer> Integer number(const std::string& text) {
    Integer value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if(result.ec != std::errc() || result.ptr != text.data() + text.size())
        throw std::invalid_argument("Invalid coop number");
    return value;
}

bool boolean(const std::string& text) {
    if(text == "1" || lower(text) == "true") return true;
    if(text == "0" || lower(text) == "false") return false;
    throw std::invalid_argument("Invalid coop boolean");
}

int participantCount(const Section& section, bool expanded = false) {
    const auto field = section.find("participantcount");
    const int count = field == section.end() ? SlotCount : number<int>(field->second);
    if(count < SlotCount || count > (expanded ? MaxSlotCount : MaxEnemyCount+2))
        throw std::invalid_argument("Invalid coop participant count");
    return count;
}

std::string hex(const std::string& value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for(const unsigned char c : value) {
        result.push_back(digits[c >> 4]);
        result.push_back(digits[c & 15]);
    }
    return result;
}

std::string unhex(const std::string& value) {
    if(value.size() % 2 != 0) throw std::invalid_argument("Invalid coop encoded text");
    const auto digit = [](char c) -> unsigned char {
        if(c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
        if(c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
        if(c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
        throw std::invalid_argument("Invalid coop encoded text");
    };
    std::string result;
    result.reserve(value.size() / 2);
    for(std::size_t i = 0; i < value.size(); i += 2)
        result.push_back(static_cast<char>((digit(value[i]) << 4) | digit(value[i + 1])));
    return result;
}

bool safeIdentifier(const std::string& value) {
    return !value.empty() && value.size() <= 64
        && std::all_of(value.begin(), value.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
}

bool validPlayerName(const std::string& value) {
    return !value.empty() && value.size() <= 64
        && std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}

void validate(const Context& context) {
    const bool tornieFour=context.followsOriginalCampaign()
        && (context.modName=="Tornie" || context.modName=="TornieLite") && context.enemyPresent.size()==4;
    if(context.campaign != "common" || !safeIdentifier(context.sessionId) || !safeIdentifier(context.modName))
        throw std::invalid_argument("Invalid coop campaign identity");
    if(context.stage < 1 || context.stage > StageCount+1
       || context.completedMask != ((1u << (context.stage-1))-1u))
        throw std::invalid_argument("Invalid coop campaign progression");
    std::set<int> roster;
    for(const int faction : context.roster)
        if(faction<0 || faction>=NUM_PLAYER_HOUSES || !roster.insert(faction).second)
            throw std::invalid_argument("Invalid coop faction roster");
    if((context.alliedCapacity!=2 && context.alliedCapacity!=4)
       || context.slots.size()!=context.enemyPresent.size()+context.alliedCapacity
       || context.enemyPresent.size()<3 || context.enemyPresent.size()>MaxEnemyCount
       || (context.modName!="vanilla" && context.enemyPresent.size()!=3 && !tornieFour)
       || (context.archon && context.alliedCapacity!=4))
        throw std::invalid_argument("Invalid coop participant count");
    if(roster.size()<5 || (context.alliedCapacity==2 && roster.size()<context.slots.size()))
        throw std::invalid_argument("Not enough coop factions");
    if(context.mapLayout!="legacy" && context.mapLayout!="original")
        throw std::invalid_argument("Invalid coop map layout");
    if(context.followsOriginalCampaign()) {
        if(!roster.count(context.sourceFaction)
           || std::none_of(context.enemyPresent.begin(),context.enemyPresent.end(),[](bool p){return p;}))
            throw std::invalid_argument("Invalid coop campaign source");
    } else if(context.sourceFaction!=-1
              || std::any_of(context.enemyPresent.begin(),context.enemyPresent.end(),[](bool p){return !p;}))
        throw std::invalid_argument("Invalid legacy coop campaign source");
    std::set<int> houses,enemies;
    std::set<std::string> humanNames;
    for(std::size_t i=0;i<context.slots.size();++i) {
        const auto& slot=context.slots[i];
        if(slot.house<0 || slot.house>=NUM_PLAYER_HOUSES || !roster.count(slot.faction)
           || (context.alliedCapacity==2 && !roster.count(slot.house))
           || !houses.insert(slot.house).second || slot.color < -1 || slot.color>=NUM_HOUSE_COLOR_SLOTS
           || slot.allied!=context.isAlliedSlot(i))
            throw std::invalid_argument("Invalid coop slot identity");
        if(slot.allied) {
            if(!validPlayerName(slot.playerName)
               || (slot.playerClass!="HumanPlayer" && !isCoopAIClass(slot.playerClass)
                   && !(context.alliedCapacity==4 && slot.playerClass=="Closed"))
               || (i==0 && slot.playerClass!="HumanPlayer"))
                throw std::invalid_argument("Invalid coop allied player");
            const auto names=slot.humanNames();
            if((!context.archon && names.size()>1)
               || (!slot.controllers.empty() && (slot.playerClass!="HumanPlayer" || slot.playerName!=slot.controllers.front())))
                throw std::invalid_argument("Invalid Archon controllers");
            for(const auto& name:names)
                if(!validPlayerName(name) || !humanNames.insert(name).second)
                    throw std::invalid_argument("Coop players must have distinct network names");
        } else {
            if(!isCoopAIClass(slot.playerClass) || !slot.controllers.empty()
               || (!enemies.insert(slot.faction).second && context.alliedCapacity==2))
                throw std::invalid_argument("Invalid coop opponent");
            if(!context.followsOriginalCampaign())
                for(const int ally:context.alliedSlots())
                    if(context.slots[ally].active() && slot.faction==context.slots[ally].faction)
                        throw std::invalid_argument("Coop opponent matches an ally");
        }
    }
    if(humanNames.empty() || humanNames.size()>static_cast<std::size_t>(context.archon ? 8 : context.alliedCapacity))
        throw std::invalid_argument("Invalid coop human player count");
    if(context.missionVariant<0 || context.missionVariant>=missionVariantCount(context.stage)
       || (!context.randomVariants && context.missionVariant!=0)
       || (context.randomVariants && (context.alliedCapacity!=4 || !context.followsOriginalCampaign())))
        throw std::invalid_argument("Invalid coop mission variant");
    if(context.modName=="vanilla" && context.chaosEligible)
        throw std::invalid_argument("Vanilla does not support Chaos campaign progression");
}
void bindEnemySlots(Context& context, const std::vector<int>& factions,
    const std::vector<bool>& present) {
    if(factions.size()<3 || factions.size()>MaxEnemyCount || factions.size()!=present.size())
        throw std::invalid_argument("Invalid coop opponent count");
    std::vector<Slot> extra;
    for(int i=2;i<context.alliedCapacity;++i) extra.push_back(context.slots[context.enemyPresent.size()+i]);
    context.slots.resize(factions.size()+context.alliedCapacity);
    for(std::size_t i=0;i<extra.size();++i) context.slots[factions.size()+2+i]=extra[i];
    context.enemyPresent=present;
    std::set<int> usedHouses;
    const auto allocate=[&](Slot& slot) {
        slot.house=slot.faction;
        if(usedHouses.count(slot.house)) {
            if(context.alliedCapacity==2) {
                const auto free=std::find_if(context.roster.begin(),context.roster.end(),[&](int h){return !usedHouses.count(h);});
                if(free==context.roster.end()) throw std::logic_error("No free coop runtime house");
                slot.house=*free;
            } else {
                slot.house=0;
                while(usedHouses.count(slot.house)) ++slot.house;
                if(slot.house>=NUM_PLAYER_HOUSES) throw std::logic_error("No free coop runtime house");
            }
        }
        usedHouses.insert(slot.house);
    };
    for(const int ally:context.alliedSlots()) allocate(context.slots[ally]);
    for(std::size_t i=0;i<factions.size();++i) {
        auto& slot=context.slots[i+2];
        slot.faction=factions[i];
        allocate(slot);
        slot.allied=false;
        slot.controllers.clear();
        slot.color=-1;
        if(slot.playerClass.empty() || slot.playerClass=="Closed" || slot.playerClass=="HumanPlayer") slot.playerClass="CampaignAIPlayer";
        slot.playerName="Opponent "+std::to_string(i+1);
    }
    context.enemyPresent = present;
    validate(context);
}

std::string serialize(const Context& context) {
    validate(context);
    std::ostringstream output;
    output << "[COOP]\nSchema=" << (context.alliedCapacity==4 ? 2 : 1) << "\nCampaign=" << context.campaign
        << "\nSession=" << context.sessionId << "\nMod=" << context.modName
        << "\nStage=" << context.stage << "\nCompletedMask=" << context.completedMask
        << "\nSeed=" << context.seed << "\nChaosEligible=" << (context.chaosEligible ? 1 : 0)
        << "\nAlliedControl=" << (context.alliedControl ? 1 : 0)
        << "\nAlliedBuilding=" << (context.alliedBuilding ? 1 : 0)
        << "\nEasyMode=" << (context.easyMode ? 1 : 0)
        << "\nExtraEnemyForces=" << (context.extraEnemyForces ? 1 : 0) << "\nRoster=";
    for(std::size_t i = 0; i < context.roster.size(); ++i) {
        if(i) output << ',';
        output << context.roster[i];
    }
    output << '\n';
    if(context.alliedCapacity==4) output << "AlliedCapacity=4\nArchon=" << context.archon << '\n';
    if(context.extraHardEnemyForces) output << "ExtraHardEnemyForces=1\n";
    if(context.randomVariants) output << "RandomVariants=1\nMissionVariant=" << context.missionVariant << '\n';
    if(context.alliedCapacity==4 && !context.followsOriginalCampaign()) output << "ParticipantCount=" << context.slots.size() << '\n';
    if(context.followsOriginalCampaign()) {
        output << "MapLayout=original\nSourceFaction=" << context.sourceFaction
            << "\nParticipantCount=" << context.slots.size() << '\n';
        for(std::size_t i = 0; i < context.enemyPresent.size(); ++i)
            output << "EnemyPresent" << i + 1 << '=' << (context.enemyPresent[i] ? 1 : 0) << '\n';
    }
    for(std::size_t i = 0; i < context.slots.size(); ++i) {
        const auto& slot = context.slots[i];
        const auto key = "Slot" + std::to_string(i + 1);
        output << key << "House=" << slot.house << '\n'
            << key << "Faction=" << slot.faction << '\n'
            << key << "Color=" << slot.color << '\n'
            << key << "NameHex=" << hex(slot.playerName) << '\n'
            << key << "Class=" << slot.playerClass << '\n';
        if(!slot.controllers.empty()) {
            output << key << "Controllers=";
            for(std::size_t n=0;n<slot.controllers.size();++n) output << (n ? "," : "") << hex(slot.controllers[n]);
            output << '\n';
        }
    }
    return output.str();
}

std::uint32_t nextRandom(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

std::string withoutMetadata(const std::string& data) {
    std::istringstream input(data);
    std::ostringstream output;
    std::string line;
    bool skip = false;
    while(std::getline(input, line)) {
        const auto cleaned = trim(line);
        if(!cleaned.empty() && cleaned.front() == '[' && cleaned.back() == ']') {
            const auto section = lower(trim(cleaned.substr(1, cleaned.size() - 2)));
            skip = section == "coop" || section == "progress";
        }
        if(!skip) output << line << '\n';
    }
    return output.str();
}

std::string materializeAllies(const std::string& data,const Context& context) {
    if(context.alliedCapacity==2) return data;
    using Entries=std::vector<std::pair<std::string,std::string>>;
    std::map<std::string,Entries> additions;
    std::set<std::string> closed;
    std::string rocks;
    const auto allies=context.alliedSlots();
    for(int ordinal=0;ordinal<context.alliedCapacity;++ordinal) {
        const int slot=allies[ordinal];
        const auto player="player"+std::to_string(slot+1);
        if(!context.slots[slot].active()) { closed.insert(player);continue; }
        const auto suffix=std::to_string(ordinal+1);
        if(const auto terrain=readSection(data,"COOP_EXTRA_TERRAIN"+suffix)) {
            const auto rock=terrain->find("rock");
            if(rock!=terrain->end() && !rock->second.empty()) rocks+=(rocks.empty() ? "" : ",")+rock->second;
        }
        if(ordinal<2) continue;
        for(const std::string kind:{"PLAYER","UNITS","STRUCTURES"}) {
            const auto source=readSection(data,"COOP_EXTRA_"+kind+suffix);
            if(!source) throw std::invalid_argument("Missing optional co-op start");
            const auto destination=kind=="PLAYER" ? player : lower(kind);
            for(const auto& sourceEntry:*source) {
                std::pair<std::string,std::string> entry(sourceEntry.first,sourceEntry.second);
                if(kind!="PLAYER") {
                    for(char& c:entry.first) if(c>='a' && c<='z') c-=32;
                    const auto comma=entry.second.find(',');
                    if(comma==std::string::npos || lower(trim(entry.second.substr(0,comma)))!="player2")
                        throw std::invalid_argument("Invalid optional co-op order");
                    entry.second="Player"+std::to_string(slot+1)+entry.second.substr(comma);
                }
                additions[destination].push_back(entry);
            }
        }

    }
    if(!rocks.empty()) additions["map"].emplace_back("CoopStartRock",rocks);
    // Drop every ordinary order and player section for closed allied houses.
    // Auxiliary source starts never enter an executable map, active or closed.
    std::istringstream input(data);
    std::ostringstream output;
    std::set<std::string> emitted;
    std::string line,section;
    bool skip=false;
    while(std::getline(input,line)) {
        const auto cleaned=trim(line);
        if(!cleaned.empty() && cleaned.front()=='[' && cleaned.back()==']') {
            section=lower(trim(cleaned.substr(1,cleaned.size()-2)));
            skip=closed.count(section) || section.rfind("coop_extra_",0)==0;
            if(!skip) {
                output << line << '\n';
                for(const auto& entry:additions[section]) output << entry.first << '=' << entry.second << '\n';
                emitted.insert(section);
            }
            continue;
        }
        if(skip) continue;
        if(section=="units" || section=="structures" || section=="reinforcements" || section=="teams") {
            const auto equals=cleaned.find('='),comma=cleaned.find(',',equals==std::string::npos ? 0 : equals+1);
            if(equals!=std::string::npos && comma!=std::string::npos
               && closed.count(lower(trim(cleaned.substr(equals+1,comma-equals-1))))) continue;
        }
        output << line << '\n';
    }
    for(const auto& section:additions) if(!emitted.count(section.first) && !section.second.empty()) {
        output << "\n[" << section.first << "]\n";
        for(const auto& entry:section.second) output << entry.first << '=' << entry.second << '\n';
    }
    return output.str();
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if(!input) throw std::runtime_error("Cannot open coop progress: " + path.string());
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if(size < 0 || static_cast<std::uint64_t>(size) > MaxDataSize)
        throw std::runtime_error("Invalid coop progress size");
    std::string data(static_cast<std::size_t>(size), '\0');
    input.seekg(0);
    if(!data.empty() && !input.read(&data[0], static_cast<std::streamsize>(data.size())))
        throw std::runtime_error("Cannot read coop progress");
    return data;
}
} // namespace

bool isCoopAIClass(const std::string& playerClass) {
    if(playerClass == "CampaignAIPlayer" || playerClass == "SmartBot") return true;
    for(const auto* family : {"qBot", "qBotSupport", "qBotFighter", "mentat", "mentatSupport"})
        for(const auto* difficulty : {"Defend", "Easy", "Medium", "Hard", "Brutal"})
            if(playerClass == std::string(family) + difficulty) return true;
    return playerClass == "AIPlayerEasy" || playerClass == "AIPlayerMedium" || playerClass == "AIPlayerHard";
}

bool canControlHouse(const Context& context, int playerHouse, int objectHouse) {
    if(playerHouse == objectHouse) return true;
    if(!context.alliedControl) return false;
    const auto allied = [&](int house) {
        for(const int i:context.alliedSlots())
            if(context.slots[i].active() && house==context.slots[i].house) return true;
        return false;
    };
    return allied(playerHouse) && allied(objectHouse);
}

bool canBuildOnAlliedLand(const Context& context, int builderHouse, int landHouse) {
    if(!context.alliedBuilding) return false;
    bool builderAllied=false, landAllied=false;
    for(const int index:context.alliedSlots()) {
        const auto& slot=context.slots[index];
        if(!slot.active()) continue;
        builderAllied |= slot.house==builderHouse;
        landAllied |= slot.house==landHouse;
    }
    return builderAllied && landAllied;
}

std::optional<Context> readContext(const std::string& mapData) {
    const auto section = readSection(mapData, "COOP");
    if(!section) return std::nullopt;
    const auto schema=require(*section,"Schema");
    if(schema!="1" && schema!="2") throw std::invalid_argument("Unsupported coop schema");
    Context context;
    context.campaign = require(*section, "Campaign");
    context.sessionId = require(*section, "Session");
    context.modName = require(*section, "Mod");
    if(const auto layout = section->find("maplayout"); layout != section->end())
        context.mapLayout = layout->second;
    if(schema=="2") {
        context.alliedCapacity=number<int>(require(*section,"AlliedCapacity"));
        if(context.alliedCapacity!=4) throw std::invalid_argument("Invalid coop allied capacity");
        context.archon=boolean(require(*section,"Archon"));
    }
    const int count=participantCount(*section,schema=="2");
    context.slots.resize(count);
    context.enemyPresent.resize(count-context.alliedCapacity,true);
    if(context.followsOriginalCampaign()) {
        context.sourceFaction = number<int>(require(*section, "SourceFaction"));
        for(std::size_t i = 0; i < context.enemyPresent.size(); ++i) {
            const auto key = "enemypresent" + std::to_string(i + 1);
            if(const auto present = section->find(key); present != section->end())
                context.enemyPresent[i] = boolean(present->second);
        }
    }
    context.stage = number<int>(require(*section, "Stage"));
    context.completedMask = number<std::uint32_t>(require(*section, "CompletedMask"));
    context.seed = number<std::uint32_t>(require(*section, "Seed"));
    context.chaosEligible = boolean(require(*section, "ChaosEligible"));
    if(const auto control = section->find("alliedcontrol"); control != section->end())
        context.alliedControl = boolean(control->second);
    if(const auto field=section->find("alliedbuilding");field!=section->end())
        context.alliedBuilding=boolean(field->second);
    if(const auto field = section->find("easymode"); field != section->end())
        context.easyMode = boolean(field->second);
    if(const auto field = section->find("extraenemyforces"); field != section->end())
        context.extraEnemyForces = boolean(field->second);
    if(const auto field=section->find("extrahardenemyforces");field!=section->end())
        context.extraHardEnemyForces=boolean(field->second);
    if(const auto field=section->find("randomvariants");field!=section->end()) context.randomVariants=boolean(field->second);
    if(const auto field=section->find("missionvariant");field!=section->end()) context.missionVariant=number<int>(field->second);
    std::istringstream roster(require(*section, "Roster"));
    std::string entry;
    while(std::getline(roster, entry, ',')) context.roster.push_back(number<int>(trim(entry)));
    for(std::size_t i = 0; i < context.slots.size(); ++i) {
        auto& slot = context.slots[i];
        const auto key = "Slot" + std::to_string(i + 1);
        slot.house = number<int>(require(*section, key + "House"));
        slot.faction = number<int>(require(*section, key + "Faction"));
        slot.color = number<int>(require(*section, key + "Color"));
        slot.playerName = unhex(require(*section, key + "NameHex"));
        slot.playerClass = require(*section, key + "Class");
        slot.allied=context.isAlliedSlot(i);
        if(const auto field=section->find(lower(key+"Controllers"));field!=section->end()) {
            std::istringstream names(field->second);
            std::string name;
            while(std::getline(names,name,',')) slot.controllers.push_back(unhex(name));
        }
    }
    validate(context);
    return context;
}

int factionForHouse(const Context& context, int runtimeHouse) {
    for(const auto& slot : context.slots) if(slot.house == runtimeHouse) return slot.faction;
    return runtimeHouse;
}

int mapSlotForHouse(const Context& context, int runtimeHouse) {
    for(std::size_t i = 0; i < context.slots.size(); ++i)
        if(context.slots[i].house == runtimeHouse) return static_cast<int>(i) + 1;
    return -1;
}

CoopCampaignSession::CoopCampaignSession(Context context) : context_(std::move(context)) {
    validate(context_);
}

CoopCampaignSession CoopCampaignSession::create(const std::string& sessionId,
    const std::string& modName, const std::vector<int>& roster,
    const std::array<int, 2>& playerFactions, const std::array<std::string, 2>& playerNames,
    std::uint32_t seed, bool chaosEnabled, const std::string& mapLayout) {
    Context context;
    context.sessionId = sessionId;
    context.modName = modName;
    context.mapLayout = mapLayout;
    context.sourceFaction = context.followsOriginalCampaign() ? playerFactions[0] : -1;
    context.roster = roster;
    context.seed = seed;
    context.chaosEligible = chaosEnabled && modName != "vanilla";
    if(roster.size() < SlotCount) throw std::invalid_argument("Not enough coop factions");
    std::set<int> used;
    for(int i = 0; i < 2; ++i) {
        auto& slot = context.slots[i];
        slot.faction = playerFactions[i];
        slot.house = slot.faction;
        slot.playerName = playerNames[i];
        slot.playerClass = "HumanPlayer";
        slot.allied = true;
        if(std::find(roster.begin(), roster.end(), slot.faction) == roster.end())
            throw std::invalid_argument("Selected coop faction is unavailable");
        if(used.count(slot.house)) {
            const auto free = std::find_if(roster.begin(), roster.end(), [&used](int house) { return !used.count(house); });
            if(free == roster.end()) throw std::invalid_argument("No free coop runtime house");
            slot.house = *free;
        }
        used.insert(slot.house);
    }
    // Populate valid provisional enemy slots before the first validation.
    std::vector<int> enemies;
    for(const int faction : roster)
        if(faction != playerFactions[0] && faction != playerFactions[1]) enemies.push_back(faction);
    if(enemies.size() < 3) throw std::invalid_argument("Not enough coop opponents");
    for(int i = 2; i < SlotCount; ++i) {
        auto& slot = context.slots[i];
        slot.faction = enemies[i - 2];
        slot.house = slot.faction;
        if(used.count(slot.house)) {
            const auto free = std::find_if(roster.begin(), roster.end(), [&used](int house) { return !used.count(house); });
            if(free == roster.end()) throw std::invalid_argument("No free coop runtime house");
            slot.house = *free;
        }
        used.insert(slot.house);
        slot.playerClass = "CampaignAIPlayer";
        slot.playerName = "Opponent " + std::to_string(i - 1);
    }
    CoopCampaignSession session(std::move(context));
    session.chooseOpponents();
    return session;
}

void CoopCampaignSession::enableExpandedAllies(bool archon) {
    if(context_.alliedCapacity!=2) throw std::invalid_argument("Coop already has four allied slots");
    auto revised=context_;
    revised.alliedCapacity=4;
    revised.archon=archon;
    for(int i=2;i<4;++i) {
        Slot slot;
        slot.faction=context_.roster[i];
        slot.playerName="Guest "+std::to_string(i+1);
        slot.playerClass="Closed";
        slot.allied=true;
        revised.slots.push_back(slot);
    }
    std::vector<int> enemies;
    for(std::size_t i=0;i<revised.enemyPresent.size();++i) enemies.push_back(revised.slots[i+2].faction);
    bindEnemySlots(revised,enemies,revised.enemyPresent);
    context_=std::move(revised);
    context_.randomVariants=context_.followsOriginalCampaign();
    chooseMissionVariant();
}

void CoopCampaignSession::chooseMissionVariant() {
    if(!context_.randomVariants || context_.isComplete()) { context_.missionVariant=0;return; }
    std::uint32_t state=context_.seed^0x4d415000u^(0x9e3779b9u*static_cast<std::uint32_t>(context_.stage));
    if(state==0) state=1;
    context_.missionVariant=nextRandom(state)%missionVariantCount(context_.stage);
}

void CoopCampaignSession::reconfigureAllies(const std::vector<Slot>& allies) {
    if(allies.size()!=static_cast<std::size_t>(context_.alliedCapacity))
        throw std::invalid_argument("Invalid allied slot count");
    auto revised=*this;
    const auto indices=revised.context_.alliedSlots();
    for(std::size_t i=0;i<allies.size();++i) {
        revised.context_.slots[indices[i]]=allies[i];
        revised.context_.slots[indices[i]].allied=true;
    }
    if(!revised.context_.followsOriginalCampaign()) revised.chooseOpponents();
    else {
        std::vector<int> enemies;
        for(std::size_t i=0;i<revised.context_.enemyPresent.size();++i) enemies.push_back(revised.context_.slots[i+2].faction);
        bindEnemySlots(revised.context_,enemies,revised.context_.enemyPresent);
    }
    validate(revised.context_);
    context_=std::move(revised.context_);
}

void CoopCampaignSession::configureAlly(int ordinal,int faction,const std::string& playerClass,
    const std::string& name,const std::vector<std::string>& controllers) {
    if(ordinal<0 || ordinal>=context_.alliedCapacity) throw std::invalid_argument("Invalid allied slot");
    std::vector<Slot> allies;
    for(const int i:context_.alliedSlots()) allies.push_back(context_.slots[i]);
    auto& slot=allies[ordinal];
    slot.faction=faction;
    slot.playerClass=playerClass;
    slot.playerName=name;
    slot.controllers=controllers;
    reconfigureAllies(allies);
}

CoopCampaignSession CoopCampaignSession::fromMapData(const std::string& mapData) {
    auto context = readContext(mapData);
    if(!context) throw std::invalid_argument("Not a coop campaign map");
    return CoopCampaignSession(std::move(*context));
}

std::string CoopCampaignSession::missionFilename() const {
    if(isComplete()) throw std::logic_error("Coop campaign is complete");
    return (context_.followsOriginalCampaign()
        ? "faction" + std::to_string(context_.sourceFaction) + "/" : "")
        + "coop0" + std::to_string(context_.stage)
        + (context_.missionVariant ? "-v"+std::to_string(context_.missionVariant) : "") + ".ini";
}

std::string CoopCampaignSession::prepareMap(const std::string& templateData) const {
    if(isComplete()) throw std::logic_error("Coop campaign is complete");
    const auto basic = readSection(templateData, "BASIC");
    const auto marker = readSection(templateData, "COOP_TEMPLATE");
    if(!basic || !marker || number<int>(require(*marker, "Stage")) != context_.stage)
        throw std::invalid_argument("Coop map does not match campaign stage");
    auto prepared = context_;
    const auto executable=context_.alliedCapacity==4 ? readContext(templateData) : std::nullopt;
    if(executable) {
        if(executable->sessionId!=context_.sessionId || executable->stage!=context_.stage
           || executable->alliedCapacity!=context_.alliedCapacity || executable->slots.size()!=context_.slots.size())
            throw std::invalid_argument("Prepared coop map does not match this session");
        for(std::size_t i=0;i<context_.slots.size();++i)
            if(executable->slots[i].faction!=context_.slots[i].faction || executable->slots[i].active()!=context_.slots[i].active())
                throw std::invalid_argument("Cannot change factions or starts in a running coop save");
        return withoutMetadata(templateData)+'\n'+serialize(context_);
    }
    if(context_.followsOriginalCampaign()) {
        const int mapTech=number<int>(require(*basic,"TechLevel"));
        const bool legacyFinal=context_.alliedCapacity==2 && context_.modName=="vanilla"
            && context_.stage==StageCount && mapTech==8;
        const int techLevel=context_.stage;
        if(require(*marker, "Schema") != "1"
           || (mapTech!=techLevel && !legacyFinal)
           || number<int>(require(*marker, "SourceFaction")) != context_.sourceFaction
           || number<int>(require(*marker, "OriginalMission")) != OriginalMissionNumbers[context_.stage - 1]+context_.missionVariant
           || require(*marker, "Mod") != context_.modName)
            throw std::invalid_argument("Coop map does not match the original campaign source");
        const int count = participantCount(*marker);
        const auto revision=basic->find("campaignrevision");
        const bool persistentTornie=(context_.modName=="Tornie" || context_.modName=="TornieLite")
            && revision!=basic->end() && revision->second=="2";
        if(count > SlotCount && ((context_.modName != "vanilla" && !(persistentTornie && count==6))
            || context_.roster.size() < static_cast<std::size_t>(count)))
            throw std::invalid_argument("Unsupported original campaign participant count");
        std::vector<int> factions(count - 2);
        std::vector<bool> present(count - 2);
        std::set<int> usedFactions;
        for(std::size_t i = 0; i < factions.size(); ++i) {
            factions[i] = number<int>(require(*marker, "EnemyFaction" + std::to_string(i + 1)));
            present[i] = factions[i] != -1;
            const auto presence = marker->find("enemypresent" + std::to_string(i + 1));
            if(presence != marker->end()) present[i] = boolean(presence->second);
            const bool invalidFaction = factions[i] != -1
                && (std::find(context_.roster.begin(), context_.roster.end(), factions[i]) == context_.roster.end()
                    || factions[i] == context_.sourceFaction || !usedFactions.insert(factions[i]).second);
            if((factions[i] == -1 && present[i]) || invalidFaction)
                throw std::invalid_argument("Invalid original campaign opponent");
        }
        const auto alliedFaction=[&](int faction) {
            for(const int i:context_.alliedSlots())
                if(context_.slots[i].active() && faction==context_.slots[i].faction) return true;
            return false;
        };
        // Family campaign enemies are authored roles, not lobby faction exclusions.
        // A matching guest keeps independent ownership without replacing the foe.
        for(auto& faction:factions) if(context_.modName=="vanilla" && alliedFaction(faction)) {
            usedFactions.erase(faction);
            faction=-1;
        }
        for(std::size_t i=0;i<factions.size();++i) if(factions[i]==-1) {
            auto free=std::find_if(context_.roster.begin(),context_.roster.end(),[&](int faction) {
                return !usedFactions.count(faction) && faction!=context_.sourceFaction && !alliedFaction(faction);
            });
            if(free==context_.roster.end() && context_.alliedCapacity==4)
                free=std::find_if(context_.roster.begin(),context_.roster.end(),[&](int faction) {
                    return faction!=context_.sourceFaction && !alliedFaction(faction);
                });
            if(free==context_.roster.end()) throw std::invalid_argument("No spare inactive coop faction");
            factions[i]=*free;
            usedFactions.insert(*free);
        }
        bindEnemySlots(prepared, factions, present);
    } else if(number<int>(require(*basic, "TechLevel")) != context_.stage) {
        throw std::invalid_argument("Coop map does not match campaign stage");
    }
    for(std::size_t i = 1; i <= prepared.enemyPresent.size()+2; ++i)
        if(!readSection(templateData, "Player" + std::to_string(i)))
            throw std::invalid_argument("Coop map is missing a participant slot");
    return materializeAllies(withoutMetadata(templateData),prepared) + '\n' + serialize(prepared);
}

void CoopCampaignSession::chooseOpponents() {
    std::vector<int> enemies;
    for(const int faction:context_.roster) {
        bool allied=false;
        for(const int i:context_.alliedSlots())
            allied=allied || (context_.slots[i].active() && faction==context_.slots[i].faction);
        if(!allied) enemies.push_back(faction);
    }
    if(enemies.empty()) throw std::invalid_argument("Not enough coop opponents");
    std::uint32_t random=context_.seed^0x434f4f50u;
    if(random==0) random=1;
    for(std::size_t i=enemies.size();i>1;--i) std::swap(enemies[i-1],enemies[nextRandom(random)%i]);
    std::vector<int> selected(context_.enemyPresent.size());
    for(std::size_t i=0;i<selected.size();++i) selected[i]=enemies[(context_.stage-1+i)%enemies.size()];
    bindEnemySlots(context_,selected,context_.enemyPresent);
}

void CoopCampaignSession::skipIntroForEasyMode() {
    if(context_.easyMode && context_.stage == 1 && context_.completedMask == 0) {
        context_.stage = 2;
        context_.completedMask = 1; // Mission 1 is explicitly waived by Easy Mode.
        chooseMissionVariant();
        if(!context_.followsOriginalCampaign()) chooseOpponents();
    }
}

void CoopCampaignSession::completeMission(bool won) {
    if(isComplete()) throw std::logic_error("Coop campaign is already complete");
    if(!won) return;
    context_.completedMask |= 1u << (context_.stage - 1);
    ++context_.stage;
    chooseMissionVariant();
    if(!isComplete() && !context_.followsOriginalCampaign()) chooseOpponents();
    validate(context_);
}

void CoopCampaignSession::setPlayerName(int playerSlot,const std::string& name) {
    if(playerSlot<0 || playerSlot>=context_.alliedCapacity || !validPlayerName(name))
        throw std::invalid_argument("Invalid coop player name");
    const auto& slot=context_.slots[context_.alliedSlots()[playerSlot]];
    auto controllers=slot.controllers;
    if(!controllers.empty()) controllers.front()=name;
    configureAlly(playerSlot,slot.faction,slot.playerClass,name,controllers);
}

void CoopCampaignSession::reconfigurePlayers(const std::array<int, 2>& playerFactions,
    const std::array<std::string, 2>& playerNames) {
    auto revised = create(context_.sessionId, context_.modName, context_.roster,
        playerFactions, playerNames, context_.seed, context_.chaosEligible, context_.mapLayout);
    revised.context_.sourceFaction = context_.sourceFaction;
    revised.context_.stage = context_.stage;
    revised.context_.completedMask = context_.completedMask;
    revised.context_.alliedControl = context_.alliedControl;
    revised.context_.alliedBuilding = context_.alliedBuilding;
    revised.context_.easyMode = context_.easyMode;
    revised.context_.extraEnemyForces = context_.extraEnemyForces;
    revised.context_.extraHardEnemyForces=context_.extraHardEnemyForces;
    revised.context_.alliedCapacity=context_.alliedCapacity;
    revised.context_.archon=context_.archon;
    revised.context_.randomVariants=context_.randomVariants;
    revised.context_.missionVariant=context_.missionVariant;
    for(int i=2;i<context_.alliedCapacity;++i)
        revised.context_.slots.push_back(context_.slots[context_.alliedSlots()[i]]);
    for(int i = 0; i < 2; ++i) {
        revised.context_.slots[i].color = context_.slots[i].color;
        revised.context_.slots[i].playerClass = context_.slots[i].playerClass;
        revised.context_.slots[i].controllers=context_.slots[i].controllers;
        if(!revised.context_.slots[i].controllers.empty()) revised.context_.slots[i].controllers.front()=playerNames[i];
    }
    if(context_.followsOriginalCampaign()) {
        std::vector<int> enemies(context_.enemyPresent.size());
        for(std::size_t i = 0; i < enemies.size(); ++i) enemies[i] = context_.slots[i + 2].faction;
        bindEnemySlots(revised.context_, enemies, context_.enemyPresent);
    } else if(!revised.isComplete()) revised.chooseOpponents();
    for(std::size_t i = 2; i < context_.enemyPresent.size()+2; ++i)
        revised.context_.slots[i].playerClass = context_.slots[i].playerClass;
    context_.slots = std::move(revised.context_.slots);
}

void CoopCampaignSession::setPlayerColor(int playerSlot, int color) {
    if(playerSlot < 0 || playerSlot >= context_.alliedCapacity || color < -1 || color >= NUM_HOUSE_COLOR_SLOTS)
        throw std::invalid_argument("Invalid coop player color");
    context_.slots[context_.alliedSlots()[playerSlot]].color = color;
}

void CoopCampaignSession::setChaosEligible(bool eligible) {
    // Disabling Chaos during the campaign cannot be repaired by re-enabling it.
    context_.chaosEligible = context_.chaosEligible && eligible;
}

void CoopCampaignSession::setPlayerClass(int slot, const std::string& playerClass) {
    if(slot < 1 || slot >= static_cast<int>(context_.slots.size())
       || (playerClass != "HumanPlayer" && !isCoopAIClass(playerClass)
           && !(context_.alliedCapacity==4 && context_.isAlliedSlot(slot) && playerClass=="Closed"))
       || (!context_.isAlliedSlot(slot) && playerClass == "HumanPlayer"))
        throw std::invalid_argument("Invalid coop player class");
    context_.slots[slot].playerClass = playerClass;
    if(playerClass!="HumanPlayer") context_.slots[slot].controllers.clear();
}

void CoopCampaignSession::saveProgress(const std::string& path, const std::string& settingsBlob,
    const std::string& checkpointPath) const {
    const auto content = serialize(context_) + "\n[PROGRESS]\nSettingsHex=" + hex(settingsBlob)
        + "\nCheckpointHex=" + hex(checkpointPath) + '\n';
    if(content.size() > MaxDataSize) throw std::invalid_argument("Coop progress is too large");
    const std::filesystem::path target(path);
    if(!target.parent_path().empty()) std::filesystem::create_directories(target.parent_path());
    const auto temporary = std::filesystem::path(path + ".tmp");
    const auto backup = std::filesystem::path(path + ".bak");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if(!output || !output.write(content.data(), static_cast<std::streamsize>(content.size())) || !output.flush())
            throw std::runtime_error("Cannot write coop progress: " + path);
    }
    const bool hadTarget = std::filesystem::exists(target);
    if(hadTarget) {
        // Windows rename does not replace a file. Keep the previous complete
        // record recoverable until the new record has been installed.
        if(std::filesystem::exists(backup)) std::filesystem::remove(backup);
        std::filesystem::rename(target, backup);
    }
    try {
        std::filesystem::rename(temporary, target);
    } catch(...) {
        if(hadTarget && !std::filesystem::exists(target)) std::filesystem::rename(backup, target);
        throw;
    }
    if(hadTarget) {
        std::error_code cleanupError;
        std::filesystem::remove(backup, cleanupError);
    }
}

CoopCampaignSession CoopCampaignSession::loadProgress(const std::string& path) {
    std::exception_ptr failure;
    for(const std::filesystem::path& candidate : {std::filesystem::path(path), std::filesystem::path(path + ".bak")}) {
        if(!std::filesystem::exists(candidate)) continue;
        try {
            const auto data = readFile(candidate);
            auto session = fromMapData(data);
            if(const auto progress = readSection(data, "PROGRESS")) {
                session.settingsBlob_ = unhex(require(*progress, "SettingsHex"));
                session.checkpointPath_ = unhex(require(*progress, "CheckpointHex"));
            }
            return session;
        } catch(...) {
            if(!failure) failure = std::current_exception();
        }
    }
    if(failure) std::rethrow_exception(failure);
    throw std::runtime_error("Cannot open coop progress: " + path);
}

} // namespace coop
