#ifndef NUCLEARPLANT_H
#define NUCLEARPLANT_H

#include <structures/StructureBase.h>

/**
 * Nuclear power plant: high-output power source for DuneCity mode.
 *
 * Mirrors WindTrap mechanics — produced power scales with health and
 * is added to the owner's House::producedPower pool. Sized 3x3,
 * rendered with the Micropolis nuclear-plant sprite scaled to fill
 * the footprint (HighTechFactory art is a fallback if missing).
 */
class NuclearPlant final : public StructureBase
{
public:
    explicit NuclearPlant(House* newOwner);
    explicit NuclearPlant(InputStream& stream);
    virtual ~NuclearPlant();

    ObjectInterface* getInterfaceContainer() override;
    bool update() override;
    void destroy() override;
    void handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                      const DeviationReward::Provenance& provenance = DeviationReward::Provenance()) override;
    void setHealth(FixPoint newHealth) override;

    int getProducedPower() const;

private:
    House* detonationCreditOwner = nullptr; // Runtime-only attribution for a pending detonation.
    Uint32 detonationTrigger = NONE_ID;
    void init();
};

#endif // NUCLEARPLANT_H
