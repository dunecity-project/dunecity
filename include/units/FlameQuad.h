/* SPDX-License-Identifier: GPL-2.0-or-later
 * Tornie Flame Quad behavior by Tornie_Panther, adapted for DuneCity.
 */
#ifndef FLAMEQUAD_H
#define FLAMEQUAD_H

#include <units/GroundUnit.h>

class FlameQuad final : public GroundUnit
{
public:
    explicit FlameQuad(House* newOwner);
    explicit FlameQuad(InputStream& stream);
    void init();
    ~FlameQuad() override;

    void destroy() override;
    bool hasBumpyMovementOnRock() const override { return true; }
    void playAttackSound() override;
};

#endif // FLAMEQUAD_H
