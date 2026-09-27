/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef SONICTRIKE_H
#define SONICTRIKE_H

#include <units/GroundUnit.h>

/// Rebels-only light vehicle that fires a shorter, weaker sonic wave.
class SonicTrike final : public GroundUnit
{
public:
    explicit SonicTrike(House* newOwner);
    explicit SonicTrike(InputStream& stream);
    void init();
    virtual ~SonicTrike() override;

    void destroy() override;
    void handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                      const DeviationReward::Provenance& provenance = DeviationReward::Provenance()) override;
    bool canAttack(const ObjectBase* object) const override;
    bool hasBumpyMovementOnRock() const override { return true; }
    void playAttackSound() override;
};

#endif // SONICTRIKE_H
