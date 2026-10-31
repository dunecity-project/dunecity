/* SPDX-License-Identifier: GPL-2.0-or-later
 * Tornie Gas Infantry behavior by Tornie_Panther, adapted for DuneCity.
 */
#pragma once
#include <units/InfantryBase.h>
#include <ObjectPointer.h>

// IX infantry: Deviator gas against enemies, healing missiles for allied units.
class GasInfantry final : public InfantryBase {
public:
    explicit GasInfantry(House* owner);
    explicit GasInfantry(InputStream& stream);
    void save(OutputStream& stream) const override;
    bool update() override;
    void doAttackObject(Uint32 targetObjectID, bool forced) override;
    bool canAttack(const ObjectBase* object) const override;
    bool canHeal() const override { return true; }
    void playAttackSound() override;
private:
    void init();
    bool isValidHealTarget(const ObjectBase* object) const;
    UnitBase* findDamagedAlly() const;
    void fireHealingMissile(UnitBase* unit);
    Sint32 healingReloadTimer = 0;
    ObjectPointer healingTarget;
    bool manualHealing = false;
};
