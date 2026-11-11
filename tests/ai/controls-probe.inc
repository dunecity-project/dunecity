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
#include <GUI/ObjectInterfaces/BuilderInterface.h>
#include <GUI/dune/BuilderList.h>
#include <structures/ConstructionYard.h>
#include <misc/ZoomSteps.h>
#include <cstring>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef MAC_TRACKPAD_PROBE
extern "C" void queueMacTrackpadScroll(int, int, double, double, bool, bool);
extern "C" void queueMacTrackpadMonitorPan(int, int, double, double);
extern "C" void queueMacTrackpadPinch(int, int, double, int);
#endif
void runControlsProbe(Game& game) {
    auto require=[](bool ok,const char* why) { if(!ok){SDL_Log("CONTROLS_FAIL: %s",why);throw std::runtime_error(why);} };
    auto commands=[&](CMDTYPE type) {
        int count=0;
        for(const auto& [cycle,slot]:game.cmdManager.timeslot)for(const auto& command:slot)
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
    // Real SDL events through Game::doInput, on the ordinary (non-Dune2R) map.
    const int originalZoom = game.zoomStep;
    const Coord originalCenter = screenborder->getCurrentCenter();
    const SDL_Rect mapArea = screenborder->getScreenRect();
    const Coord pointer(mapArea.x + mapArea.w/2 + 24, mapArea.y + mapArea.h/2 + 16);
#ifdef __APPLE__
    // sdl2-compat's SDL_PushEvent drops preciseX/Y and integer tick fields when
    // converting SDL2 to SDL3. Inject complete native wheel input into that same
    // queue, as hardware does, rather than weakening precision/sidebar assertions.
    // SDL3's stable event ABI: SDL_events.h (3.2+), 128-byte event union.
    struct NativeWheel {
        Uint32 type, reserved;
        Uint64 timestamp;
        Uint32 windowID, which;
        float x, y;
        Uint32 direction;
        float mouseX, mouseY;
        Sint32 integerX, integerY;
    };
    union NativeEvent { NativeWheel wheel; Uint8 padding[128]; };
    static_assert(sizeof(NativeEvent)==128);
    using NativePush = bool (*)(NativeEvent*);
    NativePush nativePush=nullptr;
    std::unique_ptr<void, decltype(&SDL_UnloadObject)> nativeLibrary(nullptr,SDL_UnloadObject);
    SDL_version runtime{}; SDL_GetVersion(&runtime);
    if(runtime.major==2 && runtime.minor==32 && runtime.patch>=50) {
        for(Uint32 image=0;image<_dyld_image_count();++image) {
            const char* path=_dyld_get_image_name(image);
            if(path && (std::strstr(path,"/libSDL3") || std::strstr(path,"/SDL3.framework/"))) {
                nativeLibrary.reset(SDL_LoadObject(path));
                break;
            }
        }
        require(nativeLibrary!=nullptr,"Cannot open native SDL3 wheel injector");
        nativePush=reinterpret_cast<NativePush>(SDL_LoadFunction(nativeLibrary.get(),"SDL_PushEvent"));
        require(nativePush!=nullptr,"Cannot resolve native SDL3 wheel injector");
        SDL_Log("MOUSE_WHEEL_BACKEND: sdl2-compat; native SDL3 fractional event queue");
    }
#endif
    auto motion = [&](Coord p) {
        SDL_Event e{}; e.type=SDL_MOUSEMOTION; e.motion.x=p.x; e.motion.y=p.y;
        SDL_PushEvent(&e); game.doInput();
    };
    auto wheel = [&](int y, float precise=0) {
        SDL_Event e{}; e.type=SDL_MOUSEWHEEL; e.wheel.y=y;
#if SDL_VERSION_ATLEAST(2, 0, 18)
        e.wheel.preciseY=precise;
#endif
#ifdef __APPLE__
        if(nativePush) {
            NativeEvent native{};
            native.wheel.type=SDL_MOUSEWHEEL;
            native.wheel.windowID=SDL_GetWindowID(window);
            native.wheel.y=precise!=0 ? precise : static_cast<float>(y);
            native.wheel.integerY=y;
            native.wheel.mouseX=drawnMouseX;
            native.wheel.mouseY=drawnMouseY;
            require(nativePush(&native),"Native wheel event was rejected");
        } else
#endif
        {
            require(SDL_PushEvent(&e)==1,"SDL wheel event was rejected");
        }
        game.doInput();
    };
    auto middle = [&](Coord p, bool down) {
        SDL_Event e{}; e.type=down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        e.button.button=SDL_BUTTON_MIDDLE; e.button.x=p.x; e.button.y=p.y;
        SDL_PushEvent(&e); game.doInput();
    };
    auto totalCommands = [&]() {
        size_t count=0; for(const auto& [cycle,slot]:game.cmdManager.timeslot) count+=slot.size(); return count;
    };
    select();
    screenborder->setNewScreenCenter(Coord(currentGameMap->getSizeX()*TILESIZE/2,
                                            currentGameMap->getSizeY()*TILESIZE/2));
    currentGameMap->viewMap(pLocalHouse->getHouseID(),
        Coord(currentGameMap->getSizeX()/2,currentGameMap->getSizeY()/2),24);
    game.setZoomStep(4,pointer.x,pointer.y,false);
    motion(pointer);
    const Coord anchor(screenborder->screen2worldX(pointer.x),screenborder->screen2worldY(pointer.y));
    capture("mouse-zoom-before.bmp");
    wheel(1); require(game.zoomStep==5,"Map wheel up did not zoom exactly one step");
    const Coord anchored(screenborder->screen2worldX(pointer.x),screenborder->screen2worldY(pointer.y));
    require(std::abs(anchor.x-anchored.x)<=4 && std::abs(anchor.y-anchored.y)<=4,"Wheel zoom lost mouse anchor");
    capture("mouse-zoom-after.bmp");
    wheel(-1); require(game.zoomStep==4,"Map wheel down did not zoom exactly one step");
    for(int i=0;i<ZoomSteps::kCount+2;++i) wheel(1);
    require(game.zoomStep==ZoomSteps::kCount-1,"Wheel zoom did not clamp at maximum");
    for(int i=0;i<ZoomSteps::kCount+2;++i) wheel(-1);
    require(game.zoomStep==0,"Wheel zoom did not clamp at minimum");
#if SDL_VERSION_ATLEAST(2, 0, 18)
    screenborder->resetMouseWheelZoom();
    wheel(0,.25f); wheel(0,.25f); wheel(0,.25f);
    require(game.zoomStep==0,"Fractional high-resolution wheel zoomed before one notch accumulated");
    wheel(0,.25f);
    require(game.zoomStep==1,"Fractional high-resolution wheel failed to accumulate");
    for(int i=0;i<20;++i) wheel(0,.25f);
    require(game.zoomStep==1,"One high-resolution wheel burst jumped multiple zoom levels");
    screenborder->resetMouseWheelZoom();
#endif
#ifdef MAC_TRACKPAD_PROBE
    const size_t commandsBeforeTrackpad=totalCommands();
    auto pinch = [&](double magnification, int phase) {
        queueMacTrackpadPinch(pointer.x,pointer.y,magnification,phase);
        game.doInput();
    };
    game.setZoomStep(4,pointer.x,pointer.y,false);
    screenborder->setNewScreenCenter(Coord(currentGameMap->getSizeX()*TILESIZE/2,
                                            currentGameMap->getSizeY()*TILESIZE/2));
    motion(pointer);
    const Coord pinchAnchor(screenborder->screen2worldX(pointer.x),screenborder->screen2worldY(pointer.y));
    pinch(0,1); pinch(.25,4); pinch(0,8);
    require(game.zoomStep==5,"Native trackpad spread failed to zoom one step");
    const Coord pinchAfter(screenborder->screen2worldX(pointer.x),screenborder->screen2worldY(pointer.y));
    require(std::abs(pinchAnchor.x-pinchAfter.x)<=4 && std::abs(pinchAnchor.y-pinchAfter.y)<=4,
            "Native trackpad pinch lost cursor anchor");
    const Coord trackpadCenter=screenborder->getCurrentCenter();
    const Coord trackpadExpected=trackpadCenter+Coord(
        screenborder->screen2worldX(pointer.x+24)-screenborder->screen2worldX(pointer.x),
        screenborder->screen2worldY(pointer.y+18)-screenborder->screen2worldY(pointer.y));
    queueMacTrackpadMonitorPan(pointer.x,pointer.y,-24,-18);
    game.doInput();
    require(game.zoomStep==5 && screenborder->getCurrentCenter()==trackpadExpected,
            "Two-finger trackpad scroll zoomed or failed to pan expected pixels");
    const Coord reverseCenter=screenborder->getCurrentCenter();
    const Coord reverseExpected=reverseCenter+Coord(
        screenborder->screen2worldX(pointer.x-24)-screenborder->screen2worldX(pointer.x),
        screenborder->screen2worldY(pointer.y-18)-screenborder->screen2worldY(pointer.y));
    queueMacTrackpadMonitorPan(pointer.x,pointer.y,24,18);
    game.doInput();
    require(game.zoomStep==5 && screenborder->getCurrentCenter()==reverseExpected,
            "Opposite two-finger scroll did not reverse both pan axes without zoom");
    pinch(0,1); pinch(-.25,4); pinch(0,8);
    require(game.zoomStep==4,"Native trackpad pinch-together failed to zoom out");
    queueMacTrackpadScroll(pointer.x,pointer.y,0,1,false,false);
    game.doInput();
    require(game.zoomStep==5,"Native physical mouse wheel no longer zooms");
    queueMacTrackpadScroll(pointer.x,pointer.y,0,10,true,false);
    game.doInput();
    require(game.zoomStep==6,"Unphased high-resolution physical wheel was mistaken for trackpad pan");
    game.setZoomStep(4,pointer.x,pointer.y,false);
    pinch(0,1); pinch(.1,4);
    require(game.zoomStep==4,"Sub-threshold native pinch zoomed early");
    pinch(.1,4);
    require(game.zoomStep==5,"Split native pinch failed to multiply incremental scale");
    pinch(.2,4);
    require(game.zoomStep==6,"Accumulated native pinch lost a second zoom step");
    pinch(0,8);
    game.setZoomStep(4,pointer.x,pointer.y,false);
    pinch(0,1); pinch(.21,4); pinch(.2,4); pinch(0,8);
    require(game.zoomStep==6,"Equivalent single/split native pinch produced different zoom");
    for(int terminal : {8,16}) {
        game.setZoomStep(4,pointer.x,pointer.y,false);
        pinch(0,1); pinch(.1,4); pinch(0,terminal);
        pinch(0,1); pinch(.1,4);
        require(game.zoomStep==4,"Ended/cancelled native pinch retained stale partial scale");
        pinch(0,8);
    }
    const auto& overlay=game.pInterface->getMapViewControlsRect();
    require(overlay.w>0 && overlay.h>0,"Missing map-overlay gesture fixture");
    const Coord overlayPoint(overlay.x+overlay.w/2,overlay.y+overlay.h/2);
    const Coord overlayCenter=screenborder->getCurrentCenter();
    const int overlayZoom=game.zoomStep;
    motion(overlayPoint);
    queueMacTrackpadScroll(overlayPoint.x,overlayPoint.y,0,60,true,true);
    require(SDL_HasEvent(SDL_MOUSEWHEEL)==SDL_FALSE,"Trackpad overlay scroll leaked an SDL wheel fallback");
    queueMacTrackpadPinch(overlayPoint.x,overlayPoint.y,0,1);
    queueMacTrackpadPinch(overlayPoint.x,overlayPoint.y,.5,4);
    queueMacTrackpadPinch(overlayPoint.x,overlayPoint.y,0,8);
    game.doInput();
    require(game.zoomStep==overlayZoom && screenborder->getCurrentCenter()==overlayCenter,
            "Trackpad gesture over map-overlay controls moved or zoomed the map");
    motion(pointer);
    require(totalCommands()==commandsBeforeTrackpad && game.selectedList.count(unit->getObjectID()),
            "Trackpad camera gesture changed selection or issued a command");
    SDL_Log("MAC_TRACKPAD_PASS: native spread/pinch cursor anchor, buttonless phased pan without zoom, physical wheel zoom, no commands");
#endif
    const size_t commandsBeforePan=totalCommands();
    for(int step=0;step<ZoomSteps::kCount;++step) {
        game.setZoomStep(step,pointer.x,pointer.y,false);
        screenborder->setNewScreenCenter(Coord(currentGameMap->getSizeX()*TILESIZE/2,
                                                currentGameMap->getSizeY()*TILESIZE/2));
        motion(pointer);
        const Coord center=screenborder->getCurrentCenter();
        const Coord end=pointer+Coord(-24,18);
        const Coord expected=center+Coord(screenborder->screen2worldX(pointer.x)-screenborder->screen2worldX(end.x),
                                          screenborder->screen2worldY(pointer.y)-screenborder->screen2worldY(end.y));
        middle(pointer,true);
        require(screenborder->isMousePanning(),"Middle button did not grab map");
        require(SDL_ShowCursor(SDL_QUERY)==SDL_DISABLE,"Grabbed map did not hide cursor");
        motion(end);
        require(screenborder->getCurrentCenter()==expected,"Middle drag did not follow cursor at current zoom");
        middle(end,false);
        require(!screenborder->isMousePanning(),"Middle release did not end grab");
        motion(pointer);
        require(screenborder->getCurrentCenter()==expected,"Motion after release still panned");
    }
    middle(pointer,true);
    motion(Coord(100000,100000));
    require(screenborder->getTopLeftCorner()==Coord(0,0),"Middle drag escaped map bounds");
    SDL_Event focus{}; focus.type=SDL_WINDOWEVENT; focus.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    SDL_PushEvent(&focus); game.doInput();
    require(!screenborder->isMousePanning(),"Focus loss left map grabbed");
    require(totalCommands()==commandsBeforePan && game.selectedList.count(unit->getObjectID()),
            "Middle dragging issued a command or changed selection");
    // A real, overflowing Construction Yard list must keep receiving wheel input.
    for(int item=ItemID_FirstID;item<=ItemID_LastID;++item) {
        auto& data=game.objectData.data[item][pLocalHouse->getHouseID()];
        if(data.builder!=Structure_ConstructionYard) continue;
        data.techLevel=1; data.upgradeLevel=0; data.prerequisiteStructuresSet.reset();
    }
    for(int dy=0;dy<3;++dy) for(int dx=0;dx<3;++dx)
        currentGameMap->getTile(origin.x+5+dx,origin.y+4+dy)->setType(Terrain_Rock);
    auto* yard=dynamic_cast<ConstructionYard*>(pLocalHouse->placeStructure(
        NONE_ID,Structure_ConstructionYard,origin.x+5,origin.y+4,true,true));
    require(yard,"Missing wheel sidebar fixture"); yard->updateBuildList();
    game.unselectAll(game.selectedList); game.selectedList={yard->getObjectID()}; yard->setSelected(true);
    game.pInterface->updateObjectInterface();
    auto* builder=dynamic_cast<BuilderInterface*>(game.pInterface->pObjectContainer);
    require(builder && builder->pBuilderList,"Missing sidebar production list");
    builder->update();
    auto* list=builder->pBuilderList;
    require(yard->getBuildListSize()>list->getNumButtons(list->getSize().y),"Sidebar wheel fixture is not scrollable");
    const int sidebarZoom=game.zoomStep;
    bool scrolled=false;
    for(int y=mapArea.y+180;y<getRendererHeight()-30 && !scrolled;y+=12) {
        motion(Coord(mapArea.x+mapArea.w+40,y)); wheel(-1);
        scrolled=list->currentListPos>0;
    }
    require(scrolled && game.zoomStep==sidebarZoom,"Sidebar wheel stopped scrolling or zoomed the map");
    wheel(1); require(list->currentListPos==0,"Sidebar wheel up failed to restore first row");
#ifdef MAC_TRACKPAD_PROBE
    const Coord listCenter=screenborder->getCurrentCenter();
    queueMacTrackpadScroll(drawnMouseX,drawnMouseY,0,-60,true,true);
    game.doInput();
    require(list->currentListPos>0 && game.zoomStep==sidebarZoom && screenborder->getCurrentCenter()==listCenter,
            "Trackpad scroll over sidebar failed to scroll list or moved/zoomed map");
    queueMacTrackpadScroll(drawnMouseX,drawnMouseY,0,60,true,true);
    game.doInput();
    require(list->currentListPos==0,"Trackpad sidebar scroll up did not restore first row");
#endif
    middle(Coord(mapArea.x+mapArea.w+40,mapArea.y+180),true);
    require(!screenborder->isMousePanning(),"Middle click in sidebar grabbed map");
    middle(pointer,false);
    motion(pointer);
    game.setZoomStep(originalZoom,pointer.x,pointer.y,false);
    screenborder->setNewScreenCenter(originalCenter);
    select(); game.pInterface->updateObjectInterface();
    SDL_Log("MOUSE_CONTROLS_PASS: physical wheel +/- one step, clamps, mouse anchor, high-resolution wheel debounce, sidebar scroll, all-zoom middle drag pixels, release/focus/bounds, no commands");
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
    game.onOptions();
    auto* menu=dynamic_cast<InGameMenu*>(game.pInGameMenu.get());require(menu,"Missing pause menu");
    menu->onSkipMission();menu->processChildWindowOpenCloses();
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
