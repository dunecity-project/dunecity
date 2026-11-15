// Real engine regressions for SourceForge bugs 105/86/113/88 and features 62/45.
#include <FileClasses/LoadSavePNG.h>
#include <misc/CursorAppearance.h>
#include <misc/draw_util.h>
void runSourceforgeRegressionsProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok){SDL_Log("SOURCEFORGE_FAIL: %s",why);throw std::runtime_error(why);}};
    auto* owner=pLocalHouse;
    // Make Palace genuinely available so build-list refreshes do not cancel
    // an artificially injected, unavailable campaign-level production item.
    auto& palaceData=game.objectData.data[Structure_Palace][owner->getHouseID()];
    palaceData.techLevel=1;palaceData.upgradeLevel=0;palaceData.prerequisiteStructuresSet.reset();
    Coord plot=Coord::Invalid();
    for(int y=1;y+12<currentGameMap->getSizeY() && plot.isInvalid();++y)
        for(int x=1;x+12<currentGameMap->getSizeX() && plot.isInvalid();++x) {
            bool clear=true;
            for(int dy=0;dy<12;++dy)for(int dx=0;dx<12;++dx)
                if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject())clear=false;
            if(clear)plot=Coord(x,y);
        }
    require(plot.isValid(),"No fixture space");
    auto* cy1=dynamic_cast<ConstructionYard*>(owner->placeStructure(NONE_ID,Structure_ConstructionYard,plot.x,plot.y,true,true));
    auto* cy2=dynamic_cast<ConstructionYard*>(owner->placeStructure(NONE_ID,Structure_ConstructionYard,plot.x+4,plot.y,true,true));
    require(cy1 && cy2,"Missing yards");
    for(auto* cy:{cy1,cy2}) {
        if(!cy->getBuildItem(Structure_Palace))cy->buildList.emplace_back(Structure_Palace,999);
        cy->doProduceItem(Structure_Palace);
    }
    require(!game.getGameInitSettings().getGameOptions().onlyOnePalace,"Fixture must permit multiple palaces");
    const int before=cy2->getProductionQueueSize();
    require(before==1,"Missing second palace order");
    auto* palace=owner->placeStructure(cy1->getObjectID(),Structure_Palace,plot.x,plot.y+5,true,true);
    require(palace && cy2->getProductionQueueSize()==before,"Palace cancelled other yard despite option off");
    // Recreate the same pending orders with the limit enabled before the first placement.
    palace->destroy();
    game.gameInitSettings.gameOptions.onlyOnePalace=true;
    cy1->doProduceItem(Structure_Palace);
    palace=owner->placeStructure(cy1->getObjectID(),Structure_Palace,plot.x,plot.y+5,true,true);
    require(palace && cy2->getProductionQueueSize()==0,"One-palace option failed to cancel duplicate production");
    game.gameInitSettings.gameOptions.onlyOnePalace=false;

    auto* tank=owner->createUnit(Unit_Tank);tank->deploy(plot+Coord(8,8));
    auto* second=owner->createUnit(Unit_Tank);second->deploy(plot+Coord(9,8));
    auto* other=owner->createUnit(Unit_Quad);other->deploy(plot+Coord(10,8));
    auto select=[&](std::initializer_list<UnitBase*> units) {
        game.unselectAll(game.selectedList);game.selectedList.clear();
        for(auto* unit:units){game.selectedList.insert(unit->getObjectID());unit->setSelected(true);}
    };
    auto key=[&](SDL_Keycode code,SDL_Keymod mod=KMOD_NONE) {
        SDL_SetModState(mod);SDL_KeyboardEvent event{};event.type=SDL_KEYDOWN;
        event.keysym.sym=code;event.keysym.mod=mod;game.handleKeyInput(event);SDL_SetModState(KMOD_NONE);
    };
    select({tank,second});
    for(int group=0;group<NUMSELECTEDLISTS;++group) {
        pLocalPlayer->getGroupList(group).insert(tank->getObjectID());
        pLocalPlayer->getGroupList(group).insert(second->getObjectID());
    }
    game.selectedList.insert(0xfffffffe); // A stale ID must not dereference null.
    key(SDLK_0,KMOD_CTRL);
    require(game.selectedList.empty() && !tank->isSelected() && !second->isSelected(),"Ctrl+0 failed to deselect");
    for(int group=0;group<NUMSELECTEDLISTS;++group)
        require(!pLocalPlayer->getGroupList(group).count(tank->getObjectID()) && !pLocalPlayer->getGroupList(group).count(second->getObjectID()),"Ctrl+0 retained group membership");
    game.selectedList.insert(0xfffffffe);key(SDLK_0);require(game.selectedList.empty(),"Plain 0 stale selection failed");
    auto modeCount=[&](){int count=0;for(const auto& [cycle,slot]:game.cmdManager.timeslot)for(const auto& command:slot)if(command.getCommandID()==CMD_UNIT_SETMODE)++count;return count;};
    select({tank});settings.general.wasdCamera=false;
    int count=modeCount();key(SDLK_s);require(modeCount()==count+1,"S did not queue Stop");
    settings.general.wasdCamera=true;count=modeCount();key(SDLK_s);require(modeCount()==count,"WASD S stopped instead of scrolling");
    key(SDLK_s,KMOD_SHIFT);require(modeCount()==count+1,"Shift+S did not stop with WASD");
    for(auto& [cycle,slot]:game.cmdManager.timeslot) {
        for(const auto& command:slot)if(command.getCommandID()==CMD_UNIT_SETMODE)command.executeCommand();
    }
    require(tank->getAttackMode()==STOP,"Queued Stop did not execute");
    settings.general.wasdCamera=false;
    screenborder->setNewScreenCenter((plot+Coord(8,8))*TILESIZE);
    select({tank});key(SDLK_t);
    require(game.selectedList.count(tank->getObjectID()) && game.selectedList.count(second->getObjectID()) && !game.selectedList.count(other->getObjectID()),"T failed matching-type selection");
    Coord distant=Coord::Invalid();
    for(int y=1;y<currentGameMap->getSizeY()-1 && distant.isInvalid();++y)
        for(int x=1;x<currentGameMap->getSizeX()-1 && distant.isInvalid();++x)
            if(!screenborder->isTileInsideScreen(Coord(x,y)) && !currentGameMap->getTile(x,y)->hasAGroundObject())distant=Coord(x,y);
    require(distant.isValid(),"No off-screen selection fixture");
    auto* far=owner->createUnit(Unit_Tank);far->deploy(distant);
    select({tank});key(SDLK_t);require(!game.selectedList.count(far->getObjectID()),"T selected off-screen tank");
    key(SDLK_t,KMOD_CTRL);require(game.selectedList.count(far->getObjectID()),"Ctrl+T omitted off-screen tank");
    select({tank,other});key(SDLK_t,KMOD_CTRL);
    require(game.selectedList.count(second->getObjectID()) && game.selectedList.count(other->getObjectID()),"Mixed-type selection was not preserved");
    const bool timer=game.bShowTime;const auto selection=game.selectedList;key(SDLK_t,KMOD_SHIFT);
    require(game.bShowTime!=timer && game.selectedList==selection,"Shift+T lost timer shortcut");

    House* enemy=nullptr;
    for(int h=0;h<NUM_HOUSES;++h)if(auto* house=game.getHouse(h);house && house->getTeamID()!=owner->getTeamID()){enemy=house;break;}
    require(enemy,"Missing enemy fixture");
    auto* target=enemy->createUnit(Unit_Tank);target->deploy(plot+Coord(8,9));target->setVisible(owner->getTeamID(),true);
    currentGameMap->viewMap(owner->getHouseID(),target->getLocation(),8);
    select({tank});settings.general.leftClickOrders=false;game.selectionMode=false;game.chatMode=false;
    auto hover=[&](Coord pos){drawnMouseX=screenborder->world2screenX(pos.x*TILESIZE+TILESIZE/2);drawnMouseY=screenborder->world2screenY(pos.y*TILESIZE+TILESIZE/2);};
    hover(plot+Coord(11,11));
    require(game.getHoverCursorAction()==CursorAppearance::Action::Pointer,"Ordinary movement shows forced-move cursor");
    key(SDLK_m);
    require(game.currentCursorMode==Game::CursorMode_Move,"M failed to enable explicit move cursor");
    game.setCursorMode(Game::CursorMode_Normal);
    game.pInterface->updateObjectInterface();
    auto* sidebar=dynamic_cast<UnitInterface*>(game.pInterface->pObjectContainer);
    require(sidebar,"Missing move-button fixture");
    sidebar->moveButton.handleMouseLeft(5,5,true);sidebar->moveButton.handleMouseLeft(5,5,false);
    require(game.currentCursorMode==Game::CursorMode_Move,"Move button failed to enable explicit move cursor");
    game.setCursorMode(Game::CursorMode_Normal);
    hover(target->getLocation());
    require(game.getHoverCursorAction()==CursorAppearance::Action::Attack,"Enemy hover missing attack cursor");
    game.handleSelectedObjectsActionClick(target->getX(),target->getY());
    require(game.actionIndicatorObject==target->getObjectID(),"Object command lacks target feedback");
    target->setVisible(owner->getTeamID(),false);
    require(game.getHoverCursorAction()==CursorAppearance::Action::Pointer,"Hidden enemy must retain ordinary pointer");
    require(!tank->getActionClickTarget(target->getX(),target->getY()),"Actual command still targets hidden enemy");
    target->setVisible(owner->getTeamID(),true);
    game.selectionMode=true;require(game.getHoverCursorAction()==CursorAppearance::Action::Pointer,"Drag shows command cursor");game.selectionMode=false;
    hover(second->getLocation());settings.general.leftClickOrders=true;
    require(game.getHoverCursorAction()==CursorAppearance::Action::Pointer,"Friendly left selection shows movement order");
    settings.general.leftClickOrders=false;
    drawnMouseX=getRendererWidth()-1;drawnMouseY=10;
    require(game.getHoverCursorAction()==CursorAppearance::Action::Pointer,"Sidebar shows gameplay action cursor");

    // The real capture must size to physical output even under logical scaling.
    SDL_SetRenderTarget(renderer,nullptr);SDL_RenderSetLogicalSize(renderer,320,240);
    int w=0,h=0;require(SDL_GetRendererOutputSize(renderer,&w,&h)==0,"Missing renderer output");
    auto capture=renderReadSurface(renderer);
    require(capture && capture->w==w && capture->h==h,"Screenshot allocation still uses logical size");
    sdl2::texture_ptr texture{SDL_CreateTexture(renderer,SCREEN_FORMAT,SDL_TEXTUREACCESS_TARGET,257,193)};
    require(texture && SDL_SetRenderTarget(renderer,texture.get())==0,"Missing target texture");
    capture=renderReadSurface(renderer);require(capture && capture->w==257 && capture->h==193,"Screenshot ignores active texture size");
    SDL_SetRenderTarget(renderer,nullptr);
    require(SavePNG_RW(capture.get(),nullptr)==-1,"Screenshot writer accepted invalid output");
    std::string filename;require(saveScreenshot(renderer,filename) && existsFile(filename),"Screenshot not saved in user directory");
    require(filename.find("screenshots/")!=std::string::npos,"Screenshot saved in application working directory");
    SDL_Log("SOURCEFORGE_SCREENSHOT: %s",filename.c_str());
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=sourceforge_regressions outcome=passed");
}
