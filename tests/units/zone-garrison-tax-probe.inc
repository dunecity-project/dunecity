// Included inside ZoneGarrisonProbe after its real player/fixture helpers.
struct CapturedTaxMoney {
    std::array<FixPoint, 3> bank{}, gross{}, starting{}, stored{};
};

CapturedTaxMoney capturedTaxMoney(const std::array<House*, 3>& houses) {
    CapturedTaxMoney value;
    for(size_t i = 0; i < houses.size(); ++i) {
        value.bank[i] = houses[i]->getCityCredits();
        value.gross[i] = houses[i]->getCityTaxReceipts();
        value.starting[i] = houses[i]->getStartingCredits();
        value.stored[i] = houses[i]->getStoredCredits();
    }
    return value;
}

void checkCapturedTaxWindow(Fixture& f, const ZoneStructure* zone, House* holder,
                            const char* controller, const char* phase) {
    auto* sim = f.game->getCitySimulation();
    const std::string context = f.label + " / " + phase;
    const auto check = [&](bool ok, const char* why) {
        if(!ok) require(false, context + ": " + why);
    };
    check(sim != nullptr && sim->areCityEffectsEnabled(), "the real city budget is disabled");
    constexpr Uint32 ticks = 16;
    const Uint32 dayCycles = DuneCity::cyclesPerCityDay();
    check(dayCycles > ticks + 8, "the tax window cannot fit between real growth scans");
    // Wait for a genuine effects scan to value the new holder's property. Do not write
    // derived tax/land-value state or call the private payout routine directly.
    check(f.runUntil(6, [&] {
        check(zone->getOwner() == holder, "ownership changed while waiting for the real tax scan");
        const Uint32 withinDay = f.game->getGameCycleCount() % dayCycles;
        return sim->getHouseState(holder->getHouseID()).avgLandValue > 0
            && DuneCity::getStructureTaxBaseEighths(zone, lotDensity(zone)) > 0
            && withinDay >= 4 && withinDay + ticks + 2 < dayCycles
            && !sim->pendingGrowthPhase_;
    }), "no positive live tax base/land value reached the budget window");

    const std::array<House*, 3> houses{f.own, f.enemy, f.ally};
    const int taxBase = DuneCity::getStructureTaxBaseEighths(zone, lotDensity(zone));
    const auto& ownerState = sim->getHouseState(holder->getHouseID());
    const int landValue = ownerState.avgLandValue, rate = ownerState.taxRate;
    const int annual = DuneCity::computeAnnualTaxRevenue(taxBase, rate, landValue);
    const FixPoint tickRevenue = FixPoint(annual)
        / static_cast<int>(DuneCity::cyclesPerCityYear() / DuneCity::kCyclesPerBudgetTick);
    check(annual > 0 && tickRevenue > 0, "the experiment has zero possible tax activation");
    check(holder->getEarnedCreditRoom() > tickRevenue * static_cast<int>(ticks),
          "storage would hide or truncate the captured zone's tax");
    const CapturedTaxMoney initial = capturedTaxMoney(houses);
    for(Uint32 tick = 0; tick < ticks; ++tick) {
        const CapturedTaxMoney before = capturedTaxMoney(houses);
        const Uint32 budgetBefore = sim->lastBudgetTick_;
        check(zone->getOwner() == holder, "the measured zone changed owner before payout");
        f.game->updateGameState();
        check(sim->lastBudgetTick_ == budgetBefore + 1, "the normal game did not dispatch one real budget tick");
        check(zone->getOwner() == holder, "the measured zone changed owner during payout");
        check(DuneCity::getStructureTaxBaseEighths(zone, lotDensity(zone)) == taxBase
                && sim->getHouseState(holder->getHouseID()).avgLandValue == landValue
                && sim->getCityTax(holder->getHouseID()) == rate,
              "the controlled window crossed an unrelated tax-input change");
        const CapturedTaxMoney after = capturedTaxMoney(houses);
        FixPoint paid = 0, gross = 0;
        for(size_t i = 0; i < houses.size(); ++i) {
            const bool beneficiary = houses[i] == holder;
            const FixPoint expected = beneficiary ? tickRevenue : FixPoint(0);
            check(after.bank[i] - before.bank[i] == expected,
                  "spendable city credits did not follow the live holder's full single tax");
            check(after.gross[i] - before.gross[i] == expected,
                  "gross tax receipts were missing, duplicated or credited to another house");
            check(after.starting[i] == before.starting[i] && after.stored[i] == before.stored[i],
                  "unrelated cash or spice movement confounded the tax experiment");
            const auto& state = sim->getHouseState(houses[i]->getHouseID());
            check(state.taxBaseEighths == (beneficiary ? taxBase : 0)
                    && state.budget.getLastTaxRevenue() == (beneficiary ? annual : 0)
                    && state.lastPoliceExpense == 0,
                  "the per-house city budget disagrees with the sole taxable zone's live owner");
            paid += after.bank[i] - before.bank[i];
            gross += after.gross[i] - before.gross[i];
        }
        check(paid == tickRevenue && gross == tickRevenue, "tax was paid more or less than once across the houses");
    }
    const CapturedTaxMoney final = capturedTaxMoney(houses);
    const FixPoint expected = tickRevenue * static_cast<int>(ticks);
    check(expected > 0 && holder->getCityCredits() > (holder == f.own ? initial.bank[0] : initial.bank[1]),
          "the expected recipient's actual credits never rose");
    SDL_Log("ZONE_GARRISON_TAX: controller=%s item=%d phase=%s holder=%d ticks=%u annual=%d expected_raw=%lld own_raw=%lld original_raw=%lld unrelated_raw=%lld conserved=1",
        controller, zone->getItemID(), phase, holder->getHouseID(), ticks, annual,
        static_cast<long long>(expected.getRawValue()),
        static_cast<long long>((final.bank[0] - initial.bank[0]).getRawValue()),
        static_cast<long long>((final.bank[1] - initial.bank[1]).getRawValue()),
        static_cast<long long>((final.bank[2] - initial.bank[2]).getRawValue()));
}

void checkCapturedTaxCase(int item, bool aiCapturer) {
    settings.general.playerName = aiCapturer ? "Enemy" : "Garrison tester";
    const char* controller = aiCapturer ? "ai" : "human";
    Fixture f(std::string("captured tax ") + controller + " item=" + std::to_string(item), true, false, aiCapturer);
    require(pLocalHouse == (aiCapturer ? f.enemy : f.own), f.label + ": the real human/observer owns the wrong controls");
    // Untaxed utility buildings supply real earned-income storage and power to either holder.
    require(f.own->placeStructure(NONE_ID, Structure_Silo, 4, 50, true, true)
            && f.enemy->placeStructure(NONE_ID, Structure_Silo, 50, 4, true, true)
            && f.own->placeStructure(NONE_ID, Structure_WindTrap, 28, 30, true, true)
            && f.enemy->placeStructure(NONE_ID, Structure_WindTrap, 32, 30, true, true),
            f.label + ": the real storage/power fixture could not be built");
    auto* zone = f.zone(f.enemy, item, 30, 30, 3);
    QuantBot* bot = dynamic_cast<QuantBot*>(f.own->getPlayerList().front().get());
    require((bot != nullptr) == aiCapturer, f.label + ": the requested real controller class is missing");
    checkCapturedTaxWindow(f, zone, f.enemy, controller, "original");
    auto* soldier = f.unit(f.own, Unit_Soldier, 29, 29, STOP);
    const auto capture = [&](bool retake) {
        if(bot) {
            Command(f.playerOf(f.own), CMD_UNIT_SETMODE, soldier->getObjectID(), GUARD).executeCommand();
            bot->setRole(AIRole::Fighter);
        } else {
            require(dynamic_cast<HumanPlayer*>(f.own->getPlayerList().front().get()) != nullptr,
                    f.label + ": the player Capture path has no real human");
            require(captureByPlayer(f, {soldier}, zone, retake, true) == 1,
                    f.label + ": the real Capture button/key and map click did not queue an order");
        }
        require(f.runUntil(20, [&] {
            return zone->getOwner() == f.own && zone->getGarrisonHouse() == f.own && zone->getGarrisonSize() == 1;
        }), f.label + ": the real human/AI failed to take the empty enemy zone");
        if(bot) require(!bot->switchedOff() && bot->initialMilitaryValue >= 0,
                        f.label + ": the AI capture did not exercise an active QuantBot");
        require(hidden(soldier) && zone->getGarrisonOriginalOwner() == f.enemy->getHouseID(),
                f.label + ": ownership did not come from a real completed capture");
    };
    capture(false);
    checkCapturedTaxWindow(f, zone, f.own, controller, "captured");
    if(bot) {
        bot->setRole(AIRole::Off); // Isolate release accounting from the next tactical decision.
        Command(f.playerOf(f.own), CMD_ZONE_EVACUATE, zone->getObjectID()).executeCommand();
    } else {
        auto* panel = selectZone(f, zone);
        require(panel->evacuateButton.isVisible() && panel->evacuateButton.isEnabled(),
                f.label + ": the real captured-zone panel does not offer Evacuate");
        const int before = queued(*f.game, CMD_ZONE_EVACUATE);
        press(panel->evacuateButton);
        require(queued(*f.game, CMD_ZONE_EVACUATE) == before + 1,
                f.label + ": the real Evacuate widget did not issue its command");
    }
    require(f.runUntil(5, [&] { return !zone->isGarrisoned() && zone->getOwner() == f.enemy; }),
            f.label + ": real evacuation did not return ownership to the original house");
    Command(f.playerOf(f.own), CMD_UNIT_SETMODE, soldier->getObjectID(), STOP).executeCommand();
    require(soldier->isActive() && soldier->getAttackMode() == STOP,
            f.label + ": the released soldier's real Stop order did not take effect");
    checkCapturedTaxWindow(f, zone, f.enemy, controller, "released");
    capture(true);
    checkCapturedTaxWindow(f, zone, f.own, controller, "retaken");
    record("captured_tax", std::string("controller=") + controller + " item=" + std::to_string(item)
           + " captures=2 release=1 full_tax_follows_owner=1 duplicate_tax=0");
}

void checkCapturedZoneTax() {
    int failed = 0;
    for(bool ai : {false, true}) {
        for(int item : {Structure_ZoneResidential, Structure_ZoneCommercial, Structure_ZoneIndustrial}) {
            try {
                checkCapturedTaxCase(item, ai);
            } catch(const std::exception&) {
                ++failed;
                currentGame = nullptr;
            }
        }
    }
    settings.general.playerName = "Garrison tester";
    require(failed == 0, "captured tax: " + std::to_string(failed) + " of 6 real human/AI R/C/I cases failed");
}
