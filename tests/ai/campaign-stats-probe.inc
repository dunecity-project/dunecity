// Real results-screen regression, included only by the diagnostic main.
#include <Menu/CampaignStatsMenu.h>
#include <Menu/CustomGameStatsMenu.h>
#include <units/Harvester.h>
#include <FileClasses/LoadSavePNG.h>
#include <misc/draw_util.h>
#include <misc/OMemoryStream.h>
#include <misc/IMemoryStream.h>
#include <array>
#include <climits>
#include <cstdlib>

void runTaxReceiptsProbe(Game& game) {
    House source(HOUSE_HARKONNEN, 100, 75, 12, 1, 321);
    source.addCredits(123, true);
    source.addCityTaxReceipts(FixPoint(2501) / 2);
    source.addCityCredits(900);
    source.takeCredits(750);
    source.addCityCredits(-400);
    source.returnCredits(20);
    source.addCityTaxReceipts(-10);
    if(source.getCityTaxReceipts() != FixPoint(2501) / 2 || source.getHarvestedSpice() != 123)
        throw std::runtime_error("Tax receipts changed with spending/refunds or became spice");
    OMemoryStream bytes; bytes.open(); source.save(bytes); bytes.writeUint32(0x74617831);
    const auto version = game.loadedSavegameVersion;
    game.loadedSavegameVersion = SAVEGAMEVERSION;
    IMemoryStream input(bytes.getData(), bytes.getDataLength());
    {
        House restored(input);
        if(restored.getCityTaxReceipts() != source.getCityTaxReceipts() || restored.quota != 321
           || restored.getHarvestedSpice() != 123 || input.readUint32() != 0x74617831)
            throw std::runtime_error("Tax save roundtrip or following fields changed");
    }
    // A 9840 House lacks both the tax slot and the measured deviation counters.
    std::vector<char> legacy(bytes.getData(), bytes.getData() + bytes.getDataLength());
    // The serialized prefix is 92 bytes, followed by 56 bytes per item reward record.
    OMemoryStream counterBytes; counterBytes.open(); source.getDeviationCounters().save(counterBytes);
    const size_t counterOffset = 92 + 56 * Num_ItemID;
    legacy.erase(legacy.begin() + counterOffset,
                 legacy.begin() + counterOffset + counterBytes.getDataLength());
    legacy.erase(legacy.begin() + 27, legacy.begin() + 35);
    game.loadedSavegameVersion = 9840;
    IMemoryStream oldInput(legacy.data(), legacy.size());
    {
        House restored(oldInput);
        if(restored.getCityTaxReceipts() != 0 || restored.quota != 321
           || restored.getCityCredits() != source.getCityCredits()
           || restored.getHarvestedSpice() != 123 || oldInput.readUint32() != 0x74617831)
            throw std::runtime_error("Legacy House save invented tax or lost stream alignment");
    }
    game.loadedSavegameVersion = version;
    source.addCityTaxReceipts(FixPoint(2000000000));
    source.addCityTaxReceipts(FixPoint(2000000000));
    if(source.getCityTaxReceipts() != House::MAX_CITY_TAX_RECEIPTS)
        throw std::runtime_error("Long-running tax total overflowed");
    SDL_Log("TAX_RECEIPTS_PROBE_PASS: gross receipts, separate spice, save roundtrip, 9840 default and saturation");
}

void runCampaignStatsProbe(Game& game, int level) {
    runTaxReceiptsProbe(game);
    auto* local = game.getHouse(HOUSE_HARKONNEN);
    auto* enemy = game.getHouse(HOUSE_ATREIDES);
    auto* ally = game.getHouse(HOUSE_ORDOS);
    if (!local || !enemy || !ally || local != pLocalHouse || !local->isAI())
        throw std::runtime_error("Stats fixture requires a human house with a QuantBot partner");

    for (int h=0; h<NUM_HOUSES; ++h) if (auto* house = game.getHouse(h)) {
        house->numDestroyedUnits = house->numDestroyedStructures = house->destroyedValue = 0;
        house->harvestedSpice = 0;
        house->cityTaxReceipts = 0;
        house->takeCredits(house->getCredits());
    }
    for (auto* unit : unitList) if (auto* harvester = dynamic_cast<Harvester*>(unit)) harvester->spice = 0;
    local->numDestroyedUnits = 7; local->numDestroyedStructures = 3;
    local->destroyedValue = 220; local->harvestedSpice = 1200; local->addCredits(900);
    local->addCityTaxReceipts(FixPoint(2469) / 2);
    enemy->addCityTaxReceipts(700);
    enemy->numDestroyedUnits = 5; enemy->numDestroyedStructures = 2;
    enemy->destroyedValue = 70; enemy->harvestedSpice = 700;
    auto* cargo = dynamic_cast<Harvester*>(local->createUnit(Unit_Harvester,true));
    auto* enemyCargo = dynamic_cast<Harvester*>(enemy->createUnit(Unit_Harvester,true));
    if (!cargo || !enemyCargo) throw std::runtime_error("Stats fixture harvesters failed");
    cargo->spice = 100; enemyCargo->spice = 75;

    // An ordinary human campaign supplies the unchanged score/rank baseline.
    local->ai = false;
    CampaignStatsMenu human(level);
    const int ordinaryScore = human.totalScore;
    const auto ordinaryRank = human.rank;
    local->ai = true;
    CampaignStatsMenu assisted(level);
    const bool duneCity = game.getGameInitSettings().getModName() == "dunecity";
    if(assisted.showTaxStatistics != duneCity || assisted.taxCollectedByHuman != 1234.5
       || assisted.taxCollectedByAI != 700)
        throw std::runtime_error("Results tax totals or Dune City gating incorrect");
    const auto originalMod = game.gameInitSettings.modName;
    const bool originalCity = game.citySimEnabled_;
    game.citySimEnabled_ = true; // Tornie also has a city simulation.
    for(const auto* mod : {"vanilla", "Tornie", "Dune2R"}) {
        game.gameInitSettings.modName = mod;
        CampaignStatsMenu other(level);
        if(other.showTaxStatistics) throw std::runtime_error("Tax row leaked into another mod");
    }
    game.gameInitSettings.modName = originalMod;
    game.citySimEnabled_ = originalCity;
    if (assisted.unitsDestroyedByHuman != 7 || assisted.structuresDestroyedByHuman != 3
        || assisted.spiceHarvestedByHuman != 1300 || assisted.unitsDestroyedByAI != 5
        || assisted.structuresDestroyedByAI != 2 || assisted.spiceHarvestedByAI != 775
        || assisted.totalScore != ordinaryScore || assisted.rank != ordinaryRank)
        throw std::runtime_error("AI partner changed ownership of campaign score or statistics");

    // Controller type must not misclassify a human opponent or an AI ally either.
    enemy->ai = false;
    ally->teamID = local->getTeamID();
    ally->numDestroyedUnits = 2; ally->numDestroyedStructures = 1;
    ally->harvestedSpice = 300; ally->destroyedValue = 40; ally->addCredits(200);
    int alliedBuildingScore = 0;
    for (const auto* building : structureList) if (building->getOwner() == ally)
        alliedBuildingScore += game.objectData.data[building->getItemID()][building->getOriginalHouseID()].price / 100;
    ally->addCityTaxReceipts(500);
    CampaignStatsMenu teams(level);
    if(teams.taxCollectedByHuman != 1734.5 || teams.taxCollectedByAI != 700)
        throw std::runtime_error("Tax statistics did not follow teams");
    if (teams.unitsDestroyedByHuman != 9 || teams.structuresDestroyedByHuman != 4
        || teams.spiceHarvestedByHuman != 1600 || teams.unitsDestroyedByAI != 5
        || teams.structuresDestroyedByAI != 2 || teams.spiceHarvestedByAI != 775
        || teams.totalScore != ordinaryScore + 42 + alliedBuildingScore)
        throw std::runtime_error("Campaign statistics did not follow team ownership");

    const auto size = assisted.getSize();
    sdl2::texture_ptr target(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, size.x, size.y));
    if (!target || SDL_SetRenderTarget(renderer,target.get()) != 0)
        throw std::runtime_error("Stats render target failed");
    SDL_RenderSetLogicalSize(renderer,size.x,size.y);
    SDL_SetRenderDrawColor(renderer,0,0,0,255); SDL_RenderClear(renderer);
    for(int step=0; step<40 && assisted.currentState != CampaignStatsMenu::State_Finished; ++step) assisted.doState(INT_MAX);
    if(assisted.currentState != CampaignStatsMenu::State_Finished)
        throw std::runtime_error("Tax animation never finished");
    if(duneCity && (!assisted.taxYouLabel.isVisible() || assisted.taxYouLabel.getText() != "1235"
                   || assisted.taxEnemyLabel.getText() != "700"))
        throw std::runtime_error("Tax animation did not display collected credits");
    assisted.draw();
    auto screenshot = renderReadSurface(renderer);
    if (SavePNG(screenshot.get(),"campaign-assisted-stats.png") != 0)
        throw std::runtime_error("Stats screenshot failed");

    // The debriefing widgets are positioned against the classic FAME.CPS panel
    // (640x400, centred). A flat generated background has no panel edge, so
    // look for the hard contrast step where the panel meets the wallpaper.
    if (size.x >= 720 && size.y >= 440) {
        auto channels = [&](int x,int y) {
            Uint8 r=0,g=0,b=0;
            SDL_GetRGB(getPixel(screenshot.get(),x,y),screenshot->format,&r,&g,&b);
            return std::array<int,3>{r,g,b};
        };
        const int edgeY = size.y/2 - 180;   // panel rows beside the crests
        const auto outside = channels(size.x/2 - 324, edgeY);
        const auto inside  = channels(size.x/2 - 316, edgeY);
        int contrast = 0;
        for (int c=0; c<3; ++c) contrast += std::abs(outside[c]-inside[c]);
        if (contrast < 24)
            throw std::runtime_error("Results screen lost the classic stats panel background");
    }
    SDL_SetRenderTarget(renderer,nullptr);
    // The custom debriefing reports gross receipts independently for all houses.
    for(int h=0;h<6;++h) if(!game.house[h])
        game.house[h]=std::make_unique<House>(static_cast<HOUSETYPE>(h),0,0,0,h+1,0);
    local->addCityTaxReceipts(1000000); // Long values must fit the compact layout.
    auto originalBackground=std::move(pGFXManager->pBackgroundSurface);
    for(const Point dimensions : {Point(640,480),Point(854,480),Point(1280,720)}) {
        pGFXManager->pBackgroundSurface.reset(SDL_CreateRGBSurfaceWithFormat(0,dimensions.x,dimensions.y,32,SCREEN_FORMAT));
        SDL_BlitScaled(originalBackground.get(),nullptr,pGFXManager->pBackgroundSurface.get(),nullptr);
        CustomGameStatsMenu custom;
        if(custom.showTaxStatistics!=duneCity || (duneCity &&
           (custom.houseStat[HOUSE_HARKONNEN].values[3].getText()!="1001235"
            || custom.houseStat[HOUSE_ATREIDES].values[3].getText()!="700")))
            throw std::runtime_error("Custom results tax totals or mod gating incorrect");
        if(custom.playerStatListVBox.getMinimumSize().x>dimensions.x-48)
            throw std::runtime_error("Custom results columns overflow the viewport");
        sdl2::texture_ptr customTarget(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_TARGET,dimensions.x,dimensions.y));
        if(!customTarget || SDL_SetRenderTarget(renderer,customTarget.get())!=0)
            throw std::runtime_error("Custom results render target failed");
        SDL_RenderSetLogicalSize(renderer,dimensions.x,dimensions.y);
        custom.draw();
        auto customImage=renderReadSurface(renderer);
        const auto path="custom-stats-"+std::to_string(dimensions.x)+".png";
        if(SavePNG(customImage.get(),path.c_str())!=0)
            throw std::runtime_error("Custom stats screenshot failed");
        SDL_SetRenderTarget(renderer,nullptr);
    }
    pGFXManager->pBackgroundSurface=std::move(originalBackground);
    SDL_Log("CAMPAIGN_STATS_PROBE_PASS: AI partner retains human score/rank, harvest cargo, kills and buildings; allies and human opponents classified by team");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=stats outcome=passed");
}
