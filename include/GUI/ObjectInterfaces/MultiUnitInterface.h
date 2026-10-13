/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MULTIUNITINTERFACE_H
#define MULTIUNITINTERFACE_H

#include "ObjectInterface.h"

#include <globals.h>
#include <Game.h>

#include <GUI/StaticContainer.h>
#include <GUI/HBox.h>
#include <GUI/VBox.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>

#include <GUI/TextButton.h>
#include <GUI/SymbolButton.h>
#include <misc/CursorAppearance.h>
#include <GUI/ObjectInterfaces/UnitActionBar.h>
#include <GUI/ObjectInterfaces/UnitCommandTooltips.h>

#include <units/UnitBase.h>
#include <units/MCV.h>
#include <units/Harvester.h>
#include <units/HarvesterHelpers.h>
#include <units/Devastator.h>
#include <mod/ModManager.h>

class MultiUnitInterface : public ObjectInterface {
public:
    static MultiUnitInterface* create() {
        MultiUnitInterface* tmp = new MultiUnitInterface();
        tmp->pAllocated = true;
        return tmp;
    }

protected:
    MultiUnitInterface() : ObjectInterface() {
        const int buttonGap = getRendererHeight() < 540 ? 2 : 6;
        Uint32 color = getHouseColorRGB(getHouseVisualHouse(pLocalHouse->getHouseID()), 3);

        addWidget(&topBox,Point(0,0),Point(SIDEBARWIDTH - 25,80));

        addWidget(&mainHBox,Point(0,80),Point(SIDEBARWIDTH - 25,getRendererHeight() - 80 - 148));

        topBox.addWidget(&topBoxHBox,Point(0,22),Point(SIDEBARWIDTH - 25,58));

        topBoxHBox.addWidget(Spacer::create());
        topBoxHBox.addWidget(VSpacer::create(56));
        topBoxHBox.addWidget(Spacer::create());

        mainHBox.addWidget(HSpacer::create(4));

        buttonVBox.addWidget(VSpacer::create(buttonGap));

        moveButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Move)});
        moveButton.setTooltipText(UnitCommandTooltips::move());
        moveButton.setToggleButton(true);
        moveButton.setOnClick(std::bind(&MultiUnitInterface::onMove, this));

        attackButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Attack)});
        attackButton.setTooltipText(UnitCommandTooltips::attackMove());
        attackButton.setToggleButton(true);
        attackButton.setOnClick(std::bind(&MultiUnitInterface::onAttack, this));

        patrolButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Patrol)});
        patrolButton.setTooltipText(UnitCommandTooltips::patrol());
        patrolButton.setToggleButton(true);
        patrolButton.setOnClick(std::bind(&MultiUnitInterface::onPatrol, this));

        carryallDropButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Drop)});
        carryallDropButton.setTooltipText(UnitCommandTooltips::carryallDrop());
        carryallDropButton.setToggleButton(true);
        carryallDropButton.setOnClick(std::bind(&MultiUnitInterface::onCarryallDrop, this));

        captureButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Capture)});
        captureButton.setTooltipText(UnitCommandTooltips::capture());
        captureButton.setToggleButton(true);
        captureButton.setOnClick(std::bind(&MultiUnitInterface::onCapture, this));

        returnButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Return)});
        returnButton.setTooltipText(UnitCommandTooltips::returnHarvester());
        returnButton.setOnClick(std::bind(&MultiUnitInterface::onReturn, this));

        deployButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Deploy)});
        deployButton.setTooltipText(_("Build a new construction yard"));
        deployButton.setOnClick(std::bind(&MultiUnitInterface::onDeploy, this));

        destructButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Destruct)});
        destructButton.setTooltipText(_("Self-destruct this unit"));
        destructButton.setOnClick(std::bind(&MultiUnitInterface::onDestruct, this));

        healButton.setVisible(false);
        if(ModManager::instance().isTornieContentActive()) {

            healButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Heal)});
            healButton.setTooltipText(_("Heal an allied unit"));
            healButton.setToggleButton(true);
            healButton.setOnClick(std::bind(&MultiUnitInterface::onHeal, this));

        }

        sendToRepairButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Repair)});
        sendToRepairButton.setTooltipText(UnitCommandTooltips::repairUnit());
        sendToRepairButton.setOnClick(std::bind(&MultiUnitInterface::OnSendToRepair, this));

        movementPathsButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Paths)});
        movementPathsButton.setToggleButton(true);
        movementPathsButton.setTooltipText(_("Show or hide movement paths for selected units"));
        movementPathsButton.setOnClick([]() { currentGame->toggleMovementPaths(); });
        actionBar.setButtons({&moveButton, &attackButton, &patrolButton, &movementPathsButton,
            &carryallDropButton, &captureButton, &returnButton, &deployButton,
            &destructButton, &healButton, &sendToRepairButton});
        buttonVBox.addWidget(&actionBar);

        buttonVBox.addWidget(VSpacer::create(buttonGap));

        guardButton.setText(_("Guard"));
        guardButton.setTextColor(color);
        guardButton.setTooltipText(_("Unit will not move from location"));
        guardButton.setToggleButton(true);
        guardButton.setOnClick(std::bind(&MultiUnitInterface::onGuard, this));
        buttonVBox.addWidget(&guardButton, 26);

        buttonVBox.addWidget(VSpacer::create(buttonGap));

        areaGuardButton.setText(_("Area Guard"));
        areaGuardButton.setTextColor(color);
        areaGuardButton.setTooltipText(_("Unit will engage any unit within guard range"));
        areaGuardButton.setToggleButton(true);
        areaGuardButton.setOnClick(std::bind(&MultiUnitInterface::onAreaGuard, this));
        buttonVBox.addWidget(&areaGuardButton, 26);

        buttonVBox.addWidget(VSpacer::create(buttonGap));

        stopButton.setText(_("Stop"));
        stopButton.setTextColor(color);
        stopButton.setTooltipText(UnitCommandTooltips::stop());
        stopButton.setToggleButton(true);
        stopButton.setOnClick(std::bind(&MultiUnitInterface::onStop, this));
        buttonVBox.addWidget(&stopButton, 26);

        buttonVBox.addWidget(VSpacer::create(buttonGap));

        holdButton.setText(_("Hold"));
        holdButton.setTextColor(color);
        holdButton.setTooltipText(UnitCommandTooltips::hold());
        holdButton.setToggleButton(true);
        holdButton.setOnClick(std::bind(&MultiUnitInterface::onHold, this));
        buttonVBox.addWidget(&holdButton, 26);

        buttonVBox.addWidget(VSpacer::create(buttonGap));

        ambushButton.setText(_("Ambush"));
        ambushButton.setTextColor(color);
        ambushButton.setTooltipText(_("Unit will not move until enemy unit spotted"));
        ambushButton.setToggleButton(true);
        ambushButton.setOnClick(std::bind(&MultiUnitInterface::onAmbush, this));
        buttonVBox.addWidget(&ambushButton, 26);

        buttonVBox.addWidget(VSpacer::create(buttonGap));

        huntButton.setText(_("Hunt"));
        huntButton.setTextColor(color);
        huntButton.setTooltipText(_("Unit will immediately start to engage an enemy unit"));
        huntButton.setToggleButton(true);
        huntButton.setOnClick(std::bind(&MultiUnitInterface::onHunt, this));
        buttonVBox.addWidget(&huntButton, 26);

        buttonVBox.addWidget(VSpacer::create(buttonGap));

        retreatButton.setText(_("Retreat"));
        retreatButton.setTextColor(color);
        retreatButton.setTooltipText(_("Unit will retreat back to base"));
        retreatButton.setToggleButton(true);
        retreatButton.setOnClick(std::bind(&MultiUnitInterface::onRetreat, this));
        buttonVBox.addWidget(&retreatButton,26);

        buttonVBox.addWidget(VSpacer::create(buttonGap));
        buttonVBox.addWidget(Spacer::create());
        buttonVBox.addWidget(VSpacer::create(buttonGap));

        mainHBox.addWidget(&buttonVBox);
        mainHBox.addWidget(HSpacer::create(5));

        update();
    }

    void onMove() {
        currentGame->setCursorMode(Game::CursorMode_Move);
    }

    void onAttack() {
        currentGame->setCursorMode(Game::CursorMode_Attack);
    }

    void onPatrol() {
        currentGame->setCursorMode(Game::CursorMode_Patrol);
    }
    void onHeal() {
        currentGame->setCursorMode(Game::CursorMode_Heal);
    }

    void onCapture() {
        currentGame->setCursorMode(Game::CursorMode_Capture);
    }

    void onCarryallDrop() {
        currentGame->setCursorMode(Game::CursorMode_CarryallDrop);
    }

    void onReturn() {
        for(const Uint32 selectedUnitID : currentGame->getSelectedList()) {
            ObjectBase* pObject = currentGame->getObjectManager().getObject(selectedUnitID);
            if(isHarvesterLikeObject(pObject)) {
                harvesterHandleReturnClick(pObject);
            }
        }
    }

    void OnSendToRepair() {
        for(const Uint32 selectedUnitID : currentGame->getSelectedList()) {
            ObjectBase* pObject = currentGame->getObjectManager().getObject(selectedUnitID);
            GroundUnit* pGroundUnit = dynamic_cast<GroundUnit*>(pObject);
            if((pGroundUnit != nullptr) && (pGroundUnit->getHealth() < pGroundUnit->getMaxHealth())) {
                pGroundUnit->handleSendToRepairClick();
            }
        }
    }

    void onDeploy() {
        for(const Uint32 selectedUnitID : currentGame->getSelectedList()) {
            ObjectBase* pObject = currentGame->getObjectManager().getObject(selectedUnitID);
            MCV* pMCV = dynamic_cast<MCV*>(pObject);
            if(pMCV != nullptr) {
                pMCV->handleDeployClick();
            }
        }
    }

    void onDestruct() {
        for(const Uint32 selectedUnitID : currentGame->getSelectedList()) {
            ObjectBase* pObject = currentGame->getObjectManager().getObject(selectedUnitID);
            Devastator* pDevastator = dynamic_cast<Devastator*>(pObject);
            if(pDevastator != nullptr) {
                pDevastator->handleStartDevastateClick();
            }
        }
    }

    void onGuard() {
        setAttackMode(GUARD);
    }

    void onAreaGuard() {
        setAttackMode(AREAGUARD);
    }

    void onStop() {
        setAttackMode(STOP);
    }

    void onHold() {
        setAttackMode(HOLD);
    }

    void onAmbush() {
        setAttackMode(AMBUSH);
    }

    void onHunt() {
        setAttackMode(HUNT);
    }

    void onRetreat(){
        setAttackMode(RETREAT);
    }

    void setAttackMode(ATTACKMODE newAttackMode) {

        UnitBase* pLastUnit = nullptr;
        for(const Uint32 selectedUnitID : currentGame->getSelectedList()) {
            ObjectBase* pObject = currentGame->getObjectManager().getObject(selectedUnitID);
            UnitBase* pUnit = dynamic_cast<UnitBase*>(pObject);
            if(pUnit != nullptr) {
                pLastUnit = pUnit;
                pUnit->handleSetAttackModeClick(newAttackMode);
            }
        }

        if(pLastUnit != nullptr) {
            pLastUnit->playConfirmSound();
        }

        update();
    }

    /**
        This method updates the object interface.
        If the object doesn't exists anymore then update returns false.
        \return true = everything ok, false = the object container should be removed
    */
    bool update() override
    {
        if(currentGame->getSelectedList().empty() == true) {
            return false;
        }

        movementPathsButton.setToggleState(settings.general.showMovementPaths);
        moveButton.setToggleState(currentGame->currentCursorMode == Game::CursorMode_Move);
        attackButton.setToggleState(currentGame->currentCursorMode == Game::CursorMode_Attack);
        patrolButton.setToggleState(currentGame->currentCursorMode == Game::CursorMode_Patrol);
        healButton.setToggleState(currentGame->currentCursorMode == Game::CursorMode_Heal);
        captureButton.setToggleState(currentGame->currentCursorMode == Game::CursorMode_Capture);

        bool bGuard = true;
        bool bAreaGuard = true;
        bool bStop = true;
        bool bHold = true;
        bool bAmbush = true;
        bool bHunt = true;
        bool bRetreat = true;

        bool bShowAttack = false;
        bool bShowHeal = false;
        bool bShowCapture = false;
        bool bShowReturn = false;
        bool bShowDeploy = false;
        bool bShowDevastate = false;
        bool bShowRepair = false;
        bool bShowCarryallDrop = false;

        for(const Uint32 selectedUnitID : currentGame->getSelectedList()) {
            ObjectBase* pObject = currentGame->getObjectManager().getObject(selectedUnitID);
            UnitBase* pUnit = dynamic_cast<UnitBase*>(pObject);
            if(pUnit != nullptr) {
                ATTACKMODE attackMode = pUnit->getAttackMode();
                bGuard = bGuard && (attackMode == GUARD);
                bAreaGuard = bAreaGuard && (attackMode == AREAGUARD);
                bStop = bStop && (attackMode == STOP);
                bHold = bHold && (attackMode == HOLD);
                bAmbush = bAmbush && (attackMode == AMBUSH);
                bHunt = bHunt && (attackMode == HUNT);
                bRetreat = bRetreat && (attackMode == RETREAT);

                if(pUnit->canHeal()) {
                    bShowHeal = true;
                }

                if(pUnit->canAttack()) {
                    bShowAttack = true;
                }

                if(pUnit->getOwner()->hasCarryalls()){
                    bShowCarryallDrop = true;
                }

                if(pUnit->getHealth() < pUnit->getMaxHealth()){
                    bShowRepair = true;
                }

                switch(pUnit->getItemID()) {
                    case Unit_Soldier:
                    case Unit_Trooper: {
                        bShowCapture = true;
                    } break;

                    case Unit_MCV: {
                        bShowDeploy = true;
                    } break;

                    case Unit_Devastator: {
                        bShowDevastate = true;
                    } break;

                    default: {
                    } break;
                }

                if(isHarvesterLikeUnit(pUnit->getItemID())) {
                    bShowReturn = true;
                }

            }
        }

        attackButton.setVisible(bShowAttack);
        healButton.setVisible(bShowHeal);
        captureButton.setVisible(bShowCapture);
        returnButton.setVisible(bShowReturn);
        deployButton.setVisible(bShowDeploy);
        destructButton.setVisible(bShowDevastate);
        sendToRepairButton.setVisible(bShowRepair);
        carryallDropButton.setVisible(bShowCarryallDrop && currentGame->getGameInitSettings().getGameOptions().manualCarryallDrops);
        actionBar.refresh();

        guardButton.setToggleState( bGuard );
        areaGuardButton.setToggleState( bAreaGuard );
        stopButton.setToggleState( bStop );
        holdButton.setToggleState( bHold );
        ambushButton.setToggleState( bAmbush );
        huntButton.setToggleState( bHunt );
        retreatButton.setToggleState( bRetreat );

        return true;
    }

    StaticContainer topBox;
    HBox            topBoxHBox;
    HBox            mainHBox;

    HBox            buttonHBox;
    VBox            buttonVBox;
    UnitActionBar   actionBar;

    SymbolButton    movementPathsButton;
    SymbolButton    moveButton;
    SymbolButton    attackButton;
    SymbolButton    patrolButton;
    SymbolButton    captureButton;
    SymbolButton    returnButton;
    SymbolButton    deployButton;
    SymbolButton    destructButton;
    SymbolButton    healButton;
    SymbolButton    sendToRepairButton;
    SymbolButton    carryallDropButton;

    TextButton      guardButton;
    TextButton      areaGuardButton;
    TextButton      stopButton;
    TextButton      holdButton;
    TextButton      ambushButton;
    TextButton      huntButton;
    TextButton      retreatButton;
};

#endif //MULTIUNITINTERFACE_H
