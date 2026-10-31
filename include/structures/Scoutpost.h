/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef SCOUTPOST_H
#define SCOUTPOST_H

#include <structures/TurretBase.h>

class UnitBase;

class Scoutpost final : public TurretBase
{
public:
    explicit Scoutpost(House* newOwner, int newItemID = Structure_Scoutpost);
    explicit Scoutpost(InputStream& stream, int newItemID = Structure_Scoutpost);
    void init(int newItemID);
    ~Scoutpost() override;

    bool canBeCaptured() const override;
    bool canAttack(const ObjectBase* object) const override;
    ObjectInterface* getInterfaceContainer() override;
    void setHealth(FixPoint newHealth) override;

    int getProducedPower() const;

    bool isFlamepostUpgradeEligible() const;
    bool canUpgradeToFlamepost() const;
    int getFlamepostUpgradeCost() const;
    void handleFlamepostUpgradeClick();
    void doUpgradeToFlamepost();

    bool isChemipostUpgradeEligible() const;
    bool canUpgradeToChemipost() const;
    int getChemipostUpgradeCost() const;
    void handleChemipostUpgradeClick();
    void doUpgradeToChemipost();

    bool isSlowpostUpgradeEligible() const;
    bool canUpgradeToSlowpost() const;
    int getSlowpostUpgradeCost() const;
    void handleSlowpostUpgradeClick();
    void doUpgradeToSlowpost();

protected:
    void updateStructureSpecificStuff() override;

private:
    UnitBase* findDamagedAlly() const;
};

#endif // SCOUTPOST_H
