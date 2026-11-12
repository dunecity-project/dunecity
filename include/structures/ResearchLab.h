#ifndef RESEARCHLAB_H
#define RESEARCHLAB_H

#include <structures/StructureBase.h>

/// Optional house technology research; uses the Tech Center art, not its vehicle rules.
class ResearchLab final : public StructureBase {
public:
    explicit ResearchLab(House* newOwner);
    ~ResearchLab() override;
    void handleResearchClick();
    explicit ResearchLab(InputStream& stream);
    ObjectInterface* getInterfaceContainer() override;
    bool canBeCaptured() const override { return false; }

private:
    void init();
};

#endif
