#ifndef RESEARCHLABINTERFACE_H
#define RESEARCHLABINTERFACE_H

#include "DefaultStructureInterface.h"
#include <GUI/ResearchButton.h>
#include <GUI/Label.h>
#include <GUI/VBox.h>
#include <House.h>
#include <structures/ResearchLab.h>

class ResearchLabInterface final : public DefaultStructureInterface {
public:
    static ResearchLabInterface* create(int objectID) {
        auto* view = new ResearchLabInterface(objectID);
        view->pAllocated = true;
        return view;
    }

protected:
    explicit ResearchLabInterface(int objectID) : DefaultStructureInterface(objectID) {
        mainHBox.addWidget(&box);
        const Uint32 color = ownerAccentColor();
        level.setTextFontSize(12);
        level.setTextColor(color);
        box.addWidget(&level, 22);
        research.setTextColor(color);
        research.setOnClick([objectID] {
            auto* lab = dynamic_cast<ResearchLab*>(currentGame->getObjectManager().getObject(objectID));
            if(lab) lab->handleResearchClick();
        });
        box.addWidget(&research, 38);
        box.addWidget(Spacer::create(), 0.99);
    }

    bool update() override {
        const auto* lab = dynamic_cast<ResearchLab*>(currentGame->getObjectManager().getObject(objectID));
        if(!lab) return false;
        const House* owner = lab->getOwner();
        const int tech = owner->getTechLevel();
        level.setText(_("Tech level ") + std::to_string(tech) + "/9");
        const bool active = owner->isTechResearchActive();
        const int target = active ? owner->getTechResearchTarget() : std::min(9, tech + 1);
        research.setVisible(currentGame->getGameInitSettings().getGameOptions().researchLab);
        research.setText(tech >= 9 ? _("All tech researched") : _("Research tech level ") + std::to_string(target));
        research.setPrice(tech >= 9 ? 0 : 300 * target);
        research.setEnabled(currentGame->canControlHouse(owner) && owner->canStartTechResearch(objectID));
        if(active) {
            const int percent = 100 - owner->getTechResearchRemaining() * 100 / House::techResearchCycles(target);
            research.setStatus(std::to_string(percent) + " %");
        } else research.setStatus(tech >= 9 ? _("Complete") : "");
        if(displayedTarget != target) {
            displayedTarget = target;
            research.setTooltipText(_("Raises only your house's tech level. ")
                + std::to_string(300 * target) + _(" credits, ")
                + std::to_string(30 + 10 * target) + _(" seconds. Losing the researching lab cancels without a refund."));
        }
        return DefaultStructureInterface::update();
    }

private:
    VBox box;
    Label level;
    ResearchButton research;
    int displayedTarget = -1;
};

#endif
