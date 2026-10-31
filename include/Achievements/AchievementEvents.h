// The only engine adapter; no achievement rules belong in gameplay classes.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef TORNIE_ACHIEVEMENT_EVENTS_H
#define TORNIE_ACHIEVEMENT_EVENTS_H
#include <cstdint>
#include <string>
class Game;class House;class ObjectBase;class UnitBase;class Tile;
namespace AchievementEvents {
bool initializeProfile();
std::string fileKey(const std::string& filename);
std::string dataKey(const std::string& data);
void begin(Game& game,const std::string& key,bool resumed,bool eligible);
void finish(bool won);
void pump(Game& game);
void damage(ObjectBase* victim,std::uint32_t attackerID,House* attacker,bool lethal);
void lost(UnitBase* unit);
void wormDefeated(UnitBase* unit);
void built(House* house,ObjectBase* object,bool produced = true);
void sold(House* owner, int refund);
void captured(House* captor,House* previousOwner);
void crushed(ObjectBase* victim,ObjectBase* vehicle);
void refined(House* house,std::uint64_t total);
void spice(House* house,const Tile* tile);
void bloom(House* house);
void palace(House* house);
void missile(House* house);
void beginMissileImpact(House* house, std::uint32_t shooter);
void endMissileImpact();
}
#endif
