#ifndef PARK_H
#define PARK_H

#include <structures/StructureBase.h>

/**
 * Park — DuneCity city-sim structure modelled on the Micropolis park tool.
 *
 * 1x1, 10 credits, no power. Like Micropolis's wooded park tiles it is a natural-terrain
 * source (+15 raw park terrain, DuneCity::getParkLandValueBonus) that raises the land value of
 * the blocks around it; it does not develop its own block, move the city centre, need concrete,
 * decay, grow a road ring or keep a house alive (docs/city-economy-balance.md, "Parks").
 */
class Park final : public StructureBase
{
public:
    explicit Park(House* newOwner);
    explicit Park(InputStream& stream);
    ~Park() override;

    bool canBeCaptured() const override { return false; }

private:
    void init();
    void updateStructureSpecificStuff() override;
};

#endif // PARK_H
