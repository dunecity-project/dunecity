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

#ifndef ROCKETTURRET_H
#define ROCKETTURRET_H

#include <structures/TurretBase.h>

class RocketTurret final : public TurretBase
{
public:
    explicit RocketTurret(House* newOwner);
    explicit RocketTurret(InputStream& stream);
    void init();
    virtual ~RocketTurret();

    bool canAttack(const ObjectBase* object) const override;

    const ObjectBase* findTarget() const override;
    void attack() override;

    /// Damage of one rocket as attack() fires it.
    int rocketDamage() const;

    /// Reload of the close-range cannon, scaled from the configured Gun-Turret reload.
    int closeCannonReloadTime() const;

    /// 128 of the gun turret's 240 cycles: 2.048 s, the interval Dynasty's turret
    /// script reaches with its cannon. See docs/weapon-reload-comparison.md.
    static constexpr int closeCannonReloadNumerator   = 128;
    static constexpr int closeCannonReloadDenominator = 240;

protected:
    /**
        Used for updating things that are specific to that particular structure. Is called from
        StructureBase::update() before the check if this structure is still alive.
    */
    void updateStructureSpecificStuff() override;

private:
    /// City Plane Defence: the nearest enemy city airplane this turret may shoot at inside
    /// its aircraft reach (triple weapon range, as for an Ornithopter), or nullptr.
    const ObjectBase* cityPlaneInReach() const;
    bool canMeetCityPlane(const ObjectBase* object) const;
};

#endif //ROCKETTURRET_H
