#include <dunecity/HouseColors.h>
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

#include <sand.h>
#include <Network/NetworkManager.h>

#include <globals.h>
#include <mod/ModManager.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/music/MusicPlayer.h>

#include <Menu/BriefingMenu.h>
#include <Menu/CampaignStatsMenu.h>
#include <Menu/CustomGameStatsMenu.h>

#include <CutScenes/Meanwhile.h>
#include <CutScenes/Finale.h>

#include <Game.h>
#include <GameInitSettings.h>
#include <data.h>

#include <misc/exceptions.h>

#include <algorithm>



/**
    This function resolves the picture corresponding to one item id.
    \param itemID   the id of the item to resolve (e.g. Unit_Quad)
    \return the surface corresponding. This surface should not be freed or modified. nullptr on error.
*/
SDL_Texture* resolveItemPicture(int itemID, HOUSETYPE house) {
    int newPicID;

    switch(itemID) {
        case Structure_Barracks:            newPicID = Picture_Barracks;            break;
        case Structure_ConstructionYard:    newPicID = Picture_ConstructionYard;    break;
        case Structure_GunTurret:           newPicID = Picture_GunTurret;           break;
        case Structure_HeavyFactory:        newPicID = Picture_HeavyFactory;        break;
        case Structure_HighTechFactory:     newPicID = Picture_HighTechFactory;     break;
        case Structure_IX:                  newPicID = Picture_IX;                  break;
        case Structure_LightFactory:        newPicID = Picture_LightFactory;        break;
        case Structure_Palace:              newPicID = Picture_Palace;              break;
        case Structure_Radar:               newPicID = Picture_Radar;               break;
        case Structure_Refinery:            newPicID = Picture_Refinery;            break;
        case Structure_RepairYard:          newPicID = Picture_RepairYard;          break;
        case Structure_RocketTurret:        newPicID = Picture_RocketTurret;        break;
        case Structure_Silo:                newPicID = Picture_Silo;                break;
        case Structure_Slab1:               newPicID = Picture_Slab1;               break;
        case Structure_Slab4:               newPicID = Picture_Slab4;               break;
        case Structure_StarPort:            newPicID = Picture_StarPort;            break;
        case Structure_Wall:                newPicID = Picture_Wall;                break;
        case Structure_WindTrap:            newPicID = Picture_WindTrap;            break;
        case Structure_WOR:                 newPicID = Picture_WOR;                 break;
        case Structure_NuclearPlant:        newPicID = Picture_NuclearPlant;        break;
        case Structure_PoliceStation:       newPicID = Picture_PoliceStation;       break;
        case Structure_Stadium:            newPicID = Picture_Stadium;             break;
        case Structure_Airport:            newPicID = Picture_Airport;             break;
        case Structure_AdvancedWindTrap:   newPicID = Picture_AdvancedWindTrap;    break;
        case Structure_AdvancedWindTrapMK2:newPicID = Picture_AdvancedWindTrap;    break;
        case Structure_AdvancedWindTrapMK3:newPicID = Picture_AdvancedWindTrap;    break;
        case Structure_Worfinery:          newPicID = Picture_Worfinery;           break;
        case Structure_TechCenter:         newPicID = Picture_TechCenter;          break;
        case Structure_Scoutpost:          newPicID = Picture_Scoutpost;           break;
        case Structure_Flamepost:          newPicID = Picture_Flamepost;           break;
        case Structure_Chemipost:          newPicID = Picture_Chemipost;           break;
        case Structure_LoveFactory:        newPicID = Picture_LoveFactory;       break;
        case Structure_ChaosFactory:       newPicID = Picture_ChaosFactory;      break;
        case Structure_ZoneResidential:    newPicID = Picture_ZoneResidential;    break;
        case Structure_ZoneCommercial:     newPicID = Picture_ZoneCommercial;     break;
        case Structure_ZoneIndustrial:     newPicID = Picture_ZoneIndustrial;     break;
        case Structure_Road:               newPicID = Picture_Road;               break;
        case Structure_PowerLine:          newPicID = Picture_PowerLine;          break;

        case Unit_Carryall:                 newPicID = Picture_Carryall;            break;
        case Unit_ChemicalCarryall:         newPicID = Picture_ChemicalCarryall;    break;
        case Delivery_Small:                newPicID = Picture_Trooper;             break;
        case Delivery_Medium:               newPicID = Picture_Quad;                break;
        case Delivery_Heavy:                newPicID = Picture_Tank;                break;
        case Delivery_Support:              newPicID = Picture_Carryall;            break;
        case Unit_Devastator:               newPicID = Picture_Devastator;          break;
        case Unit_Deviator:                 newPicID = Picture_Deviator;            break;
        case Unit_Frigate:                  newPicID = Picture_Frigate;             break;
        case Unit_Harvester:                newPicID = Picture_Harvester;           break;
        case Unit_Launcher:                 newPicID = Picture_Launcher;            break;
        case Unit_MCV:                      newPicID = Picture_MCV;                 break;
        case Unit_Ornithopter:              newPicID = Picture_Ornithopter;         break;
        case Unit_Quad:                     newPicID = Picture_Quad;                break;
        case Unit_RaiderTrike:              newPicID = Picture_RaiderTrike;         break;
        case Unit_SiegeTank:                newPicID = Picture_SiegeTank;           break;
        case Unit_SonicTank:                newPicID = Picture_SonicTank;           break;
        case Unit_Tank:                     newPicID = Picture_Tank;                break;
        case Unit_Trike:                    newPicID = Picture_Trike;               break;
        case Unit_Saboteur:                 newPicID = Picture_Saboteur;            break;
        case Unit_Sandworm:                 newPicID = Picture_Sandworm;            break;
        case Unit_AmbientAirplane:          newPicID = Picture_Carryall;            break;
        case Unit_AmbientHelicopter:        newPicID = Picture_Ornithopter;         break;
        case Unit_RocketTrike:              newPicID = Picture_RocketTrike;         break;
        case Unit_SonicTrike:               newPicID = Picture_SonicTrike;          break;
        case Unit_FlameTank:                newPicID = Picture_FlameTank;           break;
        case Unit_EliteLauncher:            newPicID = Picture_EliteLauncher;       break;
        case Unit_EliteSiegeTank:           newPicID = Picture_EliteSiegeTank;      break;
        case Unit_ChemicalSiegeTank:        newPicID = Picture_ChemicalSiegeTank;   break;
        case Unit_RebelHarvester:           newPicID = Picture_Harvestank;          break;
        case Unit_Soldier:                  newPicID = Picture_Soldier;             break;
        case Unit_Trooper: {
            switch(house) {
                case HOUSE_SARDAUKAR:       newPicID = Picture_Sardaukar;           break;
                case HOUSE_FREMEN:          newPicID = Picture_Fremen;              break;
                default:                    newPicID = Picture_Trooper;             break;
            }
        } break;
        case Unit_Special:                  newPicID = Picture_Special;             break;
        case Unit_Infantry:                 newPicID = Picture_Soldier;             break;
        case Unit_Troopers: {
            switch(house) {
                case HOUSE_SARDAUKAR:       newPicID = Picture_Sardaukar;           break;
                case HOUSE_FREMEN:          newPicID = Picture_Fremen;              break;
                default:                    newPicID = Picture_Trooper;             break;
            }
        } break;

        default:
            THROW(std::invalid_argument, "resolveItemPicture(): Invalid item ID " + std::to_string(itemID) + "!");
        break;
    }

    return pGFXManager->getSmallDetailPic(newPicID, house);
}


/**
    This function returns the anim id based on the passed filename.
    \param  filename    the filename (e.g. STARPORT.WSA)
    \return the id of the animation (e.g. Anim_StarPort)
*/
int getAnimByFilename(const std::string& filename) {
    const std::string lowerFilename = strToLower(filename);

    if(lowerFilename == "fartr.wsa")            return Anim_AtreidesPlanet;
    else if(lowerFilename == "fhark.wsa")       return Anim_HarkonnenPlanet;
    else if(lowerFilename == "fordos.wsa")      return Anim_OrdosPlanet;
    else if(lowerFilename == "win1.wsa")        return Anim_Win1;
    else if(lowerFilename == "win2.wsa")        return Anim_Win2;
    else if(lowerFilename == "lostbild.wsa")    return Anim_Lose1;
    else if(lowerFilename == "lostvehc.wsa")    return Anim_Lose2;
    else if(lowerFilename == "barrac.wsa")      return Anim_Barracks;
    else if(lowerFilename == "carryall.wsa")    return Anim_Carryall;
    else if(lowerFilename == "construc.wsa")    return Anim_ConstructionYard;
    else if(lowerFilename == "fremen.wsa")      return Anim_Fremen;
    else if(lowerFilename == "gold-bb.wsa")     return Anim_DeathHand;
    else if(lowerFilename == "harktank.wsa")    return Anim_Devastator;
    else if(lowerFilename == "harvest.wsa")     return Anim_Harvester;
    else if(lowerFilename == "headqrts.wsa")    return Anim_Radar;
    else if(lowerFilename == "hitcftry.wsa")    return Anim_HighTechFactory;
    else if(lowerFilename == "htank.wsa")       return Anim_SiegeTank;
    else if(lowerFilename == "hvyftry.wsa")     return Anim_HeavyFactory;
    else if(lowerFilename == "hyinfy.wsa")      return Anim_Trooper;
    else if(lowerFilename == "infantry.wsa")    return Anim_Infantry;
    else if(lowerFilename == "ix.wsa")          return Anim_IX;
    else if(lowerFilename == "liteftry.wsa")    return Anim_LightFactory;
    else if(lowerFilename == "ltank.wsa")       return Anim_Tank;
    else if(lowerFilename == "mcv.wsa")         return Anim_MCV;
    else if(lowerFilename == "ordrtank.wsa")    return Anim_Deviator;
    else if(lowerFilename == "orni.wsa")        return Anim_Ornithopter;
    else if(lowerFilename == "otrike.wsa")      return Anim_Raider;
    else if(lowerFilename == "palace.wsa")      return Anim_Palace;
    else if(lowerFilename == "quad.wsa")        return Anim_Quad;
    else if(lowerFilename == "refinery.wsa")    return Anim_Refinery;
    else if(lowerFilename == "repair.wsa")      return Anim_RepairYard;
    else if(lowerFilename == "rtank.wsa")       return Anim_Launcher;
    else if(lowerFilename == "rturret.wsa")     return Anim_RocketTurret;
    else if(lowerFilename == "saboture.wsa")    return Anim_Saboteur;
    else if(lowerFilename == "slab.wsa")        return Anim_Slab1;
    else if(lowerFilename == "stank.wsa")       return Anim_SonicTank;
    else if(lowerFilename == "starport.wsa")    return Anim_StarPort;
    else if(lowerFilename == "storage.wsa")     return Anim_Silo;
    else if(lowerFilename == "trike.wsa")       return Anim_Trike;
    else if(lowerFilename == "turret.wsa")      return Anim_GunTurret;
    else if(lowerFilename == "wall.wsa")        return Anim_Wall;
    else if(lowerFilename == "windtrap.wsa")    return Anim_WindTrap;
    else if(lowerFilename == "wor.wsa")         return Anim_WOR;
    else if(lowerFilename == "worm.wsa")        return Anim_Sandworm;
    else if(lowerFilename == "sardukar.wsa")    return Anim_Sardaukar;
    else if(lowerFilename == "frigate.wsa")     return Anim_Frigate;
    else if(lowerFilename == "4slab.wsa")       return Anim_Slab4;
    else                                        return 0;
}

/**
    This function returns the size of the specified item.
    \param ItemID   the id of the item (e.g. Structure_HeavyFactory)
    \return a Coord containg the size (e.g. (3,2) ). Returns (0,0) on error.
*/
Coord getStructureSize(int itemID) {

    switch(itemID) {
        case Structure_Barracks:            return Coord(2,2); break;
        case Structure_ConstructionYard:    return Coord(2,2); break;
        case Structure_GunTurret:           return Coord(1,1); break;
        case Structure_HeavyFactory:        return Coord(3,2); break;
        case Structure_HighTechFactory:     return Coord(3,2); break;
        case Structure_IX:                  return Coord(2,2); break;
        case Structure_LightFactory:        return Coord(2,2); break;
        case Structure_Palace:              return Coord(3,3); break;
        case Structure_Radar:               return Coord(2,2); break;
        case Structure_Refinery:            return Coord(3,2); break;
        case Structure_RepairYard:          return Coord(3,2); break;
        case Structure_RocketTurret:        return Coord(1,1); break;
        case Structure_Silo:                return Coord(2,2); break;
        case Structure_StarPort:            return Coord(3,3); break;
        case Structure_Slab1:               return Coord(1,1); break;
        case Structure_Slab4:               return Coord(2,2); break;
        case Structure_Wall:                return Coord(1,1); break;
        case Structure_WindTrap:            return Coord(2,2); break;
        case Structure_WOR:                 return Coord(2,2); break;
        case Structure_NuclearPlant:        return Coord(3,3); break;
        case Structure_PoliceStation:       return Coord(2,2); break;
        case Structure_ZoneResidential:     return Coord(2,2); break;
        case Structure_ZoneCommercial:      return Coord(2,2); break;
        case Structure_ZoneIndustrial:      return Coord(2,2); break;
        case Structure_Road:                return Coord(1,1); break;
        case Structure_PowerLine:           return Coord(1,1); break;
        case Structure_Stadium:             return Coord(3,3); break;
        case Structure_Airport:             return Coord(3,3); break;
        case Structure_AdvancedWindTrap:    return Coord(3,3); break;
        case Structure_AdvancedWindTrapMK2: return Coord(2,3); break;
        case Structure_AdvancedWindTrapMK3: return Coord(3,2); break;
        case Structure_Worfinery:           return Coord(3,2); break;
        case Structure_TechCenter:          return Coord(3,2); break;
        case Structure_Scoutpost:           return Coord(1,1); break;
        case Structure_Flamepost:           return Coord(1,1); break;
        case Structure_Chemipost:           return Coord(1,1); break;
        case Structure_LoveFactory:         return Coord(2,3); break;
        case Structure_ChaosFactory:        return Coord(3,2); break;
        default:                            return Coord(0,0); break;
    }
}

/**
    This function return the item id of an item specified by name. There may be multiple names for
    one item. The case of the name is ignored.
    \param name the name of the item (e.g. "rocket-turret" or "r-turret".
    \return the id of the item (e.g. Structure_RocketTurret)
*/
Uint32  getItemIDByName(const std::string& name) {
    const std::string lowerName = strToLower(name);

    if(lowerName == "barracks")                                                 return Structure_Barracks;
    else if((lowerName == "const yard") || (lowerName == "construction yard"))  return Structure_ConstructionYard;
    else if((lowerName == "r-turret") || (lowerName == "rocket-turret"))        return Structure_RocketTurret;
    else if((lowerName == "turret") || (lowerName == "gun-turret"))             return Structure_GunTurret;
    else if((lowerName == "heavy fctry") || (lowerName == "heavy factory"))     return Structure_HeavyFactory;
    else if((lowerName == "hi-tech") || (lowerName == "hightech factory"))      return Structure_HighTechFactory;
    else if((lowerName == "ix") || (lowerName == "house ix"))                   return Structure_IX;
    else if((lowerName == "light fctry") || (lowerName == "light factory"))     return Structure_LightFactory;
    else if(lowerName == "palace")                                              return Structure_Palace;
    else if((lowerName == "outpost") || (lowerName == "radar"))                 return Structure_Radar;
    else if(lowerName == "refinery")                                            return Structure_Refinery;
    else if((lowerName == "repair") || (lowerName == "repair yard"))            return Structure_RepairYard;
    else if((lowerName == "spice silo") || (lowerName == "silo"))               return Structure_Silo;
    else if((lowerName == "concrete") || (lowerName == "slab1"))                return Structure_Slab1;
    else if(lowerName == "slab4")                                               return Structure_Slab4;
    else if((lowerName == "star port") || (lowerName == "starport"))            return Structure_StarPort;
    else if(lowerName == "wall")                                                return Structure_Wall;
    else if(lowerName == "windtrap")                                            return Structure_WindTrap;
    else if(lowerName == "wor")                                                 return Structure_WOR;
    else if((lowerName == "nuclear") || (lowerName == "nuclear plant"))         return Structure_NuclearPlant;
    else if((lowerName == "police station") || (lowerName == "police"))         return Structure_PoliceStation;
    else if((lowerName == "zone residential") || (lowerName == "residential zone")) return Structure_ZoneResidential;
    else if((lowerName == "zone commercial") || (lowerName == "commercial zone"))   return Structure_ZoneCommercial;
    else if((lowerName == "zone industrial") || (lowerName == "industrial zone"))   return Structure_ZoneIndustrial;
    else if(lowerName == "road")                                                   return Structure_Road;
    else if((lowerName == "power line") || (lowerName == "powerline"))              return Structure_PowerLine;
    else if(lowerName == "stadium")                                                return Structure_Stadium;
    else if(lowerName == "airport")                                                return Structure_Airport;
    else if((lowerName == "advanced windtrap") || (lowerName == "advanced wind trap") || (lowerName == "advanced windtrap 3x3") || (lowerName == "advanced wind trap 3x3")) return Structure_AdvancedWindTrap;
    else if((lowerName == "advanced windtrap mk2") || (lowerName == "advanced wind trap mk2") || (lowerName == "advanced windtrap 2x3") || (lowerName == "advanced wind trap 2x3")) return Structure_AdvancedWindTrapMK2;
    else if((lowerName == "advanced windtrap mk3") || (lowerName == "advanced wind trap mk3") || (lowerName == "advanced windtrap 3x2") || (lowerName == "advanced wind trap 3x2")) return Structure_AdvancedWindTrapMK3;
    else if(lowerName == "worfinery")                                             return Structure_Worfinery;
    else if((lowerName == "tech center") || (lowerName == "techcenter"))          return Structure_TechCenter;
    else if((lowerName == "scoutpost") || (lowerName == "scout post") || (lowerName == "green post") || (lowerName == "sentinel post") || (lowerName == "avant-poste") || (lowerName == "avant poste")) return Structure_Scoutpost;
    else if((lowerName == "flamepost") || (lowerName == "flame post"))              return Structure_Flamepost;
    else if((lowerName == "chemipost") || (lowerName == "chemi post"))              return Structure_Chemipost;
    else if((lowerName == "love factory") || (lowerName == "lovefactory"))       return Structure_LoveFactory;
    else if((lowerName == "chaos factory") || (lowerName == "chaosfactory"))     return Structure_ChaosFactory;
    else if((lowerName == "carryall") || (lowerName == "carry-all"))            return Unit_Carryall;
    else if((lowerName == "chemical carryall") || (lowerName == "chemicalcarryall")) return Unit_ChemicalCarryall;
    else if((lowerName == "small delivery") || (lowerName == "delivery small")) return Delivery_Small;
    else if((lowerName == "medium delivery") || (lowerName == "delivery medium")) return Delivery_Medium;
    else if((lowerName == "heavy delivery") || (lowerName == "delivery heavy")) return Delivery_Heavy;
    else if((lowerName == "support delivery") || (lowerName == "delivery support")) return Delivery_Support;
    else if((lowerName == "devastator") || (lowerName == "devistator"))         return Unit_Devastator;
    else if(lowerName == "deviator")                                            return Unit_Deviator;
    else if(lowerName == "frigate")                                             return Unit_Frigate;
    else if(lowerName == "harvester")                                           return Unit_Harvester;
    else if(lowerName == "soldier")                                             return Unit_Soldier;
    else if(lowerName == "launcher")                                            return Unit_Launcher;
    else if(lowerName == "mcv")                                                 return Unit_MCV;
    else if((lowerName == "thopters") || (lowerName == "'thopters")
            || (lowerName == "thopter") || (lowerName == "'thopter")
            || (lowerName == "ornithopter"))                                    return Unit_Ornithopter;
    else if(lowerName == "quad")                                                return Unit_Quad;
    else if(lowerName == "saboteur")                                            return Unit_Saboteur;
    else if(lowerName == "sandworm")                                            return Unit_Sandworm;
    else if(lowerName == "siege tank")                                          return Unit_SiegeTank;
    else if((lowerName == "sonic tank") || (lowerName == "sonictank"))          return Unit_SonicTank;
    else if(lowerName == "tank")                                                return Unit_Tank;
    else if(lowerName == "trike")                                               return Unit_Trike;
    else if((lowerName == "raider trike") || (lowerName == "raider"))           return Unit_RaiderTrike;
    else if(lowerName == "trooper")                                             return Unit_Trooper;
    else if(lowerName == "special")                                             return Unit_Special;
    else if(lowerName == "infantry")                                            return Unit_Infantry;
    else if(lowerName == "troopers")                                            return Unit_Troopers;
    else if((lowerName == "ambient airplane") || (lowerName == "city airplane")) return Unit_AmbientAirplane;
    else if((lowerName == "ambient helicopter") || (lowerName == "city helicopter")) return Unit_AmbientHelicopter;
    else if(lowerName == "rocket trike")                                        return Unit_RocketTrike;
    else if(lowerName == "sonic trike")                                         return Unit_SonicTrike;
    else if(lowerName == "flame tank")                                          return Unit_FlameTank;
    else if(lowerName == "elite launcher")                                      return Unit_EliteLauncher;
    else if(lowerName == "elite siege tank")                                    return Unit_EliteSiegeTank;
    else if(lowerName == "chemical siege tank")                                 return Unit_ChemicalSiegeTank;
    else if((lowerName == "harvestank") || (lowerName == "rebel harvester"))     return Unit_RebelHarvester;
    else                                                                        return ItemID_Invalid;
}


/**
    This function returns the name of an item id.
    \param itemID the id of the item (e.g. Unit_Sandworm)
    \return the name of the item (e.g. "Sandworm").
*/
std::string getItemNameByID(Uint32 itemID) {
    switch(itemID) {
        case Structure_Barracks:            return "Barracks";          break;
        case Structure_ConstructionYard:    return "Const Yard";        break;
        case Structure_GunTurret:           return "Turret";            break;
        case Structure_HeavyFactory:        return "Heavy Fctry";       break;
        case Structure_HighTechFactory:     return "Hi-Tech";           break;
        case Structure_IX:                  return "IX";                break;
        case Structure_LightFactory:        return "Light Fctry";       break;
        case Structure_Palace:              return "Palace";            break;
        case Structure_Radar:               return "Outpost";           break;
        case Structure_Refinery:            return "Refinery";          break;
        case Structure_RepairYard:          return "Repair";            break;
        case Structure_RocketTurret:        return "R-Turret";          break;
        case Structure_Silo:                return "Spice Silo";        break;
        case Structure_Slab1:               return "Concrete";         break;
        case Structure_Slab4:               return "Slab4";         break;
        case Structure_StarPort:            return "Starport";          break;
        case Structure_Wall:                return "Wall";              break;
        case Structure_WindTrap:            return "Windtrap";          break;
        case Structure_WOR:                 return "WOR";               break;
        case Structure_NuclearPlant:        return "Nuclear";           break;
        case Structure_PoliceStation:       return "Police Station";    break;
        case Structure_ZoneResidential:     return "Residential Zone";  break;
        case Structure_ZoneCommercial:      return "Commercial Zone";   break;
        case Structure_ZoneIndustrial:      return "Industrial Zone";   break;
        case Structure_Road:                return "Road";              break;
        case Structure_PowerLine:           return "Power Line";        break;
        case Structure_Stadium:             return "Stadium";           break;
        case Structure_Airport:             return "Airport";           break;
        case Structure_AdvancedWindTrap:    return "Advanced Windtrap 3x3"; break;
        case Structure_AdvancedWindTrapMK2: return "Advanced Windtrap 2x3"; break;
        case Structure_AdvancedWindTrapMK3: return "Advanced Windtrap 3x2"; break;
        case Structure_Worfinery:           return "Worfinery";         break;
        case Structure_TechCenter:          return "Tech Center";       break;
        case Structure_Scoutpost:           return "Scoutpost";         break;
        case Structure_Flamepost:           return "Flamepost";         break;
        case Structure_Chemipost:           return "Chemipost";         break;
        case Structure_LoveFactory:         return "Love Factory";      break;
        case Structure_ChaosFactory:        return "Chaos Factory";     break;

        case Unit_Carryall:                 return "Carryall";          break;
        case Unit_ChemicalCarryall:         return "Chemical Carryall"; break;
        case Delivery_Small:                return "Small Delivery"; break;
        case Delivery_Medium:               return "Medium Delivery"; break;
        case Delivery_Heavy:                return "Heavy Delivery"; break;
        case Delivery_Support:              return "Support Delivery"; break;
        case Unit_Devastator:               return "Devastator";        break;
        case Unit_Deviator:                 return "Deviator";          break;
        case Unit_Frigate:                  return "Frigate";          break;
        case Unit_Harvester:                return "Harvester";     break;
        case Unit_Launcher:                 return "Launcher";          break;
        case Unit_MCV:                      return "MCV";               break;
        case Unit_Ornithopter:              return "'Thopter";          break;
        case Unit_Quad:                     return "Quad";              break;
        case Unit_RaiderTrike:              return "Raider Trike";      break;
        case Unit_SiegeTank:                return "Siege Tank";        break;
        case Unit_SonicTank:                return "Sonic Tank";        break;
        case Unit_Tank:                     return "Tank";              break;
        case Unit_Trike:                    return "Trike";         break;
        case Unit_Saboteur:                 return "Saboteur";          break;
        case Unit_Sandworm:                 return "Sandworm";          break;
        case Unit_Soldier:                  return "Soldier";           break;
        case Unit_Trooper:                  return "Trooper";           break;
        case Unit_Special:                  return "Special";           break;
        case Unit_Infantry:                 return "Infantry";          break;
        case Unit_Troopers:                 return "Troopers";          break;
        case Unit_AmbientAirplane:          return "Ambient Airplane";  break;
        case Unit_AmbientHelicopter:        return "Ambient Helicopter";break;
        case Unit_RocketTrike:              return "Rocket Trike";      break;
        case Unit_SonicTrike:               return "Sonic Trike";       break;
        case Unit_FlameTank:                return "Flame Tank";        break;
        case Unit_EliteLauncher:            return "Elite Launcher";    break;
        case Unit_EliteSiegeTank:           return "Elite Siege Tank";  break;
        case Unit_ChemicalSiegeTank:        return "Chemical Siege Tank"; break;
        case Unit_RebelHarvester:           return "Harvestank";        break;

        default:
            THROW(std::invalid_argument, "getItemNameByID(): Invalid item ID!");
        break;
    }
}


/**
    This function resolves the name corresponding to one item id.
    \param itemID   the id of the item to resolve (e.g. Unit_Quad)
    \return the string corresponding.
*/
std::string resolveItemName(int itemID) {
    switch(itemID) {
        case Structure_Barracks:            return _("@DUNE.ENG|253#Barracks");            break;
        case Structure_ConstructionYard:    return _("@DUNE.ENG|249#Construction Yard");   break;
        case Structure_GunTurret:           return _("@DUNE.ENG|263#Gun Turret");          break;
        case Structure_HeavyFactory:        return _("@DUNE.ENG|241#Heacy Factory");       break;
        case Structure_HighTechFactory:     return _("@DUNE.ENG|243#Hightech Factory");    break;
        case Structure_IX:                  return _("@DUNE.ENG|245#House IX");            break;
        case Structure_LightFactory:        return _("@DUNE.ENG|239#Light Factory");       break;
        case Structure_Palace:              return _("@DUNE.ENG|237#Palace");              break;
        case Structure_Radar:               return _("@DUNE.ENG|269#Radar");               break;
        case Structure_Refinery:            return _("@DUNE.ENG|256#Refinery");            break;
        case Structure_RepairYard:          return _("@DUNE.ENG|259#Repair Yard");         break;
        case Structure_RocketTurret:        return _("@DUNE.ENG|265#Rocket Turret");       break;
        case Structure_Silo:                return _("@DUNE.ENG|267#Silo");                break;
        case Structure_Slab1:               return _("@DUNE.ENG|233#Slab");                break;
        case Structure_Slab4:               return _("@DUNE.ENG|235#Slab (2x2)");          break;
        case Structure_StarPort:            return _("@DUNE.ENG|255#Starport");            break;
        case Structure_Wall:                return _("@DUNE.ENG|261#Wall");                break;
        case Structure_WindTrap:            return _("@DUNE.ENG|251#Windtrap");            break;
        case Structure_WOR:                 return _("@DUNE.ENG|247#WOR");                 break;
        case Structure_NuclearPlant:        return _("Nuclear Plant");                     break;
        case Structure_PoliceStation:       return _("Police Station");                    break;
        case Structure_ZoneResidential:     return _("Residential Zone");                  break;
        case Structure_ZoneCommercial:      return _("Commercial Zone");                   break;
        case Structure_ZoneIndustrial:      return _("Industrial Zone");                   break;
        case Structure_Road:                return _("Road");                              break;
        case Structure_PowerLine:           return _("Power Line");                        break;
        case Structure_Stadium:             return _("Stadium");                           break;
        case Structure_Airport:             return _("Airport");                           break;
        case Structure_AdvancedWindTrap:    return _("Advanced Windtrap");                 break;
        case Structure_AdvancedWindTrapMK2: return _("Advanced Windtrap");                 break;
        case Structure_AdvancedWindTrapMK3: return _("Advanced Windtrap");                 break;
        case Structure_Worfinery:           return _("Worfinery");                         break;
        case Structure_TechCenter:          return _("Tech Center");                       break;
        case Structure_Scoutpost:           return _("Scoutpost");                         break;
        case Structure_Flamepost:           return _("Flamepost");                         break;
        case Structure_Chemipost:           return _("Chemipost");                         break;
        case Structure_LoveFactory:         return _("Love Factory");
        case Structure_ChaosFactory:        return _("Chaos Factory");             break;

        case Unit_Carryall:                 return _("@DUNE.ENG|195#Carryall");            break;
        case Unit_ChemicalCarryall:         return _("Chemical Carryall");                   break;
        case Delivery_Small:                return _("Small Delivery"); break;
        case Delivery_Medium:               return _("Medium Delivery"); break;
        case Delivery_Heavy:                return _("Heavy Delivery"); break;
        case Delivery_Support:              return _("Support Delivery"); break;
        case Unit_Devastator:               return _("@DUNE.ENG|217#Devastator");          break;
        case Unit_Deviator:                 return _("@DUNE.ENG|211#Deviator");            break;
        case Unit_Frigate:                  return _("Frigate");                           break;
        case Unit_Harvester:                return _("@DUNE.ENG|227#Harvester");           break;
        case Unit_Launcher:                 return _("@DUNE.ENG|209#Launcher");            break;
        case Unit_MCV:                      return _("@DUNE.ENG|229#MCV");                 break;
        case Unit_Ornithopter:              return _("Ornithopter");                       break;
        case Unit_Quad:                     return _("@DUNE.ENG|225#Quad");                break;
        case Unit_RaiderTrike:              return _("@DUNE.ENG|223#Raider Trike");        break;
        case Unit_SiegeTank:                return _("@DUNE.ENG|215#Siege Tank");          break;
        case Unit_SonicTank:                return _("@DUNE.ENG|219#Sonic Tank");          break;
        case Unit_Tank:                     return _("@DUNE.ENG|213#Tank");                break;
        case Unit_Trike:                    return _("@DUNE.ENG|221#Trike");               break;
        case Unit_Saboteur:                 return _("@DUNE.ENG|207#Saboteur");            break;
        case Unit_Sandworm:                 return _("@DUNE.ENG|231#Sandworm");            break;
        case Unit_Soldier:                  return _("@DUNE.ENG|203#Soldier");             break;
        case Unit_Trooper:                  return _("@DUNE.ENG|205#Trooper");             break;
        case Unit_Special:                  return _("Sonic/Devast./Devia.");              break;
        case Unit_Infantry:                 return _("@DUNE.ENG|199#Infantry");            break;
        case Unit_Troopers:                 return _("@DUNE.ENG|201#Troopers");            break;
        case Unit_AmbientAirplane:          return _("City Airplane");                     break;
        case Unit_AmbientHelicopter:        return _("City Helicopter");                   break;
        case Unit_RocketTrike:              return _("Rocket Trike");                      break;
        case Unit_SonicTrike:               return _("Sonic Trike");                       break;
        case Unit_FlameTank:                return _("Flame Tank");                        break;
        case Unit_EliteLauncher:            return _("Elite Launcher");                    break;
        case Unit_EliteSiegeTank:           return _("Elite Siege Tank");                  break;
        case Unit_ChemicalSiegeTank:        return _("Chemical Siege Tank");               break;
        case Unit_RebelHarvester:           return _("Harvestank");                       break;

        default:
            THROW(std::invalid_argument, "resolveItemName(): Invalid item ID!");
        break;
    }
}


/**
    This function returns the number of each house providing the house name as a string. The comparison is
    done case-insensitive.
    \param name the name of the house (e.g."Atreides")
    \return the number of the house (e.g. HOUSE_ATREIDES). HOUSE_INVALID is returned on error.
*/
HOUSETYPE getHouseByName(const std::string& name) {
    const std::string lowerName = strToLower(name);

    if(lowerName == "harkonnen")         return HOUSE_HARKONNEN;
    else if(lowerName == "atreides")     return HOUSE_ATREIDES;
    else if(lowerName == "ordos")        return HOUSE_ORDOS;
    else if(lowerName == "fremen")       return HOUSE_FREMEN;
    else if(lowerName == "sardaukar")    return HOUSE_SARDAUKAR;
    else if(lowerName == "mercenary")    return HOUSE_MERCENARY;
    else if(lowerName == "neutral")      return HOUSE_NEUTRAL;
    else if((lowerName == "rebel") || (lowerName == "rebels")) return HOUSE_REBELS;
    else if(ModManager::instance().isTornieContentActive() && lowerName == "wildspade") return HOUSE_WILDSPADE;
    else if(ModManager::instance().isTornieContentActive() && lowerName == "kleshmersh") return HOUSE_KLESHMERSH;
    else if(ModManager::instance().isTornieContentActive() && lowerName == "tharpique") return HOUSE_THARPIQUE;
    else if(isHouseAvailable(HOUSE_CUSTOM)
            && lowerName == strToLower(ModManager::instance().getActiveCustomHouseInfo().displayName)) return HOUSE_CUSTOM;
    else                                return HOUSE_INVALID;
}

/**
    This function returns the name of house the house number.
    \param house the number of the house (e.g. HOUSE_ATREIDES)
    \return the name of the house (e.g. "Atreides").
*/
std::string getHouseNameByNumber(HOUSETYPE house) {
    if(house == HOUSE_CUSTOM) {
        if(isHouseAvailable(house)) return ModManager::instance().getActiveCustomHouseInfo().displayName;
        return "Custom";
    }
    if(house >= 0 && house < NUM_LEGACY_HOUSES) {
        static const char* const houseName[NUM_LEGACY_HOUSES] = {
            "Harkonnen", "Atreides", "Ordos", "Fremen", "Sardaukar",
            "Mercenary", "Neutral", "Rebels"
        };
        return houseName[house];
    }
    if(ModManager::instance().isTornieContentActive()) {
        if(house == HOUSE_WILDSPADE) return "Wildspade";
        if(house == HOUSE_KLESHMERSH) return "Kleshmersh";
        if(house == HOUSE_THARPIQUE) return "Tharpique";
    }
    THROW(std::invalid_argument, "Invalid house number %d!", house);
}

ATTACKMODE getAttackModeByName(const std::string& name) {
    const std::string lowerName = strToLower(name);

    if(lowerName == "guard")                                    return GUARD;
    else if(lowerName == "area guard")                          return AREAGUARD;
    else if(lowerName == "ambush")                              return AMBUSH;
    else if((lowerName == "hunt") || (lowerName == "attack"))   return HUNT;
    else if(lowerName == "harvest")                             return HARVEST;
    else if(lowerName == "sabotage")                            return SABOTAGE;
    else if(lowerName == "stop")                                return STOP;
    else if(lowerName == "capture")                             return CAPTURE;
    else if(lowerName == "retreat")                             return RETREAT;
    else                                                        return ATTACKMODE_INVALID;
}


std::string getAttackModeNameByMode(ATTACKMODE attackMode) {
    switch(attackMode) {
        case GUARD:     return "Guard";         break;
        case AREAGUARD: return "Area Guard";    break;
        case AMBUSH:    return "Ambush";        break;
        case HUNT:      return "Hunt";          break;
        case HARVEST:   return "Harvest";       break;
        case SABOTAGE:  return "Sabotage";      break;
        case STOP:      return "Stop";          break;
        case CAPTURE:   return "Capture";          break;
        default:
            THROW(std::invalid_argument, "getAttackModeNameByMode(): Invalid attack mode!");
        break;
    }
}


Uint32 getColorByTerrainType(int terrainType) {
    Uint32 color = COLOR_BLACK;
    switch (terrainType) {
        case Terrain_Dunes:         color = COLOR_DESERTSAND;  break;
        case Terrain_Mountain:      color = COLOR_MOUNTAIN;    break;
        case Terrain_Rock:          color = COLOR_ROCK;        break;
        case Terrain_Sand:          color = COLOR_DESERTSAND;  break;
        case Terrain_Spice:         color = COLOR_SPICE;       break;
        case Terrain_ThickSpice:    color = COLOR_THICKSPICE;  break;
        case Terrain_GreenSpice:    color = COLOR_RGB(28, 124, 48); break;
        case Terrain_ThickGreenSpice: color = COLOR_RGB(28, 124, 48); break;
        case Terrain_GreenSpiceBloom: color = COLOR_RGB(28, 124, 48); break;
        case Terrain_RedSpice:      color = COLOR_RGB(154, 48, 38); break;
        case Terrain_ThickRedSpice: color = COLOR_RGB(154, 48, 38); break;
        case Terrain_RedSpiceBloom: color = COLOR_RGB(154, 48, 38); break;
        case Terrain_SpiceBloom:    color = COLOR_BLOOM;       break;
        case Terrain_SpecialBloom:  color = COLOR_BLOOM;       break;
        case Terrain_Slab:          color = COLOR_ROCK;        break;
        default:                    color = COLOR_ROCK;        break;
    }

    return isDuneCityHouseColorSlot(HOUSE_HARKONNEN) ? DuneCity::radarTerrainColor(color) : color;
}

Uint32 getHouseRadarColor(HOUSETYPE house) {
    // Do not derive Neutral's radar marker from PALCOLOR_NEUTRAL. That palette
    // entry is grey and is indistinguishable from rock on the minimap.
    if(house == HOUSE_NEUTRAL) {
        return DuneCity::neutralRadarColor;
    }

    const int visualHouse = getHouseVisualHouse(house);


    if(isValidHouseColorSlot(visualHouse)) {
        return getHouseColorRGB(visualHouse, 0);
    }

    return COLOR_BLACK;
}

Uint32 getHouseInterfaceColor(HOUSETYPE house, int shadeOffset) {
    const int visualHouse = getHouseVisualHouse(house);


    if(isValidHouseColorSlot(visualHouse)) {
        return getHouseColorRGB(visualHouse, shadeOffset);
    }

    return COLOR_BLACK;
}



DropLocation getDropLocationByName(const std::string& name) {
    const std::string lowerName = strToLower(name);

    if(lowerName == "north") {
        return Drop_North;
    } else if(lowerName == "east") {
        return Drop_East;
    } else if(lowerName == "south") {
        return Drop_South;
    } else if(lowerName == "west") {
        return Drop_West;
    } else if(lowerName == "air") {
        return Drop_Air;
    } else if(lowerName == "visible") {
        return Drop_Visible;
    } else if(lowerName == "enemybase") {
        return Drop_Enemybase;
    } else if(lowerName == "homebase") {
        return Drop_Homebase;
    } else {
        return Drop_Invalid;
    }
}


std::string getDropLocationNameByID(DropLocation dropLocation) {
    switch(dropLocation) {
        case Drop_North:     return "North";     break;
        case Drop_East:      return "East";      break;
        case Drop_South:     return "South";     break;
        case Drop_West:      return "West";      break;
        case Drop_Air:       return "Air";       break;
        case Drop_Visible:   return "Visible";   break;
        case Drop_Enemybase: return "Enemybase"; break;
        case Drop_Homebase:  return "Homebase";  break;
        default:
            THROW(std::invalid_argument, "getDropLocationNameByID(): Invalid drop location!");
        break;
    }
}

std::string resolveDropLocationName(DropLocation dropLocation) {
    switch(dropLocation) {
        case Drop_North:     return _("top edge");     break;
        case Drop_East:      return _("right edge");   break;
        case Drop_South:     return _("bottom edge");  break;
        case Drop_West:      return _("left edge");    break;
        case Drop_Air:       return _("random");       break;
        case Drop_Visible:   return _("middle");       break;
        case Drop_Enemybase: return _("enemy base");   break;
        case Drop_Homebase:  return _("home base");    break;
        default:
            THROW(std::invalid_argument, "resolveDropLocationName(): Invalid drop location!");
        break;
    }
}

AITeamBehavior getAITeamBehaviorByName(const std::string& name) {
    const std::string lowerName = strToLower(name);

    if(lowerName == "normal") {
        return AITeamBehavior_Normal;
    } else if(lowerName == "guard") {
        return AITeamBehavior_Guard;
    } else if(lowerName == "kamikaze") {
        return AITeamBehavior_Kamikaze;
    } else if(lowerName == "staging") {
        return AITeamBehavior_Staging;
    } else if(lowerName == "flee") {
        return AITeamBehavior_Flee;
    } else {
        return AITeamBehavior_Invalid;
    }
}


std::string getAITeamBehaviorNameByID(AITeamBehavior aiTeamBehavior) {
    switch(aiTeamBehavior) {
        case AITeamBehavior_Normal:     return "Normal";     break;
        case AITeamBehavior_Guard:      return "Guard";      break;
        case AITeamBehavior_Kamikaze:   return "Kamikaze";   break;
        case AITeamBehavior_Staging:    return "Staging";    break;
        case AITeamBehavior_Flee:       return "Flee";       break;
        default:
            THROW(std::invalid_argument, "getAITeamBehaviorNameByID(): Invalid team behavior!");
        break;
    }
}


AITeamType getAITeamTypeByName(const std::string& name) {
    const std::string lowerName = strToLower(name);

    if(lowerName == "foot") {
        return AITeamType_Foot;
    } else if(lowerName == "wheel" || lowerName == "wheeled") {
        return AITeamType_Wheeled;
    } else if(lowerName == "track" || lowerName == "tracked") {
        return AITeamType_Tracked;
    } else if(lowerName == "winged") {
        return AITeamType_Winged;
    } else if(lowerName == "slither") {
        return AITeamType_Slither;
    } else if(lowerName == "harvester") {
        return AITeamType_Harvester;
    } else {
        return AITeamType_Invalid;
    }
}


std::string getAITeamTypeNameByID(AITeamType aiTeamType) {
    switch(aiTeamType) {
        case AITeamType_Foot:      return "Foot";      break;
        case AITeamType_Wheeled:   return "Wheeled";   break;
        case AITeamType_Tracked:   return "Tracked";   break;
        case AITeamType_Winged:    return "Winged";    break;
        case AITeamType_Slither:   return "Slither";   break;
        case AITeamType_Harvester: return "Harvester"; break;
        default:
            THROW(std::invalid_argument, "getAITeamTypeNameByID(): Invalid team type!");
        break;
    }
}


/**
    Starts a game replay
    \param  filename    the filename of the replay file
*/
void startReplay(const std::string& filename) {
    SDL_Log("Initializing replay...");
    try {
        currentGame = new Game();
        currentGame->initReplay(filename);

        currentGame->runMainLoop();

        delete currentGame;
        currentGame = nullptr;

        // Change music to menu music
        musicPlayer->changeMusic(MUSIC_MENU);
    } catch(...) {
        delete currentGame;
        currentGame = nullptr;
        throw;
    }
}


/**
    Starts a new game. If this game is quit it might start another game. This other game is also started from
    this function. This is done until there is no more game to be started.
    \param init contains all the information to start the game
*/
void startSinglePlayerGame(const GameInitSettings& init)
{
    GameInitSettings currentGameInitInfo = init;

    while(1) {

        try {

            SDL_Log("Initializing game...");
            currentGame = new Game();
            currentGame->initGame(currentGameInitInfo);

            // get init settings from game as it might have changed (through loading the game)
            currentGameInitInfo = currentGame->getGameInitSettings();

            currentGame->runMainLoop();

            bool bGetNext = true;
            while(bGetNext) {
                switch(currentGame->whatNext()) {
                    case GAME_DEBRIEFING_WIN: {
                        SDL_Log("Debriefing...");
                        {
                            BriefingMenu briefing(currentGameInitInfo.getHouseID(), currentGameInitInfo.getMission(), DEBRIEFING_WIN);
                            briefing.showMenu();
                        }

                        SDL_Log("Game statistics...");
                        {
                            CampaignStatsMenu campaignStats(missionNumberToLevelNumber(currentGameInitInfo.getMission()));
                            campaignStats.showMenu();
                        }

                        const int houseID = currentGameInitInfo.getHouseID();

                        if(currentGameInitInfo.getGameType() == GameType::Campaign) {
                            const int level = missionNumberToLevelNumber(currentGameInitInfo.getMission());

                            if(level == 4 && (houseID == HOUSE_HARKONNEN || houseID == HOUSE_ATREIDES || houseID == HOUSE_ORDOS)) {
                                SDL_Log("Playing meanwhile...");
                                Meanwhile meanwhile(houseID,true);
                                meanwhile.run();
                            } else if(level == 8 && (houseID == HOUSE_HARKONNEN || houseID == HOUSE_ATREIDES || houseID == HOUSE_ORDOS)) {
                                SDL_Log("Playing meanwhile...");
                                Meanwhile meanwhile(houseID,false);
                                meanwhile.run();
                            } else if(level == 9) {
                                SDL_Log("Playing finale.....");
                                Finale finale(houseID);
                                finale.run();
                            }
                        }
                    } break;

                    case GAME_DEBRIEFING_LOST: {
                        SDL_Log("Debriefing...");
                        BriefingMenu briefing(currentGameInitInfo.getHouseID(), currentGameInitInfo.getMission(), DEBRIEFING_LOST);
                        briefing.showMenu();
                    } break;

                    case GAME_CUSTOM_GAME_STATS: {
                        SDL_Log("Game statistics...");
                        CustomGameStatsMenu stats;
                        stats.showMenu();
                    } break;

                    case GAME_LOAD:
                    case GAME_NEXTMISSION: {
                        currentGameInitInfo = currentGame->getNextGameInitSettings();
                        bGetNext = false;
                    } break;

                    case GAME_RETURN_TO_MENU:
                    default: {
                        delete currentGame;
                        currentGame = nullptr;
                        resetHouseVisualHouseMapping();

                        // Change music to menu music
                        musicPlayer->changeMusic(MUSIC_MENU);

                        return;
                    } break;
                }
            }
        } catch(...) {
            delete currentGame;
            currentGame = nullptr;
            resetHouseVisualHouseMapping();
            throw;
        }
    }
}

/**
    Starts a new multiplayer game.
    \param init contains all the information to start the game
*/
namespace {
class CoopMissionWait : public MenuBase {
public:
    std::unique_ptr<GameInitSettings> next;
    CoopMissionWait() {
        setCurrentPosition(50, 50, 520, 100);
        setWindowWidget(&layout);
        layout.addWidget(Label::create(_("Waiting for host to choose the next mission...")));
    }
    void update() override {
        next = pNetworkManager->takeCoopMission();
        if(next || pNetworkManager->getConnectedPeers().empty()) quit();
    }
private:
    VBox layout;
};
}

void startMultiPlayerGame(const GameInitSettings& init)
{
    GameInitSettings currentGameInitInfo = init;
    try {
        while(true) {
            currentGame = new Game();
            currentGame->initGame(currentGameInitInfo);
            currentGameInitInfo = currentGame->getGameInitSettings();
            currentGame->runMainLoop();
            if(auto resumed=pNetworkManager->takeLateJoin()) {
                delete currentGame; currentGame=nullptr; resetHouseVisualHouseMapping();
                currentGameInitInfo=*resumed; continue;
            }
            int action = currentGame->whatNext();
            if(action == GAME_CUSTOM_GAME_STATS) {
                CustomGameStatsMenu().showMenu();
            } else if(action == GAME_DEBRIEFING_WIN || action == GAME_DEBRIEFING_LOST) {
                BriefingMenu(currentGameInitInfo.getHouseID(), currentGameInitInfo.getMission(),
                    action == GAME_DEBRIEFING_WIN ? DEBRIEFING_WIN : DEBRIEFING_LOST).showMenu();
                if(action == GAME_DEBRIEFING_WIN) {
                    const int level = missionNumberToLevelNumber(currentGameInitInfo.getMission());
                    CampaignStatsMenu(level).showMenu();
                    if(currentGameInitInfo.getGameType() == GameType::CampaignCoop) {
                        const auto house = currentGameInitInfo.getHouseID();
                        if((level == 4 || level == 8)
                           && (house == HOUSE_HARKONNEN || house == HOUSE_ATREIDES || house == HOUSE_ORDOS))
                            Meanwhile(house, level == 4).run();
                        else if(level == 9) Finale(house).run();
                    }
                }
            }
            std::unique_ptr<GameInitSettings> next;
            if(currentGameInitInfo.getGameType() == GameType::CampaignCoop) {
                if(pNetworkManager->isServer()) {
                    if(action != GAME_RETURN_TO_MENU && currentGame->whatNext() == GAME_NEXTMISSION)
                        next = std::make_unique<GameInitSettings>(currentGame->getNextGameInitSettings());
                    pNetworkManager->sendCoopMission(next ? *next : GameInitSettings());
                } else if(action != GAME_RETURN_TO_MENU) {
                    CoopMissionWait wait;
                    wait.showMenu();
                    next = std::move(wait.next);
                }
            }
            delete currentGame;
            currentGame = nullptr;
            resetHouseVisualHouseMapping();
            if(!next || next->getGameType() == GameType::Invalid) break;
            currentGameInitInfo = *next;
        }
    } catch(...) {
        delete currentGame;
        currentGame = nullptr;
        resetHouseVisualHouseMapping();
        throw;
    }
}
