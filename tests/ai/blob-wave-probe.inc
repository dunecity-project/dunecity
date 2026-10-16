#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <players/BlobWavePolicy.h>

// Real-engine QuantBot hunt parties (docs/formations.md, "Blob waves").
// Included only by run-campaign-balance.py --blob-wave-probe; never linked into the game.
//
// Drives the production QuantBot::launchGroundHunt() and checkAllUnits() (which runs
// updateHuntParties()) on a Custom Hard house against real units, structures and the real map:
//   1. Gather: a mixed wave with WaveFormation=Blob becomes one hunt party of every available
//      ground fighter (the harvester and the MCV stay out); each member walks to its own tile
//      at a rally point towards the objective in Area Guard; the party marches once they are
//      there, with no timeout.
//   2. Blob march: leg by leg towards the objective; no member hunts before contact, and a fast
//      member (quad, trike) is never more than two legs and two blob radii from the nearest
//      slow member - the same wave as plain Hunt (None) spreads much further.
//   3. Release on contact: enemy quads ahead put every member into engine Hunt.
//   4. Reform: once the enemies are gone and the fight is quiet, the party reforms where it
//      stands and marches on.
//   5. Tactical retreat: an overwhelming enemy makes the party fall back about kFallbackTiles
//      away from it, all members on a forced move; it reforms there and marches again.
//   6. Gather timeout: with most members far away the party leaves after kGatherMaxMs and
//      keeps the stragglers.
//   7. Determinism: the same units in reverse order give the same party and the same orders,
//      and contact is the same object for a reversed unit list.
//   8. Save/load mid-gather, mid-march and mid-reform restores every party field and re-saves
//      the same bytes; a 9861 save has no party.
//   9. None keeps today's Hunt orders and no party; None with the tactical-retreat override
//      tracks a Hunt party in the fight phase.
void runBlobWaveProbe(Game& game) {
    using BlobWavePolicy::Phase;
    auto require=[](bool ok,const char* why){
        if(!ok){SDL_Log("BLOB_WAVE_FAIL: %s",why);throw std::runtime_error(why);}
    };
    auto* owner=game.getHouse(HOUSE_HARKONNEN);
    auto* enemy=game.getHouse(HOUSE_ATREIDES);
    require(owner && enemy,"Blob fixture missing houses");
    QuantBot* bot=nullptr;
    for(const auto& player:owner->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get()))bot=candidate;
    require(bot,"Blob fixture missing bot");
    // The wave fixtures stand where they are spawned: the bundle launch gate (on by default)
    // would hold them up to 60 s. Bundling and the gate have their own probe
    // (quantbot_force_concentration); this one keeps the other production force rules.
    bot->forceVariantOverride=QuantBot::kForceFocusFire|QuantBot::kForceConverge|QuantBot::kForceSmallWaves;

    game.gameType=GameType::CustomGame;
    bot->gameMode=QuantBot::GameMode::Custom;
    bot->supportMode=false;
    bot->difficulty=QuantBot::Difficulty::Hard;
    bot->manualUnitOrders.clear();
    bot->defenceAssignments.clear();
    bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
    const int savedShape=getQuantBotConfig().waveFormation;
    const bool savedRecovery=getQuantBotConfig().recovery.enabled;

    // ---- Free ground: 60 x 24 tiles without objects or mountains, no enemy within 14 tiles
    //      (the wave-formation probe's fixture area) ----
    constexpr int kWidth=60, kHeight=24;
    std::vector<Coord> enemyThings;
    for(const auto* structure:structureList)
        if(structure->getOwner()->getTeamID()!=owner->getTeamID()) enemyThings.push_back(structure->getLocation());
    for(const auto* unit:unitList)
        if(unit->getOwner()->getTeamID()!=owner->getTeamID() && unit->getLocation().isValid())
            enemyThings.push_back(unit->getLocation());
    Coord origin=Coord::Invalid();
    for(int y=2;y+kHeight<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=2;x+kWidth<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(const Coord& at:enemyThings) {
                const int dx=std::max({x-at.x,0,at.x-(x+kWidth-1)}), dy=std::max({y-at.y,0,at.y-(y+kHeight-1)});
                if(std::max(dx,dy)<14) {clear=false;break;}
            }
            for(int dy=0;dy<kHeight && clear;++dy)for(int dx=0;dx<kWidth && clear;++dx) {
                const auto* tile=currentGameMap->getTile(x+dx,y+dy);
                if(tile->hasAGroundObject() || tile->isMountain())clear=false;
            }
            if(clear)origin=Coord(x,y);
        }
    require(origin.isValid(),"Blob fixture needs a clear 60x24 area away from the enemy");

    std::vector<UnitBase*> roster;
    std::map<const UnitBase*,Uint32> rosterIds;
    std::vector<Uint32> fixtureStructureIds;
    const auto savedUnits=unitList;
    auto alive=[&](const UnitBase* unit){
        return currentGame->getObjectManager().getObject(rosterIds.at(unit))==unit && unit->getHealth()>0;
    };
    auto publish=[&] {
        roster.erase(std::remove_if(roster.begin(),roster.end(),[&](UnitBase* unit) {
            return currentGame->getObjectManager().getObject(rosterIds.at(unit))!=unit;
        }),roster.end());
        unitList.clear();
        for(auto* unit:roster) unitList.push_back(unit);
    };
    auto spawn=[&](House* house,Uint32 item,Coord offset) {
        auto* unit=house->createUnit(item);
        require(unit,"Blob fixture unit creation failed");
        unit->deploy(origin+offset);
        unit->setVisible(owner->getTeamID(),true);
        unit->setVisible(enemy->getTeamID(),true);
        roster.push_back(unit);
        rosterIds[unit]=unit->getObjectID();
        return unit;
    };
    auto tick=[&]{
        game.processObjects();++game.gameCycleCount;
        // The real unit pass (checkAllUnits runs updateHuntParties) on QuantBot's cadence.
        if(game.getGameCycleCount()%50==0) { publish(); bot->checkAllUnits(); }
    };
    auto retire=[&](const std::vector<UnitBase*>& units) {
        for(auto* unit:units) if(alive(unit)) unit->setHealth(0);
        game.processObjects();++game.gameCycleCount;
        publish();
    };
    auto retireAll=[&] {
        for(auto* unit:roster) if(alive(unit)) unit->setHealth(0);
        game.processObjects();++game.gameCycleCount;
        roster.clear();
        publish();
        bot->blobParties.clear();
        bot->customWave.members.clear();
    };
    auto place=[&](House* house,Uint32 item,Coord offset) {
        auto* structure=house->placeStructure(NONE_ID,item,origin.x+offset.x,origin.y+offset.y,true,true);
        if(structure) {
            structure->setVisible(owner->getTeamID(),true);
            structure->setVisible(enemy->getTeamID(),true);
            fixtureStructureIds.push_back(structure->getObjectID());
        }
        return structure;
    };
    auto cleanupStructures=[&] {
        for(const Uint32 id:fixtureStructureIds)
            if(auto* s=dynamic_cast<StructureBase*>(currentGame->getObjectManager().getObject(id)))
                if(s->getHealth()>0) s->setHealth(0);
    };
    auto party=[&]() -> QuantBot::BlobParty* { return bot->blobParties.empty() ? nullptr : &bot->blobParties.front(); };
    auto phase=[&]{ return party() ? party()->phase : Phase::Gather; };
    auto runUntil=[&](int ms, const std::function<bool()>& done) {
        for(int i=0;i<MILLI2CYCLES(ms);++i) { if(done()) return true; tick(); }
        return done();
    };

    // The mixed wave: slow armour and infantry, fast quads and trikes, launchers.
    struct Pick { Uint32 item; Coord at; };
    const std::vector<Pick> picks{
        {Unit_Tank,{8,6}},{Unit_Tank,{9,11}},{Unit_Tank,{7,16}},{Unit_Tank,{10,20}},
        {Unit_SiegeTank,{5,12}},{Unit_SiegeTank,{12,8}},
        {Unit_Launcher,{4,17}},{Unit_Launcher,{9,18}},
        {Unit_Quad,{13,12}},{Unit_Quad,{12,17}},{Unit_Quad,{14,14}},
        {Unit_Trike,{11,5}},{Unit_Trike,{13,21}},
        {Unit_Trooper,{6,19}},{Unit_Trooper,{7,21}},{Unit_Trooper,{8,22}},
    };
    auto fast=[](const UnitBase* unit){ return unit->getItemID()==Unit_Quad || unit->getItemID()==Unit_Trike; };
    struct Wave { std::vector<UnitBase*> fighters; UnitBase* harvester=nullptr; UnitBase* mcv=nullptr; };
    auto muster=[&](const std::vector<Pick>& list) {
        Wave wave;
        for(const auto& pick:list) wave.fighters.push_back(spawn(owner,pick.item,pick.at));
        wave.harvester=spawn(owner,Unit_Harvester,Coord(10,14));
        wave.mcv=spawn(owner,Unit_MCV,Coord(5,8));
        publish();
        for(auto* unit:wave.fighters) {
            bot->doSetAttackMode(unit,GUARD);
            bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);
            unit->setForced(false);
        }
        bot->customWave.members.clear();
        bot->customWave.front=NONE_ID;
        bot->blobParties.clear();
        bot->blobStats=QuantBot::BlobStats();
        bot->militaryValueLimit=1;
        getQuantBotConfig().recovery.enabled=false;
        return wave;
    };
    auto ids=[](const std::vector<UnitBase*>& units){ std::set<Uint32> s; for(auto* u:units) s.insert(u->getObjectID()); return s; };
    // The largest distance from a fast member to the nearest slow member that is still alive.
    auto fastLead=[&](const std::vector<UnitBase*>& units) {
        int lead=0;
        for(const auto* f:units) {
            if(!alive(f) || !fast(f)) continue;
            int nearest=INT32_MAX;
            for(const auto* s:units)
                if(alive(s) && !fast(s)) nearest=std::min(nearest,int(blockDistance(f->getLocation(),s->getLocation()).lround()));
            if(nearest!=INT32_MAX) lead=std::max(lead,nearest);
        }
        return lead;
    };
    // A save and load of the whole bot keeps every party field and re-saves the same bytes.
    auto saveRoundTrip=[&](const char* when) {
        OMemoryStream out; out.open();
        bot->save(out);
        const std::string bytes(out.getData(),out.getDataLength());
        IMemoryStream in(bytes.data(),bytes.size());
        const Uint32 savedVersion=currentGame->loadedSavegameVersion;
        currentGame->loadedSavegameVersion=SAVEGAMEVERSION;
        QuantBot restored(in,owner);
        require(restored.blobParties.size()==bot->blobParties.size(),"A save and load lost a hunt party");
        for(size_t i=0;i<restored.blobParties.size();++i) {
            const auto& a=restored.blobParties[i]; const auto& b=bot->blobParties[i];
            require(a.phase==b.phase && a.since==b.since && a.objective==b.objective && a.goal==b.goal && a.rally==b.rally
                    && a.threat==b.threat && a.lastContact==b.lastContact && a.lastArrival==b.lastArrival
                    && a.gathered==b.gathered && a.legCycles==b.legCycles && a.outmatchedSince==b.outmatchedSince
                    && a.progressCycle==b.progressCycle && a.progressRemaining==b.progressRemaining
                    && a.fallbacks==b.fallbacks && a.members==b.members,
                "A save and load changed a hunt party");
        }
        OMemoryStream again; again.open();
        restored.save(again);
        require(std::string(again.getData(),again.getDataLength())==bytes,"Re-saving a loaded hunt party produced different bytes");
        currentGame->loadedSavegameVersion=9861;
        IMemoryStream legacyIn(bytes.data(),bytes.size());
        QuantBot legacy(legacyIn,owner);
        require(legacy.blobParties.empty(),"A 9861 save invented a hunt party");
        currentGame->loadedSavegameVersion=savedVersion;
        SDL_Log("BLOB_WAVE_PROBE: save_load=%s parties=%zu bytes=%zu",when,bot->blobParties.size(),bytes.size());
    };

    int blobLead=-1;    // the blob's largest fast-member lead on the march, for the None comparison
    try {
        require(place(owner,Structure_ConstructionYard,Coord(1,1)),"Fixture yard placement failed");
        // The enemy base the wave heads for, about 45 tiles east of it.
        require(place(enemy,Structure_ConstructionYard,Coord(56,10)),"Fixture enemy yard placement failed");
        // The map's own enemy buildings near the fixture would compete for the objective (and be
        // met on the march): only the fixture base is left within 60 tiles of it.
        for(auto* structure:structureList)
            if(structure->getOwner()->getTeamID()!=owner->getTeamID() && structure->getHealth()>0
               && std::find(fixtureStructureIds.begin(),fixtureStructureIds.end(),structure->getObjectID())==fixtureStructureIds.end()
               && blockDistance(structure->getLocation(),origin+Coord(kWidth/2,kHeight/2)).lround()<=60)
                structure->setHealth(0);
        game.processObjects();++game.gameCycleCount;

        // ---- 1. Gather ---------------------------------------------------------------------
        getQuantBotConfig().waveFormation=QuantBotConfig::kWaveFormationBlob;
        {
            Wave wave=muster(picks);
            bot->launchGroundHunt();
            require(bot->blobParties.size()==1,"A Blob wave did not become one hunt party");
            require(party()->phase==Phase::Gather,"The hunt party did not start by gathering");
            require(party()->members==ids(wave.fighters),"The hunt party is not every available ground fighter");
            require(!party()->members.count(wave.harvester->getObjectID()) && !party()->members.count(wave.mcv->getObjectID()),
                "The harvester or the MCV joined the hunt party");
            require(wave.harvester->getAttackMode()!=HUNT && wave.mcv->getAttackMode()!=HUNT,"A worker was sent to attack");
            const auto* objective=bot->getObject(party()->objective);
            require(objective && objective->getOwner()==enemy,"The hunt party has no enemy objective");
            const Coord rally=party()->rally;
            const int radius=BlobWavePolicy::blobRadius(int(wave.fighters.size()))+1;
            for(auto* unit:wave.fighters) {
                require(bot->customWave.members.count(unit->getObjectID()),"A party member is not in the tracked wave");
                require(unit->getAttackMode()==AREAGUARD && !unit->isHuntingToward(),"A gathering member is not in Area Guard");
                require(blockDistance(unit->getDestination(),rally).lround()<=radius,"A gathering member is not sent to the rally");
            }
            saveRoundTrip("gather");
            const Uint32 gatherStart=game.getGameCycleCount();
            require(runUntil(BlobWavePolicy::kGatherMaxMs+2000,[&]{ return phase()!=Phase::Gather; }),"The gather never ended");
            require(phase()==Phase::March,"The gather did not lead into the march");
            require(bot->blobStats.gatherTimeouts==0,"A gather on open ground timed out");
            SDL_Log("BLOB_WAVE_PROBE: gathered=%u of %zu in %.1f s rally=%d,%d",party()->gathered,wave.fighters.size(),
                (game.getGameCycleCount()-gatherStart)*GAMESPEED_DEFAULT/1000.0,rally.x,rally.y);
            for(auto* unit:wave.fighters)
                SDL_Log("BLOB_WAVE_PROBE: member item=%u at=%d,%d dest=%d,%d mode=%d moving=%d",unit->getItemID(),
                    unit->getLocation().x,unit->getLocation().y,unit->getDestination().x,unit->getDestination().y,
                    int(unit->getAttackMode()),int(unit->isMoving()));
            require(int64_t(party()->gathered)*10000>=int64_t(BlobWavePolicy::kGatherQuorumBps)*int64_t(wave.fighters.size()),
                "The march began before the party had gathered");

            // ---- 2. Blob march -----------------------------------------------------------
            const int members=int(wave.fighters.size());
            const int leg=BlobWavePolicy::legTiles(members);
            const int bound=2*leg+2*radius;
            const Coord goal=party()->goal;
            const Coord start=bot->huntPartyAnchor(*party())->getLocation();
            int maxLead=0, marchCycles=0;
            bool midMarchSaved=false;
            while(phase()==Phase::March && marchCycles<MILLI2CYCLES(150000)
                  && blockDistance(bot->huntPartyAnchor(*party())->getLocation(),goal).lround()>22) {
                tick(); ++marchCycles;
                maxLead=std::max(maxLead,fastLead(wave.fighters));
                for(auto* unit:wave.fighters) require(unit->getAttackMode()!=HUNT,"A marching member hunted before any contact");
                if(!midMarchSaved && bot->blobStats.legs>=2) { saveRoundTrip("march"); midMarchSaved=true; }
            }
            require(phase()==Phase::March,"The blob left the march without meeting anyone");
            const int travelled=blockDistance(start,bot->huntPartyAnchor(*party())->getLocation()).lround();
            SDL_Log("BLOB_WAVE_PROBE: march travelled=%d legs=%d max_fast_lead=%d bound=%d seconds=%.1f",
                travelled,bot->blobStats.legs,maxLead,bound,marchCycles*GAMESPEED_DEFAULT/1000.0);
            require(travelled>=12 && bot->blobStats.legs>=2,"The blob did not march towards its objective");
            require(maxLead<=bound,"A fast member ran ahead of the blob");
            blobLead=maxLead;

            // ---- 3. Release on contact -----------------------------------------------------
            const Coord at=bot->huntPartyAnchor(*party())->getLocation();
            const int dir=goal.x>=at.x ? 1 : -1;
            std::vector<UnitBase*> raiders;
            for(int i=0;i<3;++i) raiders.push_back(spawn(enemy,Unit_Quad,at-origin+Coord(dir*12,(i-1)*2)));
            publish();
            for(auto* raider:raiders) { raider->doSetAttackMode(GUARD); raider->doMove2Pos(raider->getX(),raider->getY(),false); }
            require(runUntil(20000,[&]{ return phase()==Phase::Fight; }),"The blob did not release on contact");
            require(bot->blobStats.releases==1,"The release was not counted once");
            int hunting=0, inParty=0;
            for(auto* unit:wave.fighters) {
                if(!alive(unit) || !party()->members.count(unit->getObjectID())) continue;
                ++inParty;
                if(unit->getAttackMode()==HUNT) ++hunting;
            }
            require(inParty>0 && hunting==inParty,"A member did not go into engine Hunt on contact");

            // ---- 4. Reform after the fight -------------------------------------------------
            runUntil(3000,[]{ return false; });
            retire(raiders);
            const Uint32 quietFrom=game.getGameCycleCount();
            require(runUntil(BlobWavePolicy::kQuietMs+12000,[&]{ return phase()==Phase::Reform; }),"The party did not reform after the fight");
            const double quiet=(game.getGameCycleCount()-quietFrom)*GAMESPEED_DEFAULT/1000.0;
            SDL_Log("BLOB_WAVE_PROBE: reform after %.1f s quiet",quiet);
            for(auto* unit:wave.fighters)
                if(alive(unit) && party()->members.count(unit->getObjectID()))
                    require(unit->getAttackMode()==AREAGUARD,"A reforming member is not closing up in Area Guard");
            saveRoundTrip("reform");
            require(runUntil(BlobWavePolicy::kReformMaxMs+2000,[&]{ return phase()!=Phase::Reform; }),"The reform never ended");
            require(phase()==Phase::March && bot->blobStats.reforms>=1,"The reformed party did not march on");

            // ---- 5. Tactical retreat --------------------------------------------------------
            const Coord before=bot->huntPartyAnchor(*party())->getLocation();
            std::vector<UnitBase*> heavy;
            // Six devastators and four siege tanks: well over twice the party's power.
            for(int i=0;i<10;++i) heavy.push_back(spawn(enemy,i<6 ? Unit_Devastator : Unit_SiegeTank,
                before-origin+Coord(dir*(i<5 ? 6 : 8),(i%5)*2-4)));
            publish();
            for(auto* tank:heavy) { tank->doSetAttackMode(GUARD); tank->doMove2Pos(tank->getX(),tank->getY(),false); }
            require(runUntil(20000,[&]{ return phase()==Phase::Fallback; }),"An overwhelmed party did not fall back");
            const Coord fallFrom=bot->huntPartyAnchor(*party())->getLocation();
            const Coord point=party()->rally;
            const int back=blockDistance(fallFrom,point).lround();
            int threatX=0; for(auto* tank:heavy) threatX+=tank->getLocation().x; threatX/=int(heavy.size());
            SDL_Log("BLOB_WAVE_PROBE: fallback from=%d,%d to=%d,%d distance=%d threat_x=%d",fallFrom.x,fallFrom.y,point.x,point.y,back,threatX);
            require(back>=BlobWavePolicy::kFallbackTiles-3 && back<=BlobWavePolicy::kFallbackTiles+8,
                "The tactical fall-back is not a short step back");
            require(std::abs(point.x-threatX)>std::abs(fallFrom.x-threatX),"The fall-back went towards the threat");
            for(auto* unit:wave.fighters)
                if(alive(unit) && party()->members.count(unit->getObjectID()) && unit->isActive())
                    require(unit->wasForced() || blockDistance(unit->getLocation(),point).lround()<=radius,
                        "A member did not withdraw to the fall-back point");
            retire(heavy);
            require(runUntil(BlobWavePolicy::kFallbackMaxMs+2000,[&]{ return phase()==Phase::Reform; }),
                "The party did not reform at the fall-back point");
            require(bot->blobStats.fallbackReturns==1 && party()->rally==point,"The fall-back did not end at its point");
            require(runUntil(BlobWavePolicy::kReformMaxMs+2000,[&]{ return phase()==Phase::March; }),
                "The party did not return to the march after falling back");
            retireAll();
        }

        // ---- 6. Gather timeout ---------------------------------------------------------------
        {
            // Six fighters at home, nine troopers far down the field: well below the stall quorum.
            std::vector<Pick> split(picks.begin(),picks.begin()+6);
            for(int i=0;i<9;++i) split.push_back({Unit_Trooper,{40+(i%3),3+i*2}});
            Wave wave=muster(split);
            // The rally is next to the home group (vehicles anchor the party); the troopers need
            // far longer than kGatherMaxMs to walk back to it.
            bot->launchGroundHunt();
            require(party() && party()->phase==Phase::Gather,"The split wave did not gather");
            const Uint32 start=game.getGameCycleCount();
            require(runUntil(BlobWavePolicy::kGatherMaxMs+2000,[&]{ return phase()!=Phase::Gather; }),"The gather never timed out");
            const double took=(game.getGameCycleCount()-start)*GAMESPEED_DEFAULT/1000.0;
            SDL_Log("BLOB_WAVE_PROBE: gather_timeout after %.1f s gathered=%u of %zu",took,party()->gathered,wave.fighters.size());
            require(bot->blobStats.gatherTimeouts==1,"The gather did not end on its timeout");
            require(took>=BlobWavePolicy::kGatherMaxMs/1000.0-0.1 && took<=BlobWavePolicy::kGatherMaxMs/1000.0+1.0,
                "The gather timeout is not kGatherMaxMs");
            require(party()->members==ids(wave.fighters),"Stragglers were dropped from the party at the timeout");
            retireAll();
        }

        // ---- 7. Determinism: order independence ------------------------------------------------
        {
            Wave wave=muster(picks);
            std::vector<const UnitBase*> forward(wave.fighters.begin(),wave.fighters.end());
            std::vector<const UnitBase*> reverse(forward.rbegin(),forward.rend());
            auto orders=[&]{ std::vector<std::pair<Coord,int>> o; for(auto* u:wave.fighters) o.push_back({u->getDestination(),int(u->getAttackMode())}); return o; };
            bot->startHuntParty(forward,nullptr);
            require(bot->blobParties.size()==1,"The forward party did not start");
            const auto first=bot->blobParties.front();
            const auto firstOrders=orders();
            bot->blobParties.clear();
            for(auto* unit:wave.fighters) { bot->doSetAttackMode(unit,GUARD); bot->doMove2Pos(unit,unit->getX(),unit->getY(),false); }
            bot->startHuntParty(reverse,nullptr);
            require(bot->blobParties.size()==1,"The reversed party did not start");
            const auto& second=bot->blobParties.front();
            require(first.members==second.members && first.rally==second.rally && first.goal==second.goal
                    && first.objective==second.objective,"The hunt party depends on the order of its units");
            require(firstOrders==orders(),"The gather orders depend on the order of the units");
            // Contact is the same enemy whatever order the unit list is in.
            const Coord at=bot->huntPartyAnchor(second)->getLocation();
            std::vector<UnitBase*> pair{spawn(enemy,Unit_Quad,at-origin+Coord(3,1)),spawn(enemy,Unit_Quad,at-origin+Coord(3,-1))};
            publish();
            const ObjectBase* contact=bot->huntPartyContact(second,bot->huntPartyAnchor(second));
            unitList.clear();
            for(auto it=roster.rbegin();it!=roster.rend();++it) unitList.push_back(*it);
            require(contact && contact==bot->huntPartyContact(second,bot->huntPartyAnchor(second)),
                "The contact depends on the order of the unit list");
            publish();
            retireAll();
        }

        // ---- 9. None keeps today's orders; the retreat ablation tracks a Hunt party ---------------
        {
            getQuantBotConfig().waveFormation=QuantBotConfig::kWaveFormationNone;
            Wave wave=muster(picks);
            // The same wave as plain Hunt, for the march comparison: its fast members run ahead.
            bot->launchGroundHunt();
            require(bot->blobParties.empty(),"WaveFormation None made a hunt party");
            for(auto* unit:wave.fighters) require(unit->getAttackMode()==HUNT && !unit->isHuntingToward(),
                "WaveFormation None did not give today's engine-Hunt order");
            int maxLead=0;
            for(int i=0;i<MILLI2CYCLES(60000);++i) { tick(); maxLead=std::max(maxLead,fastLead(wave.fighters)); }
            SDL_Log("BLOB_WAVE_PROBE: none max_fast_lead=%d blob=%d",maxLead,blobLead);
            require(maxLead>blobLead,"Plain Hunt kept its fast members closer than the blob did");
            retireAll();

            bot->tacticalRetreatOverride=1;
            Wave tracked=muster(picks);
            bot->launchGroundHunt();
            require(bot->blobParties.size()==1 && party()->phase==Phase::Fight,"The retreat ablation did not track the Hunt wave");
            for(auto* unit:tracked.fighters) require(unit->getAttackMode()==HUNT,"The retreat ablation changed today's Hunt orders");
            bot->tacticalRetreatOverride=-1;
            retireAll();
        }
    } catch(...) {
        getQuantBotConfig().waveFormation=savedShape;
        getQuantBotConfig().recovery.enabled=savedRecovery;
        bot->tacticalRetreatOverride=-1;
        cleanupStructures(); unitList=savedUnits; throw;
    }
    getQuantBotConfig().waveFormation=savedShape;
    getQuantBotConfig().recovery.enabled=savedRecovery;
    cleanupStructures();
    unitList=savedUnits;
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=blob_waves outcome=passed");
}
