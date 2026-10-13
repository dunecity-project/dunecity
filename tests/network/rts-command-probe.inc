// Included by command-execution-probe.inc; never linked into the game.
//
// The modern RTS-control commands (docs/rts-controls.md) on real game objects, through the
// same paths a multiplayer peer uses: every command is written to the wire format, read
// back, checked by CommandValidation and executed, and a final batch goes through the
// CommandManager the lockstep loop runs. Ownership, parameter validation, queue order,
// Hold, and repeat production are all checked on the live unit and factory.
#include <units/UnitOrderQueue.h>
#include <CommandValidation.h>

void runRtsCommandProbe(House& owner, const Player& host, const Player& coop, const Player& enemy,
                        CommandManager& batches, Uint32 batchCycle) {
    auto require=[](bool ok, const char* why) {
        if(!ok) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RTS_COMMAND_PROBE_FAIL: %s", why);
            throw std::runtime_error(why);
        }
    };
    auto wire=[&](const Command& command) {
        OMemoryStream out; out.open(); command.save(out);
        IMemoryStream in(out.getData(), out.getDataLength());
        Command received(in);
        require(CommandValidation::isWellFormedCommand(received.getCommandID(), received.getParameter().size()),
                "An RTS-control command is not well formed after the wire round trip");
        received.executeCommand();
    };

    // Free rock for one tank, its waypoints and a heavy factory.
    Coord site = Coord::Invalid();
    for(int y = 1; y + 9 < currentGameMap->getSizeY() && site.isInvalid(); ++y)
        for(int x = 1; x + 13 < currentGameMap->getSizeX() && site.isInvalid(); ++x) {
            bool clear = true;
            for(int dy = 0; dy < 9 && clear; ++dy) for(int dx = 0; dx < 13 && clear; ++dx)
                if(currentGameMap->getTile(x+dx, y+dy)->hasAnObject()) clear = false;
            if(clear) site = Coord(x, y);
        }
    require(site.isValid(), "No free area for the RTS command fixture");
    for(int dy = 0; dy < 9; ++dy) for(int dx = 0; dx < 13; ++dx)
        currentGameMap->getTile(site.x+dx, site.y+dy)->setType(Terrain_Rock);

    UnitBase* tank = owner.createUnit(Unit_Tank, true);
    tank->deploy(site);
    tank->doSetAttackMode(GUARD);
    const Uint32 id = tank->getObjectID();
    const auto& queue = tank->getOrderQueue();
    const Coord a = site + Coord(7, 0), b = site + Coord(7, 3), c = site + Coord(11, 3);

    wire(Command(enemy.getPlayerID(), CMD_UNIT_ATTACKMOVE, id, a.x, a.y, 0));
    require(queue.empty() && tank->getAttackMode() == GUARD, "An enemy attack-move reached a foreign unit");

    wire(Command(host.getPlayerID(), CMD_UNIT_ATTACKMOVE, id, a.x, a.y, 0));
    require(queue.size() == 1 && queue.front().type == UnitOrderType::AttackMove && queue.front().pos == a
            && tank->getAttackMode() == AREAGUARD && tank->getDestination() == a,
            "The owner's attack-move did not start an Area Guard walk");

    wire(Command(coop.getPlayerID(), CMD_UNIT_QUEUEORDER, id, static_cast<Uint32>(UnitOrderType::Move), b.x, b.y));
    require(queue.size() == 2 && queue[1].type == UnitOrderType::Move && queue[1].pos == b && tank->getDestination() == a,
            "A co-op partner's queued move replaced the order instead of following it");

    // Malformed orders change nothing: a non-boolean queue flag, an off-map point, attack-move
    // smuggled through the queue command, and an unknown order type.
    wire(Command(host.getPlayerID(), CMD_UNIT_ATTACKMOVE, id, c.x, c.y, 2));
    wire(Command(host.getPlayerID(), CMD_UNIT_PATROL, id, 5000, 5000, 1));
    wire(Command(host.getPlayerID(), CMD_UNIT_QUEUEORDER, id, static_cast<Uint32>(UnitOrderType::AttackMove), c.x, c.y));
    wire(Command(host.getPlayerID(), CMD_UNIT_QUEUEORDER, id, 99, c.x, c.y));
    require(queue.size() == 2 && tank->getDestination() == a, "A malformed RTS-control command changed the unit");
    require(!CommandValidation::isWellFormedCommand(CMD_UNIT_PATROL, 3)
            && !CommandValidation::isWellFormedCommand(CMD_BUILDER_SETREPEAT, 2),
            "A short RTS-control command would be scheduled");

    // Shift+patrol after the waypoint starts its route there.
    wire(Command(host.getPlayerID(), CMD_UNIT_PATROL, id, c.x, c.y, 1));
    require(queue.size() == 4 && queue[2].type == UnitOrderType::Patrol && queue[2].pos == c
            && queue[3].patrolAnchor && queue[3].pos == b,
            "A queued patrol did not start from the last queued waypoint");

    // Hold clears the queue; an explicit move ends Hold and becomes the new front.
    wire(Command(host.getPlayerID(), CMD_UNIT_SETMODE, id, HOLD));
    require(queue.empty() && tank->getAttackMode() == HOLD, "Hold did not clear the order queue");
    wire(Command(host.getPlayerID(), CMD_UNIT_MOVE2POS, id, b.x, b.y, 1));
    require(tank->getAttackMode() == AREAGUARD && queue.size() == 1 && queue.front().type == UnitOrderType::Move
            && queue.front().pos == b, "An explicit move did not end Hold or did not become the queue front");

    // The lockstep path: one batch from the host, executed by the CommandManager, in order.
    CommandList batch;
    batch.commandList.emplace_back(batchCycle, std::vector<Command>{
        Command(host.getPlayerID(), CMD_UNIT_ATTACKMOVE, id, c.x, c.y, 1),
        Command(host.getPlayerID(), CMD_UNIT_QUEUEORDER, id, static_cast<Uint32>(UnitOrderType::Move), a.x, a.y)});
    batches.addCommandList("Host", batch);
    batches.executeCommands(batchCycle);
    require(queue.size() == 3 && queue[1].type == UnitOrderType::AttackMove && queue[1].pos == c
            && queue[2].type == UnitOrderType::Move && queue[2].pos == a,
            "Batched RTS-control commands were not executed in order");
    tank->clearOrderQueue();

    // Repeat production on a real heavy factory; never on a construction yard.
    auto* factory = dynamic_cast<BuilderBase*>(owner.placeStructure(NONE_ID, Structure_HeavyFactory, site.x, site.y + 5, true, true));
    require(factory != nullptr, "Heavy factory fixture placement failed");
    factory->updateBuildList();
    Uint32 item = ItemID_Invalid;
    for(const BuildItem& buildItem : factory->getBuildList())
        if(item == ItemID_Invalid && factory->supportsRepeat(buildItem.itemID)) item = buildItem.itemID;
    require(item != ItemID_Invalid, "The heavy factory offers no unit to repeat");
    const Uint32 factoryID = factory->getObjectID();
    wire(Command(enemy.getPlayerID(), CMD_BUILDER_SETREPEAT, factoryID, item, 1));
    wire(Command(host.getPlayerID(), CMD_BUILDER_SETREPEAT, factoryID, item, 2));
    require(!factory->isRepeating(item) && factory->getProductionQueueSize() == 0,
            "An enemy or malformed repeat command changed the factory");
    wire(Command(host.getPlayerID(), CMD_BUILDER_SETREPEAT, factoryID, item, 1));
    require(factory->isRepeating(item) && factory->getProductionQueueSize() == 1,
            "Turning repeat on did not mark the item and queue one copy");
    for(const StructureBase* structure : structureList) {
        if(structure->getOwner() != &owner || structure->getItemID() != Structure_ConstructionYard) continue;
        wire(Command(host.getPlayerID(), CMD_BUILDER_SETREPEAT, structure->getObjectID(), Structure_WindTrap, 1));
        require(!static_cast<const BuilderBase*>(structure)->isRepeating(Structure_WindTrap),
                "A construction yard accepted repeat production");
    }
    wire(Command(host.getPlayerID(), CMD_BUILDER_CANCELITEM, factoryID, item, 0));
    require(!factory->isRepeating(item) && factory->getProductionQueueSize() == 0,
            "Cancelling the item did not clear its repeat");
    SDL_Log("RTS_COMMAND_PROBE_PASS: attack-move, patrol, queued orders and repeat production obey ownership, "
            "parameter validation, queue order, Hold and the lockstep batch path");
}
