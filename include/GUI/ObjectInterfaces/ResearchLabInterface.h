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
        status.setTextFontSize(11);
        status.setTextColor(color);
        status.setAlignment(Alignment_HCenter);
        box.addWidget(&status, 42);
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
        const int cost = owner->getTechResearchCost();
        const int duration = tech >= 9 ? 0 : owner->techResearchCycles(target);
        research.setPrice(cost);
        const bool canControl = currentGame->canControlHouse(owner);
        const bool affordable = owner->getStartingCredits() + owner->getEarnedCredits()
            >= FixPoint(cost);
        research.setEnabled(canControl && owner->canStartTechResearch(objectID));
        if(active) {
            const int percent = 100 - static_cast<int>(std::int64_t(owner->getTechResearchRemaining()) * 100 / duration);
            research.setStatus(std::to_string(percent) + " %");
            status.setText(_("Research in progress") + "\n" + timeText(owner->getTechResearchRemaining()) + _(" remaining"));
        } else {
            research.setStatus(tech >= 9 ? _("Complete") : "");
            status.setText((tech >= 9 ? _("All technology unlocked")
                : !canControl ? _("View only")
                : !affordable ? _("Needs ") + std::to_string(cost) + _(" credits")
                : !lab->isActive() ? _("Lab must be deployed")
                : _("Ready to research"))
                + (duration > 0 ? "\n" + _("Time: ") + timeText(duration) : ""));
        }
        if(displayedTarget != target || displayedCost != cost || displayedDuration != duration) {
            displayedTarget = target;
            displayedCost = cost;
            displayedDuration = duration;
            research.setTooltipText(tech >= 9 ? _("All technology unlocked")
                : _("Raises only your house's tech level. ")
                    + std::to_string(cost) + _(" credits, ")
                    + std::to_string((std::int64_t(duration) * GAMESPEED_DEFAULT + 999) / 1000)
                    + _(" game seconds. Losing the researching lab cancels without a refund."));
        }
        return DefaultStructureInterface::update();
    }

private:
    static std::string timeText(int cycles) {
        const auto seconds = (std::int64_t(cycles) * GAMESPEED_DEFAULT + 999) / 1000;
        return std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") + std::to_string(seconds % 60);
    }
    VBox box;
    Label level;
    ResearchButton research;
    Label status;
    int displayedTarget = -1;
    int displayedCost = -1, displayedDuration = -1;
};

#endif
