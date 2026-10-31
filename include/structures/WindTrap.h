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

#ifndef WINDTRAP_H
#define WINDTRAP_H

#include <structures/StructureBase.h>

class WindTrap final : public StructureBase
{
public:
    explicit WindTrap(House* newOwner);
    explicit WindTrap(InputStream& stream);
    virtual ~WindTrap();

    ObjectInterface* getInterfaceContainer() override;

    /**
        Updates this object.
        \return true if this object still exists, false if it was destroyed
    */
    bool update() override;

    void setHealth(FixPoint newHealth) override;
    void save(OutputStream& stream) const override;
    bool isPowerUpgraded() const { return powerUpgraded; }
    static bool supportsPowerUpgrade(HOUSETYPE house);
    bool isPowerUpgradeEligible() const;
    void handlePowerUpgradeClick();
    bool doPowerUpgrade();
    void inheritPowerUpgrade(bool upgraded);

    /// Actual output, including damage-related loss.
    int getProducedPower() const;

protected:
    bool usesGreenPowerAnimation() const override;

private:
    void init();
    bool powerUpgraded = false;
};

#endif //WINDTRAP_H
