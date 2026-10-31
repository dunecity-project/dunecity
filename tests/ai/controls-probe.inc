// Exercise the actual input/menu command path in an isolated campaign.
#include <GUI/dune/InGameMenu.h>
#include <GUI/dune/InGameSettingsMenu.h>
#include <GUI/dune/CityBudgetWindow.h>
#include <GUI/QstBox.h>
#include <ScreenBorder.h>
#include <GameInterface.h>
#include <GUI/ObjectInterfaces/UnitInterface.h>
#include <GUI/ObjectInterfaces/ZoneStructureInterface.h>
#include <GUI/ObjectInterfaces/MultiUnitInterface.h>
void runControlsProbe(Game& game) {
    auto require=[](bool ok,const char* why) { if(!ok){SDL_Log("CONTROLS_FAIL: %s",why);throw std::runtime_error(why);} };
    auto commands=[&](CMDTYPE type) {
        int count=0;
        for(const auto& slot:game.cmdManager.timeslot)for(const auto& command:slot)
            if(command.getCommandID()==type)++count;
        return count;
    };
    Coord origin=Coord::Invalid();
    for(int y=4;y+8<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=4;x+8<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(int dy=0;dy<8;++dy)for(int dx=0;dx<8;++dx)
                if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject())clear=false;
            if(clear)origin=Coord(x,y);
        }
    require(origin.isValid(),"Missing input test space");
    auto* unit=pLocalHouse->createUnit(Unit_Tank);unit->deploy(origin);
    unit->setVisible(pLocalHouse->getTeamID(),true);
    for(int dy=0;dy<8;++dy)for(int dx=0;dx<8;++dx)
        currentGameMap->getTile(origin.x+dx,origin.y+dy)->setExplored(pLocalHouse->getHouseID(),game.getGameCycleCount());
    screenborder->setNewScreenCenter(Coord((origin.x+2)*TILESIZE,(origin.y+2)*TILESIZE));
    auto select=[&]() { game.unselectAll(game.selectedList); game.selectedList={unit->getObjectID()};unit->setSelected(true); };
    auto point=[&](Coord tile) { return Coord(screenborder->world2screenX(tile.x*TILESIZE+TILESIZE/2),screenborder->world2screenY(tile.y*TILESIZE+TILESIZE/2)); };
    auto click=[&](Coord tile,int button,int drag=0) {
        const auto p=point(tile);
        require(screenborder->isScreenCoordInsideMap(p.x,p.y),"Input fixture outside map view");
        SDL_Event event{};event.type=SDL_MOUSEBUTTONDOWN;event.button.button=button;event.button.x=p.x;event.button.y=p.y;
        SDL_PushEvent(&event);event.type=SDL_MOUSEBUTTONUP;event.button.x+=drag;SDL_PushEvent(&event);game.doInput();
    };
    SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
    settings.general.leftClickOrders=true; select();
    const int before=commands(CMD_UNIT_MOVE2POS);
    click(origin+Coord(3,2),SDL_BUTTON_LEFT);
    require(commands(CMD_UNIT_MOVE2POS)==before+1,"Left click failed to queue move");
    require(game.selectedList.count(unit->getObjectID()),"Order lost selection");
    click(origin+Coord(3,2),SDL_BUTTON_LEFT,24);
    require(commands(CMD_UNIT_MOVE2POS)==before+1,"Drag selection issued an order");
    select();SDL_SetModState(KMOD_SHIFT);click(origin+Coord(3,2),SDL_BUTTON_LEFT);SDL_SetModState(KMOD_NONE);
    require(commands(CMD_UNIT_MOVE2POS)==before+1,"Shift selection issued an order");
    select();click(origin,SDL_BUTTON_LEFT);
    require(commands(CMD_UNIT_MOVE2POS)==before+1 && game.selectedList.count(unit->getObjectID()),"Friendly click moved instead of selecting");
    click(origin+Coord(3,2),SDL_BUTTON_RIGHT);
    require(game.selectedList.empty() && commands(CMD_UNIT_MOVE2POS)==before+1,"Right click did not cancel selection");
    settings.general.leftClickOrders=false;select();click(origin+Coord(3,2),SDL_BUTTON_RIGHT);
    require(commands(CMD_UNIT_MOVE2POS)==before+2,"Default right-click order regressed");
    House* enemyHouse=nullptr;
    for(int h=0;h<NUM_HOUSES;++h)
        if(auto* candidate=game.getHouse(h); candidate && candidate->getTeamID()!=pLocalHouse->getTeamID()) { enemyHouse=candidate;break; }
    require(enemyHouse,"Missing enemy house fixture");
    auto* enemy=enemyHouse->createUnit(Unit_Tank);
    enemy->deploy(origin+Coord(4,3));enemy->setVisible(pLocalHouse->getTeamID(),true);
    settings.general.leftClickOrders=true;select();
    const auto attacks=commands(CMD_UNIT_ATTACKOBJECT);
    click(origin+Coord(4,3),SDL_BUTTON_LEFT);
    require(commands(CMD_UNIT_ATTACKOBJECT)==attacks+1,"Left click did not attack enemy");
    unit->setDestination(origin+Coord(3,2));
    unit->pathList={origin+Coord(1,0),origin+Coord(2,1),origin+Coord(3,2)};
    const SDL_Rect clip{10,30,200,180};SDL_RenderSetClipRect(renderer,&clip);
    SDL_SetRenderDrawColor(renderer,3,7,11,255);
    game.drawMovementPaths();
    SDL_Rect after{};SDL_RenderGetClipRect(renderer,&after);
    Uint8 r,g,b,a;SDL_GetRenderDrawColor(renderer,&r,&g,&b,&a);
    require(SDL_RectEquals(&clip,&after) && r==3 && g==7 && b==11 && a==255,"Paths changed scene render state");
    SDL_RenderSetClipRect(renderer,nullptr);
    // Sidebar toggles operate on the same persistent local setting as Options.
    game.pInterface->updateObjectInterface();
    auto* single=dynamic_cast<UnitInterface*>(game.pInterface->pObjectContainer);
    require(single,"Missing selected-unit sidebar");
    settings.general.showMovementPaths=true;single->update();
    auto toggle=[&](auto& button) { button.handleMouseLeft(5,5,true);button.handleMouseLeft(5,5,false); };
    toggle(single->movementPathsButton);single->update();
    require(!settings.general.showMovementPaths && !single->movementPathsButton.getToggleState(),"Sidebar failed to hide paths");
    INIFile saved(getConfigFilepath());
    require(!saved.getBoolValue("General","Movement Paths",true),"Sidebar path preference was not saved");
    toggle(single->movementPathsButton);single->update();
    require(settings.general.showMovementPaths,"Sidebar failed to show paths");
    auto capture=[&](const char* name) {
        game.drawScreen();
        int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);
        sdl2::surface_ptr image{SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888)};
        require(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,image->pixels,image->pitch)==0,"Sidebar capture failed");
        SDL_SaveBMP(image.get(),name);
        SDL_RenderPresent(renderer);
    };
    capture("sidebar-single.bmp");
    auto* second=pLocalHouse->createUnit(Unit_Tank);second->deploy(origin+Coord(1,1));
    game.selectedList.insert(second->getObjectID());second->setSelected(true);
    game.pInterface->updateObjectInterface();
    auto* multi=dynamic_cast<MultiUnitInterface*>(game.pInterface->pObjectContainer);
    require(multi,"Missing group sidebar");toggle(multi->movementPathsButton);multi->update();
    require(!settings.general.showMovementPaths && !multi->movementPathsButton.getToggleState(),"Group path toggle is out of sync");
    capture("sidebar-group.bmp");
    // Exercise the maximum action count at the 640x480 fixture size.
    for(auto* button : {&multi->moveButton,&multi->attackButton,&multi->movementPathsButton,
            &multi->carryallDropButton,&multi->captureButton,&multi->returnButton,
            &multi->deployButton,&multi->destructButton,&multi->healButton,&multi->sendToRepairButton})
        button->setVisible(true);
    multi->actionBar.refresh();
    require(multi->actionBar.getSize().y==82,"Full action bar must wrap into three rows");
    capture("sidebar-all-actions.bmp");
    game.selectedList.clear();game.pInterface->updateObjectInterface();
    toggle(game.pInterface->movementPathsButton);game.pInterface->updateObjectInterface();
    require(settings.general.showMovementPaths,"Empty sidebar path toggle failed");
    capture("sidebar-empty.bmp");
    settings.general.wasdCamera=true;select();
    SDL_KeyboardEvent key{};key.type=SDL_KEYDOWN;key.keysym.sym=SDLK_a;
    game.handleKeyInput(key);require(game.currentCursorMode==Game::CursorMode_Normal,"WASD A activated attack");
    key.keysym.mod=KMOD_SHIFT;game.handleKeyInput(key);
    require(game.currentCursorMode==Game::CursorMode_Attack,"Shift+A cannot attack with WASD enabled");
    game.setCursorMode(Game::CursorMode_Normal);
    require(game.canSkipMission(),"Fixture cannot skip campaign");
    game.onOptions();
    capture("pause-menu.bmp");
    auto* pause=dynamic_cast<InGameMenu*>(game.pInGameMenu.get());
    pause->onSettings();pause->processChildWindowOpenCloses();
    capture("game-settings.bmp");
    pause->closeChildWindow();pause->processChildWindowOpenCloses();
    pause->onSave();pause->processChildWindowOpenCloses();
    capture("save-game.bmp");
    pause->closeChildWindow();pause->processChildWindowOpenCloses();
    if(game.isCitySimEnabled()) {
        game.pInGameMenu=std::make_unique<CityBudgetWindow>();
        capture("city-budget.bmp");
        auto* budget=static_cast<CityBudgetWindow*>(game.pInGameMenu.get());
        for(const auto* text : {"Police stations: 83 | -8300/yr", "Rocket turrets: 376 | -5640/yr",
                               "Net Annual: +86305.0/yr", "Projected Tax: +102245/yr",
                               "(lost: no Refinery/Silo)", "Storage: 99999/99999"})
            require(GUIStyle::getInstance().getTextWidth(text,14)<=budget->forecastVBox.getSize().x,
                    "Established-city forecast text exceeds its column");
        require(budget->getSize().x<=getRendererWidth() && budget->getSize().y<=getRendererHeight(),
                "Budget window exceeds the renderer");
    }
    game.pInGameMenu.reset();game.bMenu=false;game.resumeGame();
    if(game.isCitySimEnabled()) {
        const Coord site=origin+Coord(5,0);
        for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx)
            currentGameMap->getTile(site.x+dx,site.y+dy)->setType(Terrain_Rock);
        auto* zone=dynamic_cast<ZoneStructure*>(pLocalHouse->placeStructure(NONE_ID,Structure_ZoneResidential,site.x,site.y,false,true));
        require(zone,"Missing selected-zone fixture");
        game.unselectAll(game.selectedList);game.selectedList={zone->getObjectID()};zone->setSelected(true);
        auto showZone=[&](const char* name) {
            zone->updateStructureSpecificStuff();
            game.pInterface->updateObjectInterface();game.pInterface->updateObjectInterface();
            capture(name);
        };
        auto previewPixels=[&](bool expected) {
            SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
            SDL_Rect bounds{0,0,64,64};
            if(expected) {
                auto* texture=pGFXManager->getObjPic(zone->graphicID,pLocalHouse->getHouseID())[1];
                auto source=calcSpriteSourceRect(texture,zone->curAnimFrame%zone->numImagesX,zone->numImagesX,
                                                zone->curAnimFrame/zone->numImagesX,zone->numImagesY);
                SDL_RenderCopy(renderer,texture,&source,&bounds);
            } else zone->drawPreview(bounds);
            std::vector<Uint32> pixels(64*64);
            require(SDL_RenderReadPixels(renderer,&bounds,SDL_PIXELFORMAT_ARGB8888,pixels.data(),64*4)==0,"Preview capture failed");
            return pixels;
        };
        showZone("zone-vacant.bmp");const auto vacant=previewPixels(false);
        zone->setResidentialPopulation(40);showZone("zone-developed.bmp");
        const auto developed=previewPixels(false);
        require(developed!=vacant,"Sidebar preview did not follow zone development");
        require(developed==previewPixels(true),"Sidebar preview differs from the selected map sprite");
        auto* panel=dynamic_cast<ZoneStructureInterface*>(game.pInterface->pObjectContainer);
        require(panel && !panel->objPicture.isVisible(),"Generic zone construction icon is still displayed");
        require(panel->zoneNameLabel.getTextFontSize()==14 && panel->cityStats_.populationLabel_.getText()==" Pop: 40",
                "Zone statistics remain small or repeat the density");
        for(auto overlay:{ZoneStructure::CivicOverlay::Hospital,ZoneStructure::CivicOverlay::Church}) {
            zone->setCivicOverlay(overlay);showZone(overlay==ZoneStructure::CivicOverlay::Hospital ? "zone-hospital.bmp" : "zone-church.bmp");
            require(previewPixels(false)==previewPixels(true),"Civic preview does not match the selected hospital/church sprite");
            require(panel->zoneNameLabel.getText()==(overlay==ZoneStructure::CivicOverlay::Hospital ? " Hospital" : " Church"),
                    "Civic building still has the generic residential label");
        }
        game.unselectAll(game.selectedList);zone->demolish();game.pInterface->updateObjectInterface();
    }
    game.onSkipMission();
    auto* menu=dynamic_cast<InGameMenu*>(game.pInGameMenu.get());require(menu,"Missing pause menu");menu->processChildWindowOpenCloses();
    auto* question=dynamic_cast<QstBox*>(menu->pChildWindow);
    require(question && question->button2.isActive() && commands(CMD_CAMPAIGN_SKIP)==0,"Skip was not safely confirmed");
    capture("skip-confirmation.bmp");
    question->onButton(QSTBOX_BUTTON2);menu->processChildWindowOpenCloses();
    require(commands(CMD_CAMPAIGN_SKIP)==0,"Cancel skipped mission");
    menu->onSkipMission();menu->processChildWindowOpenCloses();
    question=dynamic_cast<QstBox*>(menu->pChildWindow);require(question,"Missing second confirmation");
    question->onButton(QSTBOX_BUTTON1);menu->processChildWindowOpenCloses();
    require(commands(CMD_CAMPAIGN_SKIP)==1,"Confirm did not queue exactly one skip");
    game.bReplay=true;game.confirmSkipMission();
    require(commands(CMD_CAMPAIGN_SKIP)==1,"Skip bypassed replay restriction");game.bReplay=false;
    SDL_Log("CONTROLS_PASS: left/default orders, drag/shift/friendly selection, cancel, WASD attack binding, confirmed skip");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=controls outcome=passed");
}
