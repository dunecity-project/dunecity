/*
 *  This file is part of Dune Legacy.
 */

#ifndef CHEMICALCARRYALL_H
#define CHEMICALCARRYALL_H

#include <units/Carryall.h>

class ChemicalCarryall final : public Carryall
{
public:
    explicit ChemicalCarryall(House* newOwner);
    explicit ChemicalCarryall(InputStream& stream);
    ~ChemicalCarryall() override;

    bool update() override;
    void deploy(const Coord& newLocation) override;
    void setProductionHouseID(int houseID) override;

    bool canHeal() const override { return true; }
    void doAttackObject(Uint32 targetObjectID, bool bForced) override;

private:
    void ensureRuntimeObjectData();
    UnitBase* findDamagedAlly(int range) const;
    bool isValidHealTarget(const ObjectBase* pObject) const;
    void fireHealingMissile(UnitBase* pTarget);
    void healCargo();

    ObjectPointer healingTarget;
    int healingReloadTimer = 0;
};

#endif // CHEMICALCARRYALL_H