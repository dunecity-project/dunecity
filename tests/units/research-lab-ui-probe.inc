// Test-only real-window helpers included by research-lab-probe.inc with -fno-access-control.
#include <FileClasses/LoadSavePNG.h>
#include <Game.h>
#include <GameInterface.h>
#include <GUI/ObjectInterfaces/BuilderInterface.h>
#include <GUI/ObjectInterfaces/ResearchLabInterface.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <House.h>
#include <misc/draw_util.h>
#include <structures/BuilderBase.h>
#include <structures/ResearchLab.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace ResearchLabUIProbe {

void require(bool value, const std::string& why) {
    if(!value) {
        SDL_Log("RESEARCH_UI_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

void viewport() {
    int width = 0, height = 0;
    require(renderer && SDL_GetRendererOutputSize(renderer, &width, &height) == 0,
            "Research UI has no renderer");
    require(width == 640 && height == 480 && getRendererWidth() == 640 && getRendererHeight() == 480,
            "Research UI requires a real 640x480 renderer, not a resized screenshot");
}

std::string capture(const std::string& filename) {
    const char* out = std::getenv("RESEARCH_LAB_PROBE_OUT");
    require(out && *out, "Research UI has no receipt directory");
    auto image = renderReadSurface(renderer);
    require(image && image->w == 640 && image->h == 480, "Research UI capture is not 640x480");
    const std::string path = std::string(out) + "/" + filename;
    require(SavePNG(image.get(), path.c_str()) == 0, "Could not save " + path);
    return path;
}

SDL_Rect bounds(Widget& control, Widget& root, Point origin = Point(0, 0)) {
    for(Widget* current = &control; current != &root;) {
        auto* parent = current->getParent();
        require(parent != nullptr, "Research UI control has no root");
        if(auto* box = dynamic_cast<StaticContainer*>(parent)) origin = origin + box->getWidgetPosition(current);
        else if(auto* box = dynamic_cast<HBox*>(parent)) origin = origin + box->getWidgetPosition(current);
        else if(auto* box = dynamic_cast<VBox*>(parent)) origin = origin + box->getWidgetPosition(current);
        else require(parent == &root, "Research UI has an unknown layout container");
        current = parent;
    }
    return SDL_Rect{origin.x, origin.y, control.getSize().x, control.getSize().y};
}

void inside(const SDL_Rect& inner, const SDL_Rect& outer, const char* why) {
    require(inner.w > 0 && inner.h > 0 && inner.x >= outer.x && inner.y >= outer.y
            && inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h, why);
}

ResearchLabInterface* select(Game& game, ResearchLab* lab) {
    require(currentGame == &game && lab && game.pInterface, "Research UI has no current lab/interface");
    require(pLocalHouse == lab->getOwner() && pLocalPlayer
            && pLocalPlayer->getHouse() == pLocalHouse
            && game.getPlayerByID(pLocalPlayer->getPlayerID()) == pLocalPlayer
            && game.canControlHouse(pLocalHouse),
            "Research UI fixture did not seat the lab's registered human controller");
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    game.selectedList.insert(lab->getObjectID());
    lab->setSelected(true);
    game.selectionChanged();
    // selectionChanged() creates the real panel; the next sidebar pass updates it.
    game.pInterface->updateObjectInterface();
    auto* panel = dynamic_cast<ResearchLabInterface*>(game.pInterface->pObjectContainer);
    require(panel && panel->update(), "Real selection did not open the Research Lab panel");
    return panel;
}

size_t commandCount(const Game& game) {
    size_t count = 0;
    for(const auto& [cycle,slot] : game.cmdManager.timeslot) count += slot.size();
    return count;
}

std::string hundredths(std::int64_t value) {
    return std::to_string(value / 100) + "." + (value % 100 < 10 ? "0" : "") + std::to_string(value % 100);
}

std::string paidText(FixPoint paid) {
    const auto unit = FixPoint(1).getRawValue();
    const auto raw = paid.getRawValue();
    return hundredths((raw / unit) * 100 + (raw % unit) * 100 / unit);
}

std::string timeText(int cycles) {
    const auto seconds = (std::int64_t(cycles) * GAMESPEED_DEFAULT + 999) / 1000;
    return std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") + std::to_string(seconds % 60);
}

void layout(Game& game, ResearchLabInterface& panel) {
    const SDL_Rect screen{0, 0, 640, 480};
    auto& root = game.pInterface->windowWidget;
    const Point origin = game.pInterface->getPosition();
    const auto panelBounds = bounds(panel, root, origin);
    inside(panelBounds, screen, "Research Lab panel extends outside the minimum viewport");
    require(panel.getSize().x == SIDEBARWIDTH - 25, "Research Lab panel widened the real sidebar");
    require(panel.box.getMinimumSize().x <= panel.mainHBox.getSize().x
            && panel.box.getMinimumSize().y <= panel.mainHBox.getSize().y,
            "Research Lab content needs more space than the real sidebar provides");
    const auto button = bounds(panel.research, root, origin);
    inside(button, panelBounds, "Research button extends outside the sidebar");
    const int titleWidth = pFontManager->getTextWidth(panel.research.getText(), 12);
    require(titleWidth <= panel.research.getSize().x - 8, "Research title needs an unreadably small font");
    require(pFontManager->getTextWidth(panel.research.status, 12)
                + pFontManager->getTextWidth(std::to_string(panel.research.price), 12) + 12 <= button.w,
            "Research progress and price overlap or truncate each other");
    int lastBottom = bounds(panel.level, root, origin).y;
    for(Label* label : {&panel.level, &panel.total, &panel.drain, &panel.paid,
                        &panel.status, &panel.remaining, &panel.access}) {
        const auto rect = bounds(*label, root, origin);
        inside(rect, panelBounds, "Research label extends outside the sidebar");
        require(rect.y >= lastBottom, "Research labels overlap");
        lastBottom = rect.y + rect.h;
        if(label == &panel.level) {
            require(lastBottom <= button.y, "Research level overlaps the start button");
            lastBottom = button.y + button.h;
        }
        if(!label->getText().empty()) {
            require(label->getTextFontSize() >= 11
                    && label->getMinimumSize().x <= rect.w && label->getMinimumSize().y <= rect.h,
                    "Research label wraps, clips or truncates a key value");
        }
    }
}

} // namespace ResearchLabUIProbe

void researchUIProbe(Game& game, ResearchLab* lab, const char* state) {
    using namespace ResearchLabUIProbe;
    viewport();
    auto* panel = select(game, lab);
    const auto* owner = lab->getOwner();
    const auto& rules = game.getGameInitSettings().getGameOptions();
    const bool active = owner->isTechResearchActive();
    const bool installment = active ? owner->isTechResearchInstallment() : rules.technologyInstallments;
    const int cost = active ? owner->getTechResearchTotalCost() : owner->getTechResearchCost();
    const int duration = active ? owner->getTechResearchTotalCycles()
        : owner->getTechLevel() >= 9 ? 0 : owner->techResearchCycles(owner->getTechLevel() + 1);
    require(panel->research.isVisible() == (rules.researchLab || active), "Research button visibility is stale");
    require(panel->research.isEnabled() == (game.canControlHouse(owner) && owner->canStartTechResearch(lab->getObjectID())),
            "Start button disagrees with the real human command eligibility");
    if(active) {
        require(duration > 0, "Active research has no captured duration");
        const int percent = std::clamp(static_cast<int>(
            (std::int64_t(duration) - owner->getTechResearchRemaining()) * 100 / duration), 0, 100);
        require(panel->research.status == std::to_string(percent) + " %", "Progress was not read from the captured timer");
        require(panel->remaining.getText() == _("Left: ") + timeText(owner->getTechResearchRemaining()),
                "Remaining research time differs from the engine");
        require(panel->status.getText() == (owner->isTechResearchPaused() ? _("Paused: no credits") : _("Researching")),
                "Research pause state differs from the real next-payment affordability");
        if(installment) {
            require(panel->paid.getText() == _("Paid: ") + paidText(owner->getTechResearchPaid()),
                    "Displayed paid amount differs from exact engine credit units");
        } else {
            require(panel->research.price == 0 && panel->total.getText() == _("Paid upfront")
                    && panel->drain.getText() == _("No further charge") && panel->paid.getText().empty(),
                    "Legacy/upfront study is presented as another charge");
        }
    } else {
        require(panel->paid.getText().empty(), "An idle lab retained a previous study's paid amount");
        require(panel->research.status == (owner->getTechLevel() >= 9 ? _("Done") : ""),
                "An idle lab retained a previous study's progress");
        if(rules.researchLab && owner->getTechLevel() < 9 && installment && game.canControlHouse(owner) && lab->isActive()) {
            require(panel->research.isEnabled() && panel->status.getText() == _("Ready to research"),
                    "Installment research is incorrectly blocked by the current cash balance");
        }
    }
    if((active && installment) || (!active && rules.researchLab && owner->getTechLevel() < 9)) {
        require(panel->research.price == cost
                && panel->total.getText() == _("Total: ") + std::to_string(cost) + _(" cr"),
                "Research total is not the captured/next-level engine price");
        const auto rate = duration > 0
            ? std::int64_t(cost) * 100000 / (std::int64_t(duration) * GAMESPEED_DEFAULT) : 0;
        require(panel->drain.getText() == (installment
                    ? _("Drain: ~") + hundredths(rate) + _(" cr/s") : _("Upfront payment")),
                "Research nominal drain differs from the captured/next-level quote");
    }
    layout(game, *panel);
    SDL_SetRenderTarget(renderer, nullptr);
    game.drawScreen();
    const auto path = capture(std::string("research-ui-") + state + "-640x480.png");
    const auto rect = bounds(panel->research, game.pInterface->windowWidget, game.pInterface->getPosition());
    SDL_Log("RESEARCH_UI_PROBE: state=%s viewport=640x480 active=%d installment=%d paused=%d enabled=%d total=%d cycles=%d remaining=%d paid_raw=%lld button=%d,%d,%d,%d screenshot=%s",
        state, int(active), int(installment), int(owner->isTechResearchPaused()), int(panel->research.isEnabled()),
        cost, duration, owner->getTechResearchRemaining(), static_cast<long long>(owner->getTechResearchPaid().getRawValue()),
        rect.x, rect.y, rect.w, rect.h, path.c_str());
}

void researchUIStart(Game& game, ResearchLab* lab) {
    using namespace ResearchLabUIProbe;
    viewport();
    auto* panel = select(game, lab);
    const auto* owner = lab->getOwner();
    require(panel->research.isVisible() && panel->research.isEnabled(), "Real human research button is not clickable");
    const auto commands = commandCount(game);
    const auto cash = owner->getStartingCredits() + owner->getEarnedCredits();
    const auto paid = owner->getTechResearchPaid();
    const int remaining = owner->getTechResearchRemaining();
    const bool active = owner->isTechResearchActive();
    panel->research.handleMouseLeft(5, 5, true);
    panel->research.handleMouseLeft(5, 5, false);
    require(commandCount(game) == commands + 1, "Research button did not enqueue exactly one ordinary human command");
    require((owner->getStartingCredits() + owner->getEarnedCredits()) == cash
            && owner->getTechResearchPaid() == paid && owner->getTechResearchRemaining() == remaining
            && owner->isTechResearchActive() == active,
            "Research UI bypassed command processing or charged immediately");
    SDL_Log("RESEARCH_UI_COMMAND: queued=1 direct_charge=0 cash_raw=%lld lab=%u",
            static_cast<long long>(cash.getRawValue()), lab->getObjectID());
}

void researchUIOptionsProbe(Game& game) {
    using namespace ResearchLabUIProbe;
    viewport();
    SettingsClass::GameOptionsClass defaults;
    require(!defaults.technologyInstallments, "New-game technology installments do not default off");
    auto rules = game.getGameInitSettings().getGameOptions();
    GameOptionsWindow window(rules);
    require(window.technologyInstallmentsCheckbox.isChecked() == rules.technologyInstallments,
            "Game Rules ignored the supplied technology payment default");
    require(window.vbox.getMinimumSize().x <= window.getSize().x
            && window.vbox.getMinimumSize().y <= window.getSize().y,
            "Adding Technology Installments overflowed the Game Rules layout");
    const auto rect = bounds(window.technologyInstallmentsCheckbox, window, window.getPosition());
    inside(rect, SDL_Rect{0, 0, 640, 480}, "Technology Installments checkbox is outside 640x480");
    require(window.technologyInstallmentsCheckbox.getMinimumSize().x <= rect.w
            && window.technologyInstallmentsCheckbox.getMinimumSize().y <= rect.h,
            "Technology Installments checkbox text is clipped");
    const SDL_Rect crests[] = {{11, 11, 53, 54}, {576, 11, 53, 54}, {11, 415, 53, 54}, {576, 415, 53, 54}};
    for(const auto& crest : crests)
        require(!SDL_HasIntersection(&rect, &crest), "Technology Installments overlaps a corner crest");
    SDL_SetRenderTarget(renderer, nullptr);
    game.drawScreen();
    window.draw();
    const auto path = capture("research-ui-options-640x480.png");
    const auto commands = commandCount(game);
    window.technologyInstallmentsCheckbox.handleMouseLeft(5, 5, true);
    window.technologyInstallmentsCheckbox.handleMouseLeft(5, 5, false);
    auto expected = rules;
    expected.technologyInstallments = !rules.technologyInstallments;
    require(window.technologyInstallmentsCheckbox.isChecked() == expected.technologyInstallments,
            "Technology Installments checkbox did not respond to a real click");
    window.onOK();
    require(window.getGameOptions() == expected, "Technology Installments editing changed another game option");
    auto chosen = window.getGameOptions();
    GameOptionsWindow reopened(chosen);
    require(reopened.technologyInstallmentsCheckbox.isChecked() == expected.technologyInstallments,
            "Technology Installments was lost when reopening Game Rules");
    require(game.getGameInitSettings().getGameOptions() == rules && commandCount(game) == commands,
            "Inspecting/editing a detached rules dialog changed the running match");
    SDL_Log("RESEARCH_UI_OPTIONS: viewport=640x480 default=0 initial=%d selected=%d checkbox=%d,%d,%d,%d screenshot=%s",
        int(rules.technologyInstallments), int(expected.technologyInstallments), rect.x, rect.y, rect.w, rect.h, path.c_str());
}

namespace ResearchLabUIProbe {

BuilderInterface* selectUpgrade(Game& game, BuilderBase* builder) {
    require(currentGame == &game && builder && game.pInterface, "Upgrade UI has no current builder/interface");
    require(pLocalHouse == builder->getOwner() && pLocalPlayer
            && pLocalPlayer->getHouse() == pLocalHouse
            && game.getPlayerByID(pLocalPlayer->getPlayerID()) == pLocalPlayer
            && game.canControlHouse(pLocalHouse),
            "Upgrade UI fixture did not seat the builder's registered human controller");
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    game.selectedList.insert(builder->getObjectID());
    builder->setSelected(true);
    game.selectionChanged();
    // As in normal frames, update only after selectionChanged() attaches the panel.
    game.pInterface->updateObjectInterface();
    auto* panel = dynamic_cast<BuilderInterface*>(game.pInterface->pObjectContainer);
    require(panel && panel->update(), "Real selection did not open the builder's upgrade panel");
    return panel;
}

void upgradeLayout(Game& game, BuilderInterface& panel) {
    auto& root = game.pInterface->windowWidget;
    const Point origin = game.pInterface->getPosition();
    const auto panelBounds = bounds(panel, root, origin);
    inside(panelBounds, SDL_Rect{0, 0, 640, 480}, "Upgrade panel extends outside the minimum viewport");
    require(panel.getSize().x == SIDEBARWIDTH - 25 && panel.mainHBox.getSize().y == 480 - 80 - 148,
            "Upgrade details widened the sidebar or stole production-list rows");
    const auto top = bounds(panel.topBox, root, origin);
    SDL_Rect visible[8];
    size_t count = 0;
    for(Widget* control : {static_cast<Widget*>(&panel.upgradeButton),
                          static_cast<Widget*>(&panel.upgradeProgressBar),
                          static_cast<Widget*>(&panel.upgradeStatusLabel),
                          static_cast<Widget*>(&panel.upgradeTotalLabel),
                          static_cast<Widget*>(&panel.upgradePaidLabel),
                          static_cast<Widget*>(&panel.repairButton),
                          static_cast<Widget*>(&panel.sellButton),
                          static_cast<Widget*>(&panel.destroyButton)}) {
        if(!control->isVisible()) continue;
        const auto rect = bounds(*control, root, origin);
        inside(rect, top, "An upgrade control extends outside the builder's top strip");
        for(size_t index = 0; index < count; ++index)
            require(!SDL_HasIntersection(&rect, &visible[index]), "Upgrade, repair, sell or payment controls overlap");
        visible[count++] = rect;
        require(control->getMinimumSize().x <= rect.w && control->getMinimumSize().y <= rect.h,
                "An upgrade control clips or wraps its key value");
    }
    for(Label* label : {&panel.upgradeStatusLabel, &panel.upgradeTotalLabel, &panel.upgradePaidLabel})
        require(label->getTextFontSize() >= 11, "Upgrade payment status uses unreadably small text");
}

} // namespace ResearchLabUIProbe

void upgradeUIProbe(Game& game, BuilderBase* builder, const char* state) {
    using namespace ResearchLabUIProbe;
    viewport();
    require(builder != nullptr, "Upgrade UI has no builder");
    const bool held = builder->isOnHold();
    auto* panel = selectUpgrade(game, builder);
    const auto* owner = builder->getOwner();
    const bool active = builder->isUpgrading();
    const bool canControl = game.canControlHouse(owner);
    const bool healthy = builder->getHealth() >= builder->getMaxHealth();
    const int cost = builder->getUpgradeCost();
    require(panel->upgradeProgressBar.isVisible() == active
            && panel->upgradeStatusLabel.isVisible() == active
            && panel->upgradeTotalLabel.isVisible() == active
            && panel->upgradePaidLabel.isVisible() == active && panel->objPicture.isVisible() == !active,
            "Upgrade progress and portrait/payment visibility disagree with the engine");
    require(panel->upgradeButton.isVisible() == (canControl && healthy && !active && builder->isAllowedToUpgrade())
            && panel->upgradeButton.isEnabled() == (canControl && healthy && builder->canStartUpgrade()),
            "Upgrade button ignores real eligibility, ownership, health or payment rules");
    if(active) {
        require(cost > 0, "Active upgrade has no price");
        const FixPoint percent = builder->getUpgradeProgress() * 100 / cost;
        require(panel->upgradeProgressBar.getProgress() == percent.toDouble()
                && panel->upgradeProgressBar.getText() == std::to_string(std::clamp(percent.floor(), 0, 100)) + " %",
                "Upgrade progress bar differs from funded engine progress");
        require(panel->upgradeStatusLabel.getText() == (builder->isUpgradePaused() ? _("Paused: no credits") : _("Upgrading")),
                "Upgrade pause status differs from effective engine payment availability");
        require(panel->upgradeTotalLabel.getText() == _("Total: ") + std::to_string(cost) + _(" cr")
                && panel->upgradePaidLabel.getText() == _("Paid: ") + paidText(builder->getUpgradeProgress()),
                "Upgrade total/paid values differ from exact engine credits");
    } else {
        require(panel->upgradeProgressBar.getProgress() == 0 && panel->upgradeStatusLabel.getText().empty()
                && panel->upgradeTotalLabel.getText().empty() && panel->upgradePaidLabel.getText().empty(),
                "Idle/completed upgrade retained the previous upgrade's payment/progress");
    }
    upgradeLayout(game, *panel);
    SDL_SetRenderTarget(renderer, nullptr);
    game.drawScreen();
    require(builder->isOnHold() == held, "Rendering upgrade status changed the production hold");
    const auto path = capture(std::string("upgrade-ui-") + state + "-640x480.png");
    Widget* control = active ? static_cast<Widget*>(&panel->upgradeProgressBar) : static_cast<Widget*>(&panel->upgradeButton);
    const auto rect = bounds(*control, game.pInterface->windowWidget, game.pInterface->getPosition());
    const auto cash = owner->getStartingCredits() + owner->getEarnedCredits();
    SDL_Log("UPGRADE_UI_PROBE: state=%s viewport=640x480 item=%u active=%d paused=%d enabled=%d level=%d cost=%d paid_raw=%lld cash_raw=%lld on_hold=%d control=%d,%d,%d,%d screenshot=%s",
        state, builder->getItemID(), int(active), int(builder->isUpgradePaused()), int(panel->upgradeButton.isEnabled()),
        builder->getCurrentUpgradeLevel(), cost, static_cast<long long>(builder->getUpgradeProgress().getRawValue()),
        static_cast<long long>(cash.getRawValue()), int(held), rect.x, rect.y, rect.w, rect.h, path.c_str());
}

void upgradeUIStart(Game& game, BuilderBase* builder) {
    using namespace ResearchLabUIProbe;
    viewport();
    auto* panel = selectUpgrade(game, builder);
    const auto* owner = builder->getOwner();
    require(panel->upgradeButton.isVisible() && panel->upgradeButton.isEnabled(),
            "Real human Upgrade button is not clickable");
    const auto commands = commandCount(game);
    const auto cash = owner->getStartingCredits() + owner->getEarnedCredits();
    const auto paid = builder->getUpgradeProgress();
    const int level = builder->getCurrentUpgradeLevel();
    const bool active = builder->isUpgrading();
    const bool held = builder->isOnHold();
    panel->upgradeButton.handleMouseLeft(5, 5, true);
    panel->upgradeButton.handleMouseLeft(5, 5, false);
    require(commandCount(game) == commands + 1, "Upgrade button did not enqueue exactly one ordinary human command");
    require((owner->getStartingCredits() + owner->getEarnedCredits()) == cash
            && builder->getUpgradeProgress() == paid && builder->getCurrentUpgradeLevel() == level
            && builder->isUpgrading() == active && builder->isOnHold() == held,
            "Upgrade UI bypassed command processing, charged immediately or changed the production hold");
    SDL_Log("UPGRADE_UI_COMMAND: queued=1 direct_charge=0 cash_raw=%lld item=%u builder=%u on_hold=%d",
        static_cast<long long>(cash.getRawValue()), builder->getItemID(), builder->getObjectID(), int(held));
}
