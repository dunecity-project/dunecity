#ifndef RESEARCHLABINTERFACE_H
#define RESEARCHLABINTERFACE_H

#include "DefaultStructureInterface.h"
#include <GUI/ResearchButton.h>
#include <GUI/Label.h>
#include <GUI/VBox.h>
#include <House.h>
#include <structures/ResearchLab.h>
#include <algorithm>
#include <cstdint>

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
        for(Label* label : {&total, &drain, &paid, &status, &remaining, &access}) {
            label->setTextFontSize(11);
            label->setTextColor(color);
            label->setAlignment(Alignment_HCenter);
            box.addWidget(label, 20);
        }
        box.addWidget(Spacer::create(), 0.99);
    }

    bool update() override {
        const auto* lab = dynamic_cast<ResearchLab*>(currentGame->getObjectManager().getObject(objectID));
        if(!lab) return false;
        const House* owner = lab->getOwner();
        const auto& options = currentGame->getGameInitSettings().getGameOptions();
        const int tech = owner->getTechLevel();
        const bool active = owner->isTechResearchActive();
        const bool complete = tech >= 9 && !active;
        const bool installments = active ? owner->isTechResearchInstallment() : options.technologyInstallments;
        const int target = active ? owner->getTechResearchTarget() : std::min(9, tech + 1);
        const int cost = active ? owner->getTechResearchTotalCost() : owner->getTechResearchCost();
        const int duration = active ? owner->getTechResearchTotalCycles()
                                   : complete ? 0 : owner->techResearchCycles(target);
        const bool canControl = currentGame->canControlHouse(owner);
        level.setText(_("Tech level ") + std::to_string(tech) + "/9");
        research.setVisible(options.researchLab || active);
        research.setText(complete ? _("Research complete") : _("Research level ") + std::to_string(target));
        // Paid-upfront timers have no further price. Old saves never stored their
        // original quote, so today's quote must not be presented as another bill.
        research.setPrice(active && !installments ? 0 : cost);
        research.setEnabled(canControl && owner->canStartTechResearch(objectID));
        const bool showQuote = active || (options.researchLab && !complete);
        total.setText(!showQuote ? "" : active && !installments ? _("Paid upfront")
            : _("Total: ") + std::to_string(cost) + _(" cr"));
        drain.setText(!showQuote ? "" : active && !installments ? _("No further charge")
            : installments ? _("Drain: ~") + drainText(cost, duration) + _(" cr/s") : _("Upfront payment"));
        paid.setText(active && installments ? _("Paid: ") + creditText(owner->getTechResearchPaid()) : "");
        remaining.setText(duration > 0 && showQuote
            ? (active ? _("Left: ") + timeText(owner->getTechResearchRemaining())
                      : _("Time: ") + timeText(duration)) : "");
        access.setText(active && !canControl ? _("View only")
            : active && owner->getTechResearchLabID() != static_cast<Uint32>(objectID) ? _("Other lab active") : "");
        if(active) {
            const int percent = duration > 0 ? std::clamp(static_cast<int>(
                (std::int64_t(duration) - owner->getTechResearchRemaining()) * 100 / duration), 0, 100) : 0;
            research.setStatus(std::to_string(percent) + " %");
            status.setText(owner->isTechResearchPaused() ? _("Paused: no credits") : _("Researching"));
        } else {
            const bool affordable = owner->getStartingCredits() + owner->getEarnedCredits() >= FixPoint(cost);
            research.setStatus(complete ? _("Done") : "");
            status.setText(complete ? _("All tech unlocked")
                : !options.researchLab ? _("Research disabled")
                : !canControl ? _("View only")
                : !lab->isActive() ? _("Deploy this lab")
                : !installments && !affordable ? _("Needs full price")
                : _("Ready to research"));
        }
        if(displayedTarget != target || displayedCost != cost || displayedDuration != duration
           || displayedActive != active || displayedInstallments != installments) {
            displayedTarget = target;
            displayedCost = cost;
            displayedDuration = duration;
            displayedActive = active;
            displayedInstallments = installments;
            research.setTooltipText(complete ? _("All technology unlocked")
                : active && !installments ? _("Paid upfront. No further charge. Lab lost: no refund.")
                : std::to_string(cost) + _(" cr, ")
                    + std::to_string((std::int64_t(duration) * GAMESPEED_DEFAULT + 999) / 1000)
                    + _(" game s. ")
                    + (installments ? _("Pay per tick; pauses without credits. ") : _("Paid upfront. "))
                    + _("Lab lost: no refund."));
        }
        return DefaultStructureInterface::update();
    }

private:
    static std::string timeText(int cycles) {
        const auto seconds = (std::int64_t(cycles) * GAMESPEED_DEFAULT + 999) / 1000;
        return std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") + std::to_string(seconds % 60);
    }
    static std::string decimalText(std::int64_t hundredths) {
        return std::to_string(hundredths / 100) + "."
            + (hundredths % 100 < 10 ? "0" : "") + std::to_string(hundredths % 100);
    }
    static std::string creditText(FixPoint value) {
        const auto raw = std::max<std::int64_t>(0, value.getRawValue());
        const auto unit = FixPoint(1).getRawValue();
        return decimalText((raw / unit) * 100 + (raw % unit) * 100 / unit);
    }
    static std::string drainText(int cost, int duration) {
        return decimalText(duration > 0
            ? std::int64_t(cost) * 100000 / (std::int64_t(duration) * GAMESPEED_DEFAULT) : 0);
    }
    VBox box;
    Label level;
    ResearchButton research;
    Label total;
    Label drain;
    Label paid;
    Label status;
    Label remaining;
    Label access;
    int displayedTarget = -1;
    int displayedCost = -1, displayedDuration = -1;
    bool displayedActive = false, displayedInstallments = false;
};

#endif
