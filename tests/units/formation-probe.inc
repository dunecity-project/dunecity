// Headless integration fixture for formation movement (docs/formations.md); linked with the
// production game objects by run-formation-probe.py, never part of the game binary.
//
// Twelve mixed units are moved across open sand through the real player path
// (Game::handleSelectedObjectsActionClick -> commands -> executor -> game loop), once with
// "Move groups in formation" on and once off, from identical starting positions. A hunting
// attack-move saved mid-march is reloaded, and 9854 and 9857 unit records are loaded into the
// current build. Then the shapes: a Pincer march with sonic tanks (no unit
// ever steps into a sonic lane), Line and Column frontage and depth, and a hunt in formation
// that breaks into a fight at the first contact with an enemy group. Last, the formation
// buttons and Ctrl+Shift+F form a scattered group up where it stands (Game::chooseFormation).
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <ScreenBorder.h>
#include <FormationOrders.h>
#include <GameInterface.h>
#include <GUI/ObjectInterfaces/MultiUnitInterface.h>
#include <GUI/ObjectInterfaces/FormationBar.h>
#include <FileClasses/LoadSavePNG.h>
#include <misc/draw_util.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <units/Tank.h>
#include <units/UnitBase.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

namespace FormationProbe {

void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FORMATION_PROBE_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

struct Start { Uint32 item; Coord tile; };

/// Twelve units, scattered over more than the coherent radius on the west side.
const std::vector<Start> kArmy = {
    {Unit_Tank, Coord(5, 8)},      {Unit_Tank, Coord(8, 13)},    {Unit_Tank, Coord(4, 18)},
    {Unit_Tank, Coord(7, 22)},     {Unit_Launcher, Coord(3, 10)}, {Unit_Launcher, Coord(6, 16)},
    {Unit_Launcher, Coord(9, 19)}, {Unit_Launcher, Coord(5, 24)}, {Unit_SiegeTank, Coord(9, 9)},
    {Unit_SiegeTank, Coord(4, 14)}, {Unit_Quad, Coord(10, 12)},  {Unit_Quad, Coord(6, 20)},
};
const Coord kTarget(42, 16);
/// A member has arrived once it is this close to the clicked tile (the same test with and
/// without formations: a formation slot is within this of the target, and without one the
/// group piles up around it).
constexpr int kArrivalTiles = 4;
constexpr Uint32 kTimeoutCycles = 7000;

struct Outcome {
    std::map<Uint32, Uint32> arrival;   ///< cycles from the click
    std::map<Uint32, Coord> ordered;    ///< destination each unit was given
    std::map<Uint32, Coord> final;
    Uint32 spread = 0;
};

double seconds(Uint32 cycles) { return cycles * GAMESPEED_DEFAULT / 1000.0; }

std::vector<UnitBase*> deployArmy(House* owner) {
    std::vector<UnitBase*> army;
    for(const auto& start : kArmy) {
        UnitBase* unit = owner->createUnit(start.item, true);
        require(unit != nullptr, "Could not create a formation fixture unit");
        unit->deploy(start.tile);
        require(unit->getLocation() == start.tile, "A fixture unit did not stand on its start tile");
        unit->angle = RIGHT; unit->drawnAngle = RIGHT;
        army.push_back(unit);
    }
    return army;
}

void clearArmy(std::vector<UnitBase*>& army) {
    for(UnitBase* unit : army) unit->cancelDeployment();
    army.clear();
}

/// Selects the army and right-clicks the target through the real player path.
void order(Game& game, const std::vector<UnitBase*>& army, bool formations) {
    settings.general.formationMoves = formations;
    settings.general.formationShape = static_cast<int>(FormationPlanner::Shape::Box);
    game.selectedList.clear();
    for(const UnitBase* unit : army) game.selectedList.insert(unit->getObjectID());
    require(game.handleSelectedObjectsActionClick(kTarget.x, kTarget.y), "The right click was not accepted");
}

void saveScreenshot(Game& game, const char* name, Coord centre = Coord(26, 16)) {
    screenborder->setNewScreenCenter(Coord(centre.x * TILESIZE, centre.y * TILESIZE));
    SDL_SetRenderTarget(renderer, screenTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    game.drawScreen();
    auto image = renderReadSurface(renderer);
    SDL_SetRenderTarget(renderer, nullptr);
    require(image != nullptr, "Could not read back the rendered map");
    SavePNG(image.get(), name);
}

Outcome march(Game& game, const std::vector<UnitBase*>& army, bool formations, const char* screenshot) {
    Outcome outcome;
    const Uint32 clicked = game.getGameCycleCount();
    order(game, army, formations);
    bool captured = false, shot = false;
    while(game.getGameCycleCount() - clicked < kTimeoutCycles) {
        game.updateGameState();
        const Uint32 now = game.getGameCycleCount() - clicked;
        if(!captured) {
            bool all = true;
            for(const UnitBase* unit : army) all = all && unit->getDestination() != unit->getLocation();
            if(all) {
                captured = true;
                // With formations the slot, not the form-up tile the member walks to first.
                for(const UnitBase* unit : army)
                    outcome.ordered[unit->getObjectID()] = unit->isInFormation() ? unit->getFormationSlot() : unit->getDestination();
            }
        }
        for(const UnitBase* unit : army)
            if(!outcome.arrival.count(unit->getObjectID())
               && FormationPlanner::tileDistance(unit->getLocation(), kTarget) <= kArrivalTiles)
                outcome.arrival[unit->getObjectID()] = now;
        if(screenshot && !shot && now >= MILLI2CYCLES(14000)) { saveScreenshot(game, screenshot); shot = true; }
        if(outcome.arrival.size() == army.size()) {
            // With formations, wait for every member to stand on its own slot: a member can
            // reach the area behind a row that arrived first and still has to walk round it.
            bool settled = true;
            for(const UnitBase* unit : army)
                settled = settled && !unit->isMoving()
                    && (!formations || unit->getLocation() == outcome.ordered.at(unit->getObjectID()));
            if(settled && now > outcome.arrival.rbegin()->second + 120) break;
        }
    }
    require(captured, "The order never reached the units");
    for(const UnitBase* unit : army) outcome.final[unit->getObjectID()] = unit->getLocation();
    Uint32 first = UINT32_MAX, last = 0;
    for(const auto& [id, cycle] : outcome.arrival) { first = std::min(first, cycle); last = std::max(last, cycle); }
    outcome.spread = outcome.arrival.empty() ? 0 : last - first;
    return outcome;
}

void report(const char* label, const Outcome& outcome, const std::vector<UnitBase*>& army) {
    SDL_Log("FORMATION_PROBE: %s arrived=%zu/%zu first=%.2fs last=%.2fs spread=%.2fs", label,
            outcome.arrival.size(), army.size(),
            outcome.arrival.empty() ? -1.0 : seconds(std::min_element(outcome.arrival.begin(), outcome.arrival.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; })->second),
            outcome.arrival.empty() ? -1.0 : seconds(std::max_element(outcome.arrival.begin(), outcome.arrival.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; })->second),
            seconds(outcome.spread));
    for(const UnitBase* unit : army) {
        const Uint32 id = unit->getObjectID();
        SDL_Log("FORMATION_PROBE: %s id=%u item=%u ordered=%d,%d final=%d,%d arrival=%.2fs", label, id,
                unit->getItemID(), outcome.ordered.at(id).x, outcome.ordered.at(id).y,
                outcome.final.at(id).x, outcome.final.at(id).y,
                outcome.arrival.count(id) ? seconds(outcome.arrival.at(id)) : -1.0);
    }
}


/// Removes every unit of the house from the map.
void clearHouse(House* house) {
    std::vector<UnitBase*> units;
    for(UnitBase* unit : unitList) if(unit->getOwner() == house && unit->isActive()) units.push_back(unit);
    for(UnitBase* unit : units) unit->cancelDeployment();
}

std::vector<UnitBase*> deploy(House* owner, const std::vector<Start>& starts) {
    std::vector<UnitBase*> group;
    for(const auto& start : starts) {
        UnitBase* unit = owner->createUnit(start.item, true);
        require(unit != nullptr, "Could not create a shape fixture unit");
        unit->deploy(start.tile);
        require(unit->getLocation() == start.tile, "A shape fixture unit did not stand on its start tile");
        unit->angle = RIGHT; unit->drawnAngle = RIGHT;
        group.push_back(unit);
    }
    return group;
}

/// Selects the group and right-clicks the target in the given shape.
FormationPlanner::Plan orderShape(Game& game, const std::vector<UnitBase*>& group, FormationPlanner::Shape shape, Coord target) {
    settings.general.formationMoves = true;
    settings.general.formationShape = static_cast<int>(shape);
    std::vector<const UnitBase*> members(group.begin(), group.end());
    const auto plan = FormationOrders::planGroup(members, target, shape);
    require(plan.formation && plan.slots.size() == group.size(), "The shape fixture got no formation plan");
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    for(UnitBase* unit : group) { game.selectedList.insert(unit->getObjectID()); unit->setSelected(true); }
    game.selectionChanged();
    require(game.handleSelectedObjectsActionClick(target.x, target.y), "The right click was not accepted");
    return plan;
}

/// Runs until every member stands on its slot (or the timeout), calling 'each' every cycle.
template<typename Each>
Uint32 runToSlots(Game& game, const std::vector<UnitBase*>& group, const FormationPlanner::Plan& plan, Each each,
                  Uint32 timeout = kTimeoutCycles) {
    Uint32 cycles = 0;
    while(cycles < timeout) {
        game.updateGameState();
        ++cycles;
        each(cycles);
        bool settled = true;
        for(const UnitBase* unit : group)
            settled = settled && !unit->isMoving() && unit->getLocation() == plan.slotFor(unit->getObjectID());
        if(settled) break;
    }
    for(const UnitBase* unit : group) {
        if(unit->getLocation() != plan.slotFor(unit->getObjectID()))
            SDL_Log("FORMATION_PROBE: id=%u item=%u at %d,%d slot %d,%d moving=%d inFormation=%d forming=%d", unit->getObjectID(),
                    unit->getItemID(), unit->getLocation().x, unit->getLocation().y, plan.slotFor(unit->getObjectID()).x,
                    plan.slotFor(unit->getObjectID()).y, unit->isMoving(), unit->isInFormation(), unit->isFormationForming());
        require(unit->getLocation() == plan.slotFor(unit->getObjectID()), "A shape member did not reach its slot");
    }
    return cycles;
}

/// The sonic units' lane check, using the group's facing: steps into a sonic lane by a unit
/// that may not stand there (anything but a sonic unit of the same type).
int countLaneEntries(const std::vector<UnitBase*>& group, const Coord& forward, std::set<std::pair<Uint32, Uint32>>& inLane,
                     std::string& detail) {
    int entries = 0;
    for(const UnitBase* sonic : group) {
        if(!FormationPlanner::isSonic(sonic->getItemID())) continue;
        for(const UnitBase* other : group) {
            if(other == sonic || FormationPlanner::mayStandInLane(sonic->getItemID(), other->getItemID())) continue;
            const auto key = std::make_pair(sonic->getObjectID(), other->getObjectID());
            const bool now = FormationPlanner::inSonicLane(sonic->getLocation(), other->getLocation(), forward);
            // A step into the lane counts while both are on the march (formed up).
            if(now && !inLane.count(key) && sonic->isInFormation() && other->isInFormation()
               && !sonic->isFormationForming() && !other->isFormationForming()) {
                ++entries;
                detail += " " + std::to_string(other->getObjectID()) + "@" + std::to_string(other->getLocation().x) + ","
                    + std::to_string(other->getLocation().y) + "->sonic" + std::to_string(sonic->getObjectID()) + "@"
                    + std::to_string(sonic->getLocation().x) + "," + std::to_string(sonic->getLocation().y);
            }
            if(now) inLane.insert(key); else inLane.erase(key);
        }
    }
    return entries;
}

/// How many (sonic unit, other unit) pairs of marching members have the other in the sonic
/// unit's lane although it may not stand there.
int forbiddenInLane(const std::vector<UnitBase*>& group, const Coord& forward) {
    int pairs = 0;
    for(const UnitBase* sonic : group) {
        if(!FormationPlanner::isSonic(sonic->getItemID()) || !sonic->isInFormation() || sonic->isFormationForming()) continue;
        for(const UnitBase* other : group)
            if(other != sonic && other->isInFormation() && !FormationPlanner::mayStandInLane(sonic->getItemID(), other->getItemID())
               && FormationPlanner::inSonicLane(sonic->getLocation(), other->getLocation(), forward)) ++pairs;
    }
    return pairs;
}

/// A mixed army with two sonic tanks in the middle of it, scattered on the west side.
const std::vector<Start> kCombinedArmy = {
    {Unit_SonicTank, Coord(5, 14)}, {Unit_SonicTank, Coord(6, 18)}, {Unit_Tank, Coord(4, 9)},
    {Unit_Tank, Coord(8, 12)},      {Unit_Tank, Coord(3, 22)},      {Unit_Launcher, Coord(2, 11)},
    {Unit_Launcher, Coord(5, 20)},  {Unit_Launcher, Coord(9, 24)},  {Unit_SiegeTank, Coord(3, 16)},
    {Unit_Quad, Coord(10, 8)},      {Unit_Quad, Coord(9, 21)},      {Unit_Trike, Coord(7, 6)},
    {Unit_Trike, Coord(11, 16)},
};
const Coord kCombinedTarget(38, 16);

struct CombinedOutcome { int laneStepIns = 0; int wingGap = 0; };

CombinedOutcome marchCombined(Game& game, House* owner) {
    CombinedOutcome outcome;
    auto group = deploy(owner, kCombinedArmy);
    for(int i = 0; i < 5; ++i) game.updateGameState();     // settle visibility before the order
    // The Pincer with sonic tanks: two wings, the sonic tanks in their front rank, nothing in a lane.
    const auto plan = orderShape(game, group, FormationPlanner::Shape::Pincer, kCombinedTarget);
    require(plan.forward == Coord(1, 0), "The pincer fixture does not face east");
    int nearest = INT32_MAX;
    for(const UnitBase* unit : group) nearest = std::min(nearest, std::abs(plan.slotFor(unit->getObjectID()).y - kCombinedTarget.y));
    require(nearest >= 2, "The pincer has no gap between its wings");
    outcome.wingGap = nearest;
    std::set<std::pair<Uint32, Uint32>> inLane;
    std::string detail;
    countLaneEntries(group, plan.forward, inLane, detail);  // where the units start counts as no step
    const size_t startInLane = inLane.size();
    bool shot = false;
    const Uint32 cycles = runToSlots(game, group, plan, [&](Uint32 now) {
        outcome.laneStepIns += countLaneEntries(group, plan.forward, inLane, detail);
        if(shot) return;
        // Frame the march with the panel once the group has formed up: every member within
        // three tiles of its slot.
        int sumX = 0, sumY = 0, farthest = 0;
        for(const UnitBase* unit : group) {
            farthest = std::max(farthest, FormationPlanner::tileDistance(unit->getLocation(), plan.slotFor(unit->getObjectID())));
            sumX += unit->getLocation().x;
            sumY += unit->getLocation().y;
        }
        const int count = static_cast<int>(group.size());
        if(farthest > 3) return;
        shot = true;
        game.pInterface->updateObjectInterface();
        auto* panel = dynamic_cast<MultiUnitInterface*>(game.pInterface->pObjectContainer);
        require(panel != nullptr, "No group panel for the pincer frame");
        require(panel->formationBar.isVisible(), "The formation buttons are not shown for a ground group");
        const int pincer = 1 + FormationPlanner::positionOf(FormationPlanner::Shape::Pincer);
        for(int choice = 0; choice < FormationBar::kChoices; ++choice)
            require(panel->formationBar.buttons[choice].getToggleState() == (choice == pincer), "The Pincer button is not the one held down");
        require(panel->formationBar.buttons[pincer].getTooltipText().rfind("Pincer formation (Ctrl+Shift+F)", 0) == 0
                && panel->formationBar.buttons[0].getTooltipText().rfind("Formations off (Ctrl+F)", 0) == 0,
                "The formation buttons' tooltips do not name their hotkeys");
        SymbolButton& hovered = panel->formationBar.buttons[pincer];
        hovered.bHover = true;
        hovered.tooltipLastMouseMotion = SDL_GetTicks() - 2000;
        // The tooltip follows the mouse: keep it below the marching group.
        drawnMouseX = getRendererWidth() - 120;
        drawnMouseY = getRendererHeight() - 60;
        const bool paths = settings.general.showMovementPaths;
        settings.general.showMovementPaths = false;
        saveScreenshot(game, "formation-pincer-panel.png", Coord(sumX / count + 3, sumY / count));
        settings.general.showMovementPaths = paths;
        hovered.bHover = false;
        SDL_Log("FORMATION_PROBE_PINCER_FRAME: %.2fs into the march", seconds(now));
    });
    require(shot, "The pincer frame was not captured");
    SDL_Log("FORMATION_PROBE_PINCER: arrived in %.2fs; %zu pairs started in a lane, %d stepped into one%s",
            seconds(cycles), startInLane, outcome.laneStepIns, detail.c_str());
    require(outcome.laneStepIns == 0, "A unit stepped into a sonic tank's lane during the march");
    for(const UnitBase* sonic : group) {
        if(!FormationPlanner::isSonic(sonic->getItemID())) continue;
        for(const UnitBase* other : group)
            require(other == sonic || !FormationPlanner::inSonicLane(sonic->getLocation(), other->getLocation(), plan.forward),
                    "A unit stands in a sonic tank's lane after the march");
    }
    clearHouse(owner);
    return outcome;
}

constexpr int kShapeGroup = 10;

struct ShapeExtent { int frontage = 0; int depth = 0; };

ShapeExtent marchShape(Game& game, House* owner, FormationPlanner::Shape shape) {
    std::vector<Start> starts;
    for(int i = 0; i < kShapeGroup; ++i) starts.push_back({Unit_Tank, Coord(2 + (i % 3) * 4, 4 + i * 2)});
    auto group = deploy(owner, starts);
    const auto plan = orderShape(game, group, shape, Coord(36, 15));
    require(plan.forward == Coord(1, 0), "The shape fixture does not face east");
    runToSlots(game, group, plan, [](Uint32) {});
    std::set<int> xs, ys;
    for(const UnitBase* unit : group) { xs.insert(unit->getLocation().x); ys.insert(unit->getLocation().y); }
    clearHouse(owner);
    return {static_cast<int>(ys.size()), static_cast<int>(xs.size())};
}

/// Sixteen mixed units with two sonic tanks, scattered on the west side: the shape gallery.
const std::vector<Start> kGalleryArmy = {
    {Unit_SonicTank, Coord(5, 13)}, {Unit_SonicTank, Coord(6, 18)}, {Unit_Tank, Coord(4, 8)},
    {Unit_Tank, Coord(8, 11)},      {Unit_Tank, Coord(3, 22)},      {Unit_Tank, Coord(9, 25)},
    {Unit_SiegeTank, Coord(3, 15)}, {Unit_SiegeTank, Coord(7, 23)}, {Unit_Launcher, Coord(2, 11)},
    {Unit_Launcher, Coord(5, 20)},  {Unit_Launcher, Coord(9, 15)},  {Unit_Quad, Coord(10, 8)},
    {Unit_Quad, Coord(10, 21)},     {Unit_Trike, Coord(7, 5)},      {Unit_Trooper, Coord(2, 18)},
    {Unit_Trooper, Coord(2, 19)},
};
const Coord kGalleryTarget(36, 15);

struct GalleryOutcome { int shapes = 0; int laneStepIns = 0; double slowest = 0; };

/// Every shape: the gallery army right-clicked east in it through the real player path, every
/// member on its slot, no step into a sonic lane on the march, and a frame of the formed group
/// with its panel button held down (formation-shape-<name>.png).
GalleryOutcome marchEveryShape(Game& game, House* owner) {
    GalleryOutcome outcome;
    for(int index = 0; index < FormationPlanner::kShapeCount; ++index) {
        const auto shape = FormationPlanner::shapeAt(index);
        auto group = deploy(owner, kGalleryArmy);
        for(int i = 0; i < 5; ++i) game.updateGameState();
        const auto plan = orderShape(game, group, shape, kGalleryTarget);
        require(plan.forward == Coord(1, 0), "The gallery fixture does not face east");
        std::set<std::pair<Uint32, Uint32>> inLane;
        std::string detail;
        countLaneEntries(group, plan.forward, inLane, detail);
        int stepIns = 0;
        SDL_Log("FORMATION_PROBE_GALLERY_START: %s", FormationPlanner::shapeName(shape));
        // The troopers set the pace: three times the usual allowance.
        const Uint32 cycles = runToSlots(game, group, plan, [&](Uint32) {
            stepIns += countLaneEntries(group, plan.forward, inLane, detail);
        }, 3 * kTimeoutCycles);
        int sumX = 0, sumY = 0;
        for(const UnitBase* unit : group) { sumX += unit->getLocation().x; sumY += unit->getLocation().y; }
        game.pInterface->updateObjectInterface();
        auto* panel = dynamic_cast<MultiUnitInterface*>(game.pInterface->pObjectContainer);
        require(panel != nullptr && panel->formationBar.isVisible(), "No formation buttons for the gallery group");
        for(int choice = 0; choice < FormationBar::kChoices; ++choice)
            require(panel->formationBar.buttons[choice].getToggleState() == (choice == index + 1),
                    "The gallery shape's button is not the one held down");
        const bool paths = settings.general.showMovementPaths;
        settings.general.showMovementPaths = false;
        std::string name = FormationPlanner::shapeName(shape);
        std::replace(name.begin(), name.end(), ' ', '-');
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
        const std::string file = "formation-shape-" + name + ".png";
        saveScreenshot(game, file.c_str(), Coord(sumX / static_cast<int>(group.size()) + 4, sumY / static_cast<int>(group.size())));
        settings.general.showMovementPaths = paths;
        SDL_Log("FORMATION_PROBE_GALLERY: %s formed in %.2fs, %d sonic lane step-ins%s, frame %s",
                FormationPlanner::shapeName(shape), seconds(cycles), stepIns, detail.c_str(), file.c_str());
        require(stepIns == 0, std::string("A unit stepped into a sonic lane marching in ") + FormationPlanner::shapeName(shape));
        outcome.laneStepIns += stepIns;
        outcome.slowest = std::max(outcome.slowest, seconds(cycles));
        ++outcome.shapes;
        clearHouse(owner);
        game.unselectAll(game.selectedList);
        game.selectedList.clear();
        for(int i = 0; i < 5; ++i) game.updateGameState();
    }
    return outcome;
}

struct HuntOutcome { Uint32 marched = 0; Uint32 engaged = 0; };

HuntOutcome huntInFormation(Game& game, House* owner, House* enemyHouse) {
    require(enemyHouse != nullptr, "No enemy house for the hunt");
    HuntOutcome outcome;
    const std::vector<Start> army = {
        {Unit_Tank, Coord(4, 12)}, {Unit_Tank, Coord(6, 17)}, {Unit_Tank, Coord(3, 20)}, {Unit_Launcher, Coord(2, 14)},
        {Unit_Launcher, Coord(5, 23)}, {Unit_SiegeTank, Coord(2, 18)}, {Unit_Quad, Coord(9, 10)}, {Unit_Quad, Coord(8, 22)},
        {Unit_Trike, Coord(10, 16)},
    };
    auto group = deploy(owner, army);
    std::vector<Uint32> ids, enemyIDs;
    for(const UnitBase* unit : group) ids.push_back(unit->getObjectID());
    for(const Coord& at : {Coord(33, 14), Coord(33, 17), Coord(34, 15), Coord(34, 16)}) {
        UnitBase* enemy = enemyHouse->createUnit(Unit_Tank, true);
        enemy->deploy(at);
        enemy->angle = LEFT; enemy->drawnAngle = LEFT;
        enemy->doSetAttackMode(GUARD);
        enemyIDs.push_back(enemy->getObjectID());
    }
    for(int i = 0; i < 5; ++i) game.updateGameState();
    // Units die in the fight: look them up by id every cycle.
    auto unitOf = [&](Uint32 id) {
        const auto* unit = dynamic_cast<const UnitBase*>(game.getObjectManager().getObject(id));
        return (unit != nullptr && unit->isActive() && unit->getHealth() > 0) ? unit : nullptr;
    };
    // The attack-move cursor click on the ground beyond the enemy: a hunt in formation.
    settings.general.formationMoves = true;
    settings.general.formationShape = static_cast<int>(FormationPlanner::Shape::Pincer);
    const Coord goal(44, 16);
    require(game.issuePositionOrder(Game::PositionOrder::AttackMove, group, goal, false) != nullptr,
            "The hunting attack-move was not issued");
    group.clear();
    Uint32 groupID = 0;
    Uint32 now = 0, broke = 0;
    std::map<Uint32, Uint32> fought;     // member -> cycles after the break when it first had a target
    std::map<Uint32, Uint32> hunting;    // member -> cycles after the break when it first fought or closed in
    int nearestAtBreak = INT32_MAX;
    while(now < kTimeoutCycles) {
        game.updateGameState();
        ++now;
        if(groupID == 0) {
            for(const Uint32 id : ids) if(const auto* unit = unitOf(id)) groupID = groupID ? groupID : unit->getFormationGroup();
            require(groupID != 0, "The attack-move did not form a hunting formation");
        }
        if(broke == 0) {
            int inFormation = 0, alive = 0;
            for(const Uint32 id : ids) {
                const auto* unit = unitOf(id);
                if(unit == nullptr) continue;
                ++alive;
                if(unit->getFormationGroup() == groupID && unit->isFormationHunt()) ++inFormation;
            }
            if(inFormation == alive) continue;
            // The first contact ends the formation for every member in the same cycle.
            require(inFormation == 0, "The hunting formation broke for some members only");
            broke = now;
            for(const Uint32 id : ids)
                for(const Uint32 enemyID : enemyIDs)
                    if(unitOf(id) && unitOf(enemyID))
                        nearestAtBreak = std::min(nearestAtBreak,
                            FormationPlanner::tileDistance(unitOf(id)->getLocation(), unitOf(enemyID)->getLocation()));
        }
        for(const Uint32 id : ids) {
            const auto* unit = unitOf(id);
            if(unit == nullptr) continue;
            if(unit->hasATarget() && !fought.count(id)) fought[id] = now - broke;
            // Hunting: closing in on an enemy (its walk ends next to one) or fighting.
            bool closing = false;
            for(const Uint32 enemyID : enemyIDs)
                if(const auto* enemy = unitOf(enemyID))
                    closing = closing || (unit->getDestination().isValid()
                        && FormationPlanner::tileDistance(unit->getDestination(), enemy->getLocation()) <= 2);
            if((unit->hasATarget() || closing) && !hunting.count(id)) hunting[id] = now - broke;
        }
        if(fought.size() == ids.size() || now - broke > MILLI2CYCLES(15000)) break;
    }
    require(broke != 0, "The hunting formation never met the enemy");
    outcome.marched = broke;
    for(const auto& [id, after] : hunting) outcome.engaged = std::max(outcome.engaged, after);
    Uint32 firing = 0;
    for(const auto& [id, after] : fought) firing = std::max(firing, after);
    size_t idle = 0, unfought = 0;
    for(const Uint32 id : ids) {
        if(!unitOf(id)) continue;
        idle += hunting.count(id) ? 0 : 1;
        unfought += fought.count(id) ? 0 : 1;
    }
    SDL_Log("FORMATION_PROBE_HUNT: marched %.2fs in formation, broke %d tiles from the nearest enemy; every member hunting "
            "within %.2fs (%zu idle), %zu/%zu with a target, the last %.2fs after the break",
            seconds(broke), nearestAtBreak, seconds(outcome.engaged), idle, fought.size(), ids.size(), seconds(firing));
    require(seconds(broke) > 5.0, "The hunting formation broke before it had marched");
    require(idle == 0 && outcome.engaged <= MILLI2CYCLES(1000), "A member did not turn on the enemy within a second of the break");
    require(unfought == 0 && firing <= MILLI2CYCLES(15000), "A surviving member never fought after the hunting formation broke");
    clearHouse(owner);
    clearHouse(enemyHouse);
    return outcome;
}

/// The block march (docs/formations.md, "One speed"): a mixed Combined-arms group with sonic
/// tanks, siege tanks, an MCV, troopers and a harvester, right-clicked across open sand.
const std::vector<Start> kBlockArmy = {
    {Unit_SonicTank, Coord(6, 13)}, {Unit_SonicTank, Coord(4, 18)}, {Unit_SiegeTank, Coord(9, 15)},
    {Unit_SiegeTank, Coord(3, 10)}, {Unit_Tank, Coord(7, 21)},      {Unit_Tank, Coord(10, 9)},
    {Unit_Launcher, Coord(2, 15)},  {Unit_Launcher, Coord(5, 23)},  {Unit_MCV, Coord(4, 7)},
    {Unit_Quad, Coord(11, 19)},     {Unit_Quad, Coord(8, 6)},       {Unit_Trooper, Coord(2, 20)},
    {Unit_Trooper, Coord(6, 25)},   {Unit_Harvester, Coord(11, 24)},
};
const Coord kBlockTarget(40, 15);
constexpr Uint32 kSpeedWindow = 384;     ///< cycles per speed sample (several tiles at the pace)

struct BlockOutcome {
    double speedSpread = 0;     ///< largest (max - min) / mean member speed over a steady window
    double meanSpeed = 0;       ///< world units per cycle over the steady windows
    double shapeError = 0;      ///< largest distance of a member from its place in the block, tiles
    double stretch = 0;         ///< largest depth of the block along the march beyond the plan, tiles
    int windows = 0;
    Uint32 arrived = 0;
    bool harvesterLeftOut = false;  ///< no slot, no membership, its own move to the clicked tile
    bool siegeInFront = false;      ///< both siege tanks planned in the front row
    bool mcvOnTarget = false;       ///< the MCV's slot is the clicked tile
    int steadyCycles = 0;
    int mcvSurrounded = 0;          ///< steady cycles with members ahead, behind and on both sides of the MCV
    int mcvInFormation = 0;
    int harvesterOutside = 0;       ///< steady cycles with the harvester outside the formation
    int laneStepIns = 0;            ///< steps into a sonic lane by a unit that may not stand there
    int steadyLanePairs = 0;        ///< such units standing in a sonic lane, summed over the steady cycles
};

BlockOutcome marchBlock(Game& game, House* owner) {
    BlockOutcome outcome;
    auto group = deploy(owner, kBlockArmy);
    for(int i = 0; i < 5; ++i) game.updateGameState();
    settings.general.formationMoves = true;
    settings.general.formationShape = static_cast<int>(FormationPlanner::Shape::Staggered);
    std::vector<const UnitBase*> selected(group.begin(), group.end());
    const auto plan = FormationOrders::planGroup(selected, kBlockTarget, FormationPlanner::Shape::Staggered);
    require(plan.formation, "The block fixture got no formation plan");
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    for(UnitBase* unit : group) { game.selectedList.insert(unit->getObjectID()); unit->setSelected(true); }
    game.selectionChanged();
    require(game.handleSelectedObjectsActionClick(kBlockTarget.x, kBlockTarget.y), "The block right click was not accepted");
    // The measured block: every planned member except harvesters (old builds planned them too).
    std::vector<const UnitBase*> block;
    const UnitBase* harvester = nullptr;
    const UnitBase* mcv = nullptr;
    for(const UnitBase* unit : group) {
        if(isHarvesterLikeUnit(unit->getItemID())) harvester = unit;
        if(unit->getItemID() == Unit_MCV) mcv = unit;
        if(plan.slotFor(unit->getObjectID()).isValid() && !isHarvesterLikeUnit(unit->getItemID())) block.push_back(unit);
    }
    require(harvester != nullptr && mcv != nullptr, "The block fixture lost its harvester or MCV");
    for(int i = 0; i < 3; ++i) game.updateGameState();
    // The harvester in the selection is left out: no slot, no membership, its own ordinary
    // move to the clicked tile. The player then sends it home, off the MCV's slot.
    outcome.harvesterLeftOut = plan.slotFor(harvester->getObjectID()).isInvalid() && !harvester->isInFormation()
        && harvester->getDestination() == kBlockTarget;
    SDL_Log("FORMATION_PROBE_BLOCK_HARVESTER: slot %s, in formation %d, destination %d,%d (clicked %d,%d)",
            plan.slotFor(harvester->getObjectID()).isValid() ? "planned" : "none", harvester->isInFormation(),
            harvester->getDestination().x, harvester->getDestination().y, kBlockTarget.x, kBlockTarget.y);
    game.getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_UNIT_MOVE2POS, harvester->getObjectID(),
                                                12u, 28u, static_cast<Uint32>(true)));
    // Siege tanks hold the front row with the tanks; the MCV stands on the clicked tile.
    int frontX = INT32_MIN;
    for(const UnitBase* unit : block)
        frontX = std::max(frontX, plan.slotFor(unit->getObjectID()).x);
    outcome.siegeInFront = true;
    for(const UnitBase* unit : block)
        if(unit->getItemID() == Unit_SiegeTank) outcome.siegeInFront = outcome.siegeInFront && plan.slotFor(unit->getObjectID()).x == frontX;
    outcome.mcvOnTarget = plan.slotFor(mcv->getObjectID()) == kBlockTarget;
    std::set<std::pair<Uint32, Uint32>> inLane;
    std::string laneDetail;
    countLaneEntries(std::vector<UnitBase*>(group.begin(), group.end()), plan.forward, inLane, laneDetail);
    bool shot = false;
    double startX = 0, endX = 0;
    for(const UnitBase* unit : block) {
        startX += unit->getRealX().toDouble();
        endX += plan.slotFor(unit->getObjectID()).x * TILESIZE + TILESIZE / 2;
    }
    startX /= block.size();
    endX /= block.size();
    double planDepth = 0;
    {
        int lo = INT32_MAX, hi = INT32_MIN;
        for(const UnitBase* unit : block) { lo = std::min(lo, plan.slotFor(unit->getObjectID()).x); hi = std::max(hi, plan.slotFor(unit->getObjectID()).x); }
        planDepth = hi - lo;
    }
    std::vector<std::vector<std::pair<double, double>>> track;     // cycle -> member -> real position
    std::vector<double> progress;
    Uint32 cycles = 0;
    while(cycles < kTimeoutCycles) {
        game.updateGameState();
        ++cycles;
        std::vector<std::pair<double, double>> at;
        double sumX = 0;
        for(const UnitBase* unit : block) {
            at.emplace_back(unit->getRealX().toDouble(), unit->getRealY().toDouble());
            sumX += at.back().first;
        }
        track.push_back(at);
        progress.push_back((sumX / block.size() - startX) / (endX - startX));
        outcome.laneStepIns += countLaneEntries(std::vector<UnitBase*>(group.begin(), group.end()), plan.forward, inLane, laneDetail);
        if(progress.back() >= 0.35 && progress.back() <= 0.75) {
            // The MCV in the middle of the marching block: members ahead, behind and on both
            // sides of it (by more than half a tile) on every steady cycle.
            const double mx = mcv->getRealX().toDouble(), my = mcv->getRealY().toDouble();
            bool ahead = false, behind = false, left = false, right = false;
            for(const auto& [x, y] : at) {
                ahead = ahead || x > mx + TILESIZE / 2;
                behind = behind || x < mx - TILESIZE / 2;
                left = left || y < my - TILESIZE / 2;
                right = right || y > my + TILESIZE / 2;
            }
            ++outcome.steadyCycles;
            outcome.mcvSurrounded += (ahead && behind && left && right) ? 1 : 0;
            outcome.mcvInFormation += mcv->isInFormation() ? 1 : 0;
            outcome.harvesterOutside += harvester->isInFormation() ? 0 : 1;
            outcome.steadyLanePairs += forbiddenInLane(std::vector<UnitBase*>(group.begin(), group.end()), plan.forward);
        }
        if(!shot && progress.back() >= 0.5) {
            shot = true;
            const bool paths = settings.general.showMovementPaths;
            settings.general.showMovementPaths = false;
            game.unselectAll(game.selectedList);
            game.selectedList.clear();
            saveScreenshot(game, "formation-block.png", Coord(static_cast<int>(sumX / block.size()) / TILESIZE, 14));
            settings.general.showMovementPaths = paths;
        }
        bool settled = true;
        for(const UnitBase* unit : block)
            settled = settled && !unit->isMoving() && unit->getLocation() == plan.slotFor(unit->getObjectID());
        if(settled) break;
    }
    outcome.arrived = cycles;
    // The steady march: the block's centre between 35% and 75% of the way.
    auto steady = [&](size_t t) { return progress[t] >= 0.35 && progress[t] <= 0.75; };
    double speedSum = 0;
    int speedSamples = 0;
    std::vector<double> memberSpeed(block.size(), 0), memberLo(block.size(), 1e9), memberHi(block.size(), -1e9);
    std::vector<double> memberError(block.size(), 0);
    for(size_t t = 0; t + kSpeedWindow < track.size(); t += 32) {
        if(!steady(t) || !steady(t + kSpeedWindow)) continue;
        double lo = 1e9, hi = -1e9, sum = 0;
        for(size_t i = 0; i < block.size(); ++i) {
            const double speed = (track[t + kSpeedWindow][i].first - track[t][i].first) / kSpeedWindow;
            lo = std::min(lo, speed); hi = std::max(hi, speed); sum += speed;
            memberSpeed[i] += speed;
            memberLo[i] = std::min(memberLo[i], speed);
            memberHi[i] = std::max(memberHi[i], speed);
        }
        const double mean = sum / block.size();
        outcome.speedSpread = std::max(outcome.speedSpread, mean > 0 ? (hi - lo) / mean : 1.0);
        speedSum += mean;
        ++speedSamples;
        ++outcome.windows;
    }
    outcome.meanSpeed = speedSamples ? speedSum / speedSamples : 0;
    for(size_t t = 0; t < track.size(); ++t) {
        if(!steady(t)) continue;
        std::vector<double> dx, dy;
        double front = -1e9, back = 1e9;
        for(size_t i = 0; i < block.size(); ++i) {
            const Coord slot = plan.slotFor(block[i]->getObjectID());
            dx.push_back(track[t][i].first - (slot.x * TILESIZE + TILESIZE / 2));
            dy.push_back(track[t][i].second - (slot.y * TILESIZE + TILESIZE / 2));
            front = std::max(front, track[t][i].first);
            back = std::min(back, track[t][i].first);
        }
        auto median = [](std::vector<double> v) { std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end()); return v[v.size() / 2]; };
        const double mx = median(dx), my = median(dy);
        for(size_t i = 0; i < block.size(); ++i) {
            const double error = std::max(std::abs(dx[i] - mx), std::abs(dy[i] - my)) / TILESIZE;
            memberError[i] = std::max(memberError[i], error);
            outcome.shapeError = std::max(outcome.shapeError, error);
        }
        outcome.stretch = std::max(outcome.stretch, (front - back) / TILESIZE - planDepth);
    }
    for(size_t i = 0; i < block.size(); ++i)
        SDL_Log("FORMATION_PROBE_BLOCK_MEMBER: id=%u item=%u slot=%d,%d steady speed mean %.3f min %.3f max %.3f, largest place error %.2f tiles",
                block[i]->getObjectID(), block[i]->getItemID(), plan.slotFor(block[i]->getObjectID()).x,
                plan.slotFor(block[i]->getObjectID()).y, speedSamples ? memberSpeed[i] / speedSamples : 0.0,
                memberLo[i], memberHi[i], memberError[i]);
    SDL_Log("FORMATION_PROBE_BLOCK: %zu members, %d steady windows of %u cycles: speed spread %.1f%% (mean %.3f world units/cycle), "
            "shape error %.2f tiles, stretch beyond the planned depth %.2f tiles, settled after %.2fs",
            block.size(), outcome.windows, kSpeedWindow, outcome.speedSpread * 100, outcome.meanSpeed, outcome.shapeError,
            outcome.stretch, seconds(outcome.arrived));
    SDL_Log("FORMATION_PROBE_BLOCK_ROLES: harvester left out %d (outside the formation on %d/%d steady cycles), siege tanks in "
            "the front row %d, MCV on the clicked tile %d, in formation %d/%d and surrounded %d/%d steady cycles, "
            "%d sonic lane step-ins, %d unit-cycles in a sonic lane on the steady march%s",
            outcome.harvesterLeftOut, outcome.harvesterOutside, outcome.steadyCycles, outcome.siegeInFront, outcome.mcvOnTarget,
            outcome.mcvInFormation, outcome.steadyCycles, outcome.mcvSurrounded, outcome.steadyCycles, outcome.laneStepIns,
            outcome.steadyLanePairs, laneDetail.c_str());
    require(outcome.harvesterLeftOut && outcome.harvesterOutside == outcome.steadyCycles, "A selected harvester joined the formation");
    require(outcome.siegeInFront, "A siege tank was not planned in the front row");
    require(outcome.mcvOnTarget && outcome.steadyCycles > 0 && outcome.mcvInFormation == outcome.steadyCycles
            && outcome.mcvSurrounded == outcome.steadyCycles, "The MCV was not in the middle of the marching block");
    require(outcome.steadyLanePairs == 0, "A unit stood in a sonic lane on the march");
    require(outcome.windows > 0 && outcome.speedSpread <= 0.15, "The members' speeds differ by more than 15% on the steady march");
    require(outcome.shapeError <= 2.0 && outcome.stretch <= 3.0, "The block lost its shape on the steady march");
    clearHouse(owner);
    return outcome;
}

/// The form-up in place (docs/formations.md, "Form up where they stand"): a scattered mixed
/// group with sonic tanks, an MCV, a trooper and a harvester. Twelve formation members, so
/// an even split of headings cancels out.
const std::vector<Start> kFormUpArmy = {
    {Unit_SonicTank, Coord(6, 10)}, {Unit_SonicTank, Coord(4, 19)}, {Unit_Tank, Coord(9, 14)},
    {Unit_Tank, Coord(3, 8)},       {Unit_Tank, Coord(8, 22)},      {Unit_SiegeTank, Coord(11, 17)},
    {Unit_Launcher, Coord(2, 13)},  {Unit_Launcher, Coord(5, 24)},  {Unit_Quad, Coord(12, 9)},
    {Unit_Trike, Coord(10, 25)},    {Unit_Trooper, Coord(7, 17)},   {Unit_MCV, Coord(4, 15)},
    {Unit_Harvester, Coord(12, 21)},
};

struct FormUpOutcome {
    int shapes = 0;             ///< form-ups pressed and reached (four buttons and the hotkey)
    double slowest = 0;         ///< seconds from the press until every member stood on its slot
};

/// Enemy ground units (no sandworms) and structures the local team can see: what
/// Game::formUpFacing faces.
int visibleEnemies(int team) {
    int count = 0;
    for(const UnitBase* unit : unitList)
        count += (unit->isActive() && unit->isAGroundUnit() && unit->getItemID() != Unit_Sandworm
                  && unit->getOwner()->getTeamID() != team && unit->isVisible(team)) ? 1 : 0;
    for(const StructureBase* structure : structureList)
        count += (structure->getOwner()->getTeamID() != team && structure->isVisible(team)) ? 1 : 0;
    return count;
}

/// Presses a formation button of the group panel the way a mouse click does.
void pressFormationButton(Game& game, int choice) {
    game.pInterface->updateObjectInterface();
    auto* panel = dynamic_cast<MultiUnitInterface*>(game.pInterface->pObjectContainer);
    require(panel != nullptr && panel->formationBar.isVisible(), "The group panel shows no formation buttons");
    SymbolButton& button = panel->formationBar.buttons[choice];
    const Point inside(button.getSize().x / 2, button.getSize().y / 2);
    require(button.handleMouseLeft(inside.x, inside.y, true) && button.handleMouseLeft(inside.x, inside.y, false),
            "A formation button did not take the click");
}

void pressCycleHotkey(Game& game) {
    SDL_KeyboardEvent key{};
    key.type = SDL_KEYDOWN;
    key.keysym.sym = SDLK_f;
    key.keysym.mod = KMOD_CTRL | KMOD_SHIFT;
    SDL_SetModState(static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_SHIFT));
    game.handleKeyInput(key);
    SDL_SetModState(KMOD_NONE);
}

FormUpOutcome formUpOnButtons(Game& game, House* owner, House* enemyHouse) {
    require(enemyHouse != nullptr, "No enemy house for the form-up");
    FormUpOutcome outcome;
    auto all = deploy(owner, kFormUpArmy);
    std::vector<UnitBase*> members;
    UnitBase* harvester = nullptr;
    for(UnitBase* unit : all) {
        if(unit->getItemID() == Unit_Harvester) harvester = unit;
        else members.push_back(unit);
    }
    const std::vector<const UnitBase*> constMembers(members.begin(), members.end());
    require(harvester != nullptr && members.size() == 12, "The form-up fixture is not twelve members and a harvester");
    clearHouse(enemyHouse);     // what is left of the hunt
    for(int i = 0; i < 5; ++i) game.updateGameState();
    const int team = owner->getTeamID();
    require(visibleEnemies(team) == 0, "An enemy is in sight before the facing checks");
    game.orderedHeadings.clear();
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    for(UnitBase* unit : all) { game.selectedList.insert(unit->getObjectID()); unit->setSelected(true); }
    game.selectionChanged();

    // The facing rules, last first (Game::formUpFacing).
    // 3. No orders and no enemy in sight: the way they face (every body faces east).
    require(game.formUpFacing(constMembers) == Coord(1, 0), "Facing east, the group would not form up facing east");
    // 4. Half face west, half east: nothing to go by, north.
    for(size_t i = 0; i < members.size(); ++i) {
        members[i]->angle = (i % 2) ? LEFT : RIGHT;
        members[i]->drawnAngle = (i % 2) ? LEFT : RIGHT;
    }
    require(game.formUpFacing(constMembers) == Coord(0, -1), "Headings that cancel out did not fall back to north");
    // 2. An enemy in sight south of the group: towards it.
    const Coord centre = FormationPlanner::centroidOf([&] {
        std::vector<FormationPlanner::Member> planned;
        for(const UnitBase* unit : members) planned.push_back(FormationOrders::memberOf(unit));
        return planned;
    }());
    UnitBase* enemy = enemyHouse->createUnit(Unit_Tank, true);
    enemy->deploy(Coord(centre.x, 27));
    for(int i = 0; i < 3 && !enemy->isVisible(team); ++i) game.updateGameState();
    require(enemy->isVisible(team), "The enemy south of the group is not in sight");
    require(game.formUpFacing(constMembers) == FormationPlanner::facing(centre, enemy->getLocation())
            && game.formUpFacing(constMembers) == Coord(0, 1), "The group would not form up facing the enemy in sight");
    enemy->cancelDeployment();
    for(UnitBase* unit : members) { unit->setTarget(nullptr); unit->setDestination(unit->getLocation()); }
    game.updateGameState();
    require(visibleEnemies(team) == 0, "The enemy did not leave");
    std::map<Uint32, Coord> scattered;
    for(const UnitBase* unit : all) scattered[unit->getObjectID()] = unit->getLocation();

    int sumX = 0, sumY = 0;
    for(const UnitBase* unit : members) { sumX += unit->getLocation().x; sumY += unit->getLocation().y; }
    const Coord view(sumX / static_cast<int>(members.size()) + 4, sumY / static_cast<int>(members.size()));
    const bool paths = settings.general.showMovementPaths;
    settings.general.showMovementPaths = false;
    game.pInterface->updateObjectInterface();
    saveScreenshot(game, "formup-before.png", view);

    // 1. The order they were last given wins: a plain right-click far to the east (formations
    //    off). The buttons are pressed in the same cycle, before that order moves anyone.
    settings.general.formationMoves = false;
    require(game.handleSelectedObjectsActionClick(46, 16), "The eastward right click was not accepted");
    require(game.formUpFacing(constMembers) == Coord(1, 0), "The last order east did not set the facing");

    // Off: no command, nothing moves.
    settings.general.formationMoves = true;
    const Uint32 cycle = game.getGameCycleCount();
    const size_t queuedBefore = game.getCommandManager().commandsAt(cycle).size();
    pressFormationButton(game, 0);
    require(!settings.general.formationMoves, "The Off button did not switch formations off");
    require(game.getCommandManager().commandsAt(cycle).size() == queuedBefore, "The Off button issued commands");

    const char* shots[] = {"formup-box.png", "formup-pincer.png", "formup-column.png", "formup-staggered.png",
                           "formup-echelon.png", "formup-hotkey-box.png"};
    constexpr int kPresses = FormationPlanner::kShapeCount + 1;
    for(int press = 0; press < kPresses; ++press) {
        // The five shape buttons, then Ctrl+Shift+F from the last (Echelon) back to Box.
        const auto shape = FormationPlanner::shapeAt(press % FormationPlanner::kShapeCount);
        const Coord forward = game.formUpFacing(constMembers);
        require(forward == Coord(1, 0), "The group would not keep forming up facing east");
        const auto expected = FormationOrders::planInPlace(constMembers, forward, shape);
        require(expected.formation && expected.slots.size() == members.size(), "The form-up got no plan");
        std::vector<FormationPlanner::Member> planned;
        for(const UnitBase* unit : members) planned.push_back(FormationOrders::memberOf(unit));
        const Coord anchor = FormationPlanner::centroidOf(planned);
        const Uint32 at = game.getGameCycleCount();
        const size_t before = game.getCommandManager().commandsAt(at).size();
        if(press < FormationPlanner::kShapeCount) pressFormationButton(game, 1 + press);
        else pressCycleHotkey(game);
        require(settings.general.formationMoves && settings.general.formationShape == static_cast<int>(shape),
                "The press did not choose the shape");
        // One formation move per member to its planned slot, facing east, one group token.
        const auto& queued = game.getCommandManager().commandsAt(at);
        require(queued.size() == before + members.size(), "The press did not issue one command per member");
        std::set<Uint32> tokens;
        for(size_t i = before; i < queued.size(); ++i) {
            const Command& command = queued[i];
            const auto parameter = command.getParameter();
            require(command.getCommandID() == CMD_UNIT_FORMATION_MOVE, "The press issued something other than a formation move");
            require(parameter[0] != harvester->getObjectID(), "The harvester was given a formation slot");
            require(Coord(static_cast<int>(parameter[1]), static_cast<int>(parameter[2])) == expected.slotFor(parameter[0]),
                    "A member was not sent to its planned slot");
            require(FormationOrders::kindOf(parameter[3]) == static_cast<Uint32>(FormationOrders::OrderKind::FormUp)
                    && FormationOrders::facingOf(parameter[3]) == FormationPlanner::facingCode(Coord(1, 0)),
                    "A form-up is not of kind FormUp facing east");
            tokens.insert(FormationOrders::tokenOf(parameter[3]));
        }
        require(tokens.size() == 1, "The members were not ordered as one group");
        for(const UnitBase* unit : members)
            if(unit->getItemID() == Unit_MCV) require(expected.slotFor(unit->getObjectID()) == anchor, "The MCV is not on the group's centre");
        game.updateGameState();
        // One group, forming up straight into its slots at the slowest member's pace cap.
        for(const UnitBase* unit : members)
            require(unit->getFormationGroup() == members.front()->getFormationGroup()
                    && unit->getFormationSpeedCap() == members.front()->getFormationSpeedCap()
                    && (!unit->isInFormation() || (unit->getFormationSlot() == expected.slotFor(unit->getObjectID())
                        && (unit->getLocation() == unit->getFormationSlot()
                            || unit->getFormationFormUp() == unit->getFormationSlot()))),
                    "A member is not forming up into its slot with its group");
        bool harvesterJoined = false;
        const Uint32 cycles = runToSlots(game, members, expected, [&](Uint32) {
            harvesterJoined = harvesterJoined || harvester->isInFormation();
        });
        require(!harvesterJoined, "The harvester joined a formation");
        for(const UnitBase* sonic : members) {
            if(!FormationPlanner::isSonic(sonic->getItemID())) continue;
            for(const UnitBase* other : members)
                require(other == sonic || FormationPlanner::mayStandInLane(sonic->getItemID(), other->getItemID())
                        || !FormationPlanner::inSonicLane(sonic->getLocation(), other->getLocation(), Coord(1, 0)),
                        "A member stands in a sonic tank's lane after the form-up");
        }
        std::set<int> xs, ys;
        for(const UnitBase* unit : members) { xs.insert(unit->getLocation().x); ys.insert(unit->getLocation().y); }
        // Two abreast; the MCV's row mate moves aside to a third file.
        if(shape == FormationPlanner::Shape::Column) require(ys.size() <= 3 && xs.size() >= 5, "The column is not narrow and deep");
        game.pInterface->updateObjectInterface();     // the chosen shape's button held down
        saveScreenshot(game, shots[press], view);
        outcome.slowest = std::max(outcome.slowest, seconds(cycles));
        ++outcome.shapes;
        SDL_Log("FORMATION_PROBE_FORMUP_SHAPE: %s via %s around %d,%d: %zu members on their slots after %.2fs, frontage %zu depth %zu",
                FormationPlanner::shapeName(shape), press < FormationPlanner::kShapeCount ? "button" : "Ctrl+Shift+F", anchor.x, anchor.y, members.size(),
                seconds(cycles), ys.size(), xs.size());
    }
    settings.general.showMovementPaths = paths;
    require(game.orderedHeadings.at(members.front()->getObjectID()) == Coord(1, 0), "The form-up facing was not remembered");
    // The harvester kept its own order: the right click east.
    require(harvester->getLocation() != scattered.at(harvester->getObjectID()), "The harvester did not keep its own order");
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    game.selectionChanged();
    clearHouse(owner);
    return outcome;
}

} // namespace FormationProbe

int runFormationProbe() {
    using namespace FormationProbe;
    settings.general.playerName = "Formation tester";
    settings.general.showMovementPaths = true;
    std::ifstream mapFile(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(mapFile)), {});
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("formation.ini", mapData, "Formations", true, settings.gameOptions);
    init.randomSeed = 4242;
    GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
    humans.addPlayerInfo({"Formation tester", "HumanPlayer"});
    init.addHouseInfo(humans);
    GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN, 2);
    enemies.addPlayerInfo({"Enemy", "HumanPlayer"});
    init.addHouseInfo(enemies);
    game->initGame(init);
    game->initializeGameLoop();
    House* owner = game->getHouse(HOUSE_ATREIDES);
    require(owner && pLocalHouse == owner && pLocalPlayer != nullptr, "The local house is not the fixture house");

    // Open sand from the start area to beyond the target, with nothing standing on it.
    for(int y = 2; y <= 28; ++y)
        for(int x = 1; x <= 48; ++x) {
            Tile* tile = currentGameMap->getTile(x, y);
            require(!tile->hasAnObject(), "The fixture field is not empty at " + std::to_string(x) + "," + std::to_string(y));
            tile->setType(Terrain_Sand);
            tile->setRoad(false);
        }

    // 1. Formations on: the real right click issues one formation order per unit.
    auto army = deployArmy(owner);
    std::vector<const UnitBase*> constArmy(army.begin(), army.end());
    const auto expected = FormationOrders::planGroup(constArmy, kTarget, FormationPlanner::Shape::Box);
    require(expected.formation && expected.slots.size() == army.size(), "The fixture army got no formation plan");
    const Outcome on = march(*game, army, true, "formation-march.png");
    report("on", on, army);
    for(const UnitBase* unit : army)
        require(on.ordered.at(unit->getObjectID()) == expected.slotFor(unit->getObjectID()),
                "A unit was not sent to the slot the planner chose");
    std::set<std::pair<int, int>> slots;
    for(const auto& [id, slot] : on.ordered) slots.insert({slot.x, slot.y});
    require(slots.size() == army.size(), "Two vehicles were given the same slot");
    // Tanks ahead of launchers (travel is +x), at their destinations.
    int rearmostTank = INT32_MAX, foremostLauncher = INT32_MIN;
    for(const UnitBase* unit : army) {
        const Coord at = on.final.at(unit->getObjectID());
        if(unit->getItemID() == Unit_Tank) rearmostTank = std::min(rearmostTank, at.x);
        if(unit->getItemID() == Unit_Launcher) foremostLauncher = std::max(foremostLauncher, at.x);
    }
    require(rearmostTank > foremostLauncher, "A launcher ended up level with or ahead of a tank");
    require(on.arrival.size() == army.size(), "Not every formation member arrived");
    for(const UnitBase* unit : army) require(!unit->isInFormation(), "A member kept its formation after arriving");
    clearArmy(army);

    // 2. Formations off: exactly the old orders - every unit a forced move to the clicked tile.
    army = deployArmy(owner);
    const Outcome off = march(*game, army, false, nullptr);
    report("off", off, army);
    for(const UnitBase* unit : army) {
        require(off.ordered.at(unit->getObjectID()) == kTarget, "Without formations a unit was not sent to the clicked tile");
        require(!unit->isInFormation(), "Without formations a unit joined one");
    }
    SDL_Log("FORMATION_PROBE_SPREAD: on=%.2fs off=%.2fs arrived_on=%zu arrived_off=%zu",
            seconds(on.spread), seconds(off.spread), on.arrival.size(), off.arrival.size());
    require(on.spread < off.spread, "Formations did not narrow the arrival spread");
    require(on.spread <= MILLI2CYCLES(20000), "The formation arrival spread exceeds 20 seconds");
    clearArmy(army);

    // 3. Mid-march save and reload of a hunting attack-move in Combined arms: membership,
    //    slot, pace, the hunt flag, the facing and the form-up survive (SAVEGAMEVERSION 9860).
    army = deployArmy(owner);
    settings.general.formationMoves = true;
    settings.general.formationShape = static_cast<int>(FormationPlanner::Shape::Pincer);
    require(game->issuePositionOrder(Game::PositionOrder::AttackMove, army, kTarget, false) != nullptr,
            "The group attack-move was not issued");
    for(int i = 0; i < 300; ++i) game->updateGameState();
    struct Saved { Uint32 group; Coord slot; FixPoint cap; bool hunt; Uint8 facing; Coord formUp; };
    std::map<Uint32, Saved> before;
    int formingAtSave = 0;
    for(const UnitBase* unit : army) {
        require(unit->isInFormation() && unit->isFormationHunt(), "A member left its hunting formation mid-march");
        before[unit->getObjectID()] = {unit->getFormationGroup(), unit->getFormationSlot(), unit->getFormationSpeedCap(),
                                       unit->isFormationHunt(), unit->getFormationFacing(), unit->getFormationFormUp()};
        formingAtSave += unit->isFormationForming() ? 1 : 0;
        require(unit->getFormationFacing() == FormationPlanner::facingCode(Coord(1, 0)), "A hunting member has no eastward facing");
    }
    const std::string savePath = "formation-midmarch.dls";
    require(game->saveGame(savePath), "Could not save the mid-march game");
    army.clear();
    game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;
    game->initGame(GameInitSettings(savePath));
    game->bReplay = false;
    game->initializeGameLoop();
    game->bPause = false;
    require(game->getLoadedSavegameVersion() == SAVEGAMEVERSION, "The save was not written at the current version");
    owner = game->getHouse(HOUSE_ATREIDES);
    require(owner && pLocalHouse == owner && pLocalPlayer != nullptr, "The reloaded local house is not the fixture house");
    for(const auto& [id, saved] : before) {
        const auto* unit = dynamic_cast<const UnitBase*>(game->getObjectManager().getObject(id));
        require(unit != nullptr, "A member disappeared across the reload");
        require(unit->getFormationGroup() == saved.group && unit->getFormationSlot() == saved.slot
                && unit->getFormationSpeedCap() == saved.cap && unit->isFormationHunt() == saved.hunt
                && unit->getFormationFacing() == saved.facing && unit->getFormationFormUp() == saved.formUp,
                "Formation state changed across the reload");
    }
    Uint32 resumed = 0;
    std::map<Uint32, bool> reached;
    while(resumed < kTimeoutCycles && reached.size() < before.size()) {
        game->updateGameState();
        ++resumed;
        for(const auto& [id, saved] : before) {
            const auto* unit = dynamic_cast<const UnitBase*>(game->getObjectManager().getObject(id));
            if(unit && unit->getLocation() == saved.slot && !unit->isMoving()) reached[id] = true;
        }
    }
    require(reached.size() == before.size(), "The reloaded formation did not finish its march");
    SDL_Log("FORMATION_PROBE_SAVE: %zu hunting members (%d forming up) restored with group, slot, pace, hunt flag, facing and "
            "form-up tile; all reached their slots %.2fs after the reload", before.size(), formingAtSave, seconds(resumed));

    // 4. Old unit records: 9854 (no formation fields) loads with no membership, 9857 (no
    //    hunt flag or facing) loads its formation as a plain march, 9859 (no form-up) as a march.
    {
        Tank* tank = nullptr;
        for(UnitBase* unit : unitList) if(unit->getItemID() == Unit_Tank) { tank = static_cast<Tank*>(unit); break; }
        require(tank != nullptr, "No tank for the old-record fixture");
        // The 9855 order-queue block sits right before the formation record; an empty queue
        // writes a fixed 11 bytes (count, dispatched flag, walk retries, break-off point).
        tank->clearOrderQueue();
        constexpr size_t kEmptyOrderQueueBlock = 1 + 1 + 1 + 4 + 4;
        constexpr size_t kFormationRecord = 4 + 8 + 8;     // group, slot, speed cap (9857)
        constexpr size_t kHuntFields = 1 + 1;              // hunt flag and facing (9859)
        constexpr size_t kFormUpFields = 8 + 1 + 4;        // form-up tile, resume kind and pace unit type (9860)
        constexpr size_t kTornieFields = 4 + 4 + 8;        // production house, slow timer and step offset (9878)
        FormationOrders::join({tank, tank}, FormationPlanner::makeGroupID(1, 0x123456), {Coord(1, 2), Coord(1, 2)}, true, 3,
                              FormationOrders::Resume::Walk);
        tank->formationFormUp = Coord(3, 4);
        tank->formationPending = false;
        OMemoryStream withFormation; withFormation.open(); tank->save(withFormation);
        const std::string current(withFormation.getData(), withFormation.getDataLength());
        const Uint32 savedGroup = tank->getFormationGroup();
        const FixPoint savedCap = tank->getFormationSpeedCap();
        tank->leaveFormation();
        OMemoryStream without; without.open(); tank->save(without);
        const std::string plain(without.getData(), without.getDataLength());
        size_t first = 0;
        while(first < current.size() && current[first] == plain[first]) ++first;
        size_t last = current.size();
        while(last > first && current[last - 1] == plain[last - 1]) --last;
        constexpr size_t kAll = kFormationRecord + kHuntFields + kFormUpFields;
        constexpr size_t kAllWithTornie = kAll + kTornieFields;
        require(last - first <= kAll && first + kAllWithTornie <= current.size()
                && first >= kEmptyOrderQueueBlock, "The formation record is not where expected");
        // Old records also omit the 9878 technology/slow-state tail before the derived fields.
        const std::string derived = current.substr(first + kAllWithTornie);
        // 9854: neither the order queue nor the formation record with its later fields.
        const std::string old9854 = current.substr(0, first - kEmptyOrderQueueBlock) + derived;
        // 9857: the formation record without the hunt flag, facing and form-up after it.
        const std::string old9857 = current.substr(0, first + kFormationRecord) + derived;
        // 9859: everything but the form-up tile, resume kind and pace unit type.
        const std::string old9859 = current.substr(0, first + kFormationRecord + kHuntFields) + derived;
        game->loadedSavegameVersion = 9859;
        {
            IMemoryStream in(old9859.data(), old9859.size());
            Tank restored(in);
            require(in.getRemainingLength() == 0, "A 9859 unit record was not read exactly");
            require(restored.getFormationGroup() == savedGroup && restored.isFormationHunt() && restored.getFormationFacing() == 3
                    && !restored.isFormationForming(), "A 9859 unit did not load its formation as marching");
            restored.leaveFormation();
            restored.setHealth(0);
            unitList.remove(&restored);
        }
        {
            IMemoryStream in(current.data(), current.size());
            game->loadedSavegameVersion = SAVEGAMEVERSION;
            Tank restored(in);
            require(in.getRemainingLength() == 0 && restored.getFormationFormUp() == Coord(3, 4)
                    && restored.formationResume == static_cast<Uint8>(FormationOrders::Resume::Walk)
                    && restored.getFormationPaceItem() == static_cast<Uint32>(Unit_Tank),
                    "A 9860 unit did not keep its form-up tile, resume kind and pace unit");
            restored.leaveFormation();
            restored.setHealth(0);
            unitList.remove(&restored);
        }
        game->loadedSavegameVersion = 9854;
        {
            IMemoryStream in(old9854.data(), old9854.size());
            Tank restored(in);
            require(in.getRemainingLength() == 0, "A 9854 unit record was not read exactly");
            require(!restored.isInFormation() && restored.getFormationSlot().isInvalid()
                    && restored.getFormationSpeedCap() == 0, "A 9854 unit loaded with formation state");
            require(restored.getLocation() == tank->getLocation(), "A 9854 unit record lost its position");
            restored.setHealth(0);
            unitList.remove(&restored);
        }
        game->loadedSavegameVersion = 9857;
        {
            IMemoryStream in(old9857.data(), old9857.size());
            Tank restored(in);
            require(in.getRemainingLength() == 0, "A 9857 unit record was not read exactly");
            require(restored.getFormationGroup() == savedGroup && restored.getFormationSlot() == Coord(1, 2)
                    && restored.getFormationSpeedCap() == savedCap && !restored.isFormationHunt()
                    && restored.getFormationFacing() == 0,
                    "A 9857 unit did not load its formation as a plain march");
            restored.leaveFormation();
            restored.setHealth(0);
            unitList.remove(&restored);
        }
        game->loadedSavegameVersion = SAVEGAMEVERSION;
    }
    clearHouse(owner);

    // 5. Pincer with sonic tanks: two wings with a gap, and no unit ever stepping into a sonic
    //    tank's lane on the march; the frame shows the unit panel's formation buttons.
    const CombinedOutcome combined = marchCombined(*game, owner);

    // 6. Staggered and Column: frontage and depth of the real arrival.
    const ShapeExtent line = marchShape(*game, owner, FormationPlanner::Shape::Staggered);
    const ShapeExtent column = marchShape(*game, owner, FormationPlanner::Shape::Column);
    // Staggered: ten tanks in rows of four two tiles apart, alternate rows a column over.
    require(line.depth == 3 && line.frontage >= 7, "The staggered group did not stand in three spaced ranks");
    require(column.frontage == 2 && column.depth == kShapeGroup / 2, "The column did not stand two abreast, five deep");
    SDL_Log("FORMATION_PROBE_SHAPES: staggered frontage=%d depth=%d, column frontage=%d depth=%d", line.frontage, line.depth,
            column.frontage, column.depth);

    // 7. Hunt in formation towards an enemy group: the march keeps formation until the first
    //    contact, then every member fights.
    const HuntOutcome hunt = huntInFormation(*game, owner, game->getHouse(HOUSE_HARKONNEN));

    // 8. The one-speed block march: form-up, equal speeds, harvester left out, MCV in the middle.
    const BlockOutcome block = marchBlock(*game, owner);

    // 9. Form up where they stand: the panel's shape buttons and Ctrl+Shift+F re-form a
    //    scattered group around its centre, facing the way it was last sent; Off moves nobody.
    const FormUpOutcome formUp = formUpOnButtons(*game, owner, game->getHouse(HOUSE_HARKONNEN));
    SDL_Log("FORMATION_PROBE_FORMUP: %d form-ups in place (every shape button and Ctrl+Shift+F), all members on their slots within "
            "%.2fs, no member in a sonic lane in any formed shape, harvester left out, Off issued nothing",
            formUp.shapes, formUp.slowest);

    // 10. The shape gallery: every shape formed by the real player path, one frame each.
    const GalleryOutcome gallery = marchEveryShape(*game, owner);

    SDL_Log("FORMATION_PROBE_PASS: on spread %.2fs, off spread %.2fs, tanks ahead of launchers, hunting save round trip, "
            "9854, 9857 and 9859 records, pincer (%d sonic lane step-ins, wing gap %d), staggered %dx%d, column %dx%d, "
            "hunt broke after %.2fs in formation and every member hunted or fought within %.2fs, block march speed spread "
            "%.1f%%, shape error %.2f tiles, %d form-ups in place, %d shapes formed (slowest %.2fs, %d sonic lane step-ins)",
            seconds(on.spread), seconds(off.spread), combined.laneStepIns, combined.wingGap, line.frontage, line.depth,
            column.frontage, column.depth, seconds(hunt.marched), seconds(hunt.engaged), block.speedSpread * 100, block.shapeError,
            formUp.shapes, gallery.shapes, gallery.slowest, gallery.laneStepIns);
    game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    return -1;
}
