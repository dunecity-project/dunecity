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

#ifndef DEFAULTUNITINTERFACE_H
#define DEFAULTUNITINTERFACE_H

#include "DefaultObjectInterface.h"

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>

#include <GUI/TextButton.h>
#include <GUI/SymbolButton.h>
#include <misc/CursorAppearance.h>
#include <GUI/ObjectInterfaces/UnitActionBar.h>
#include <GUI/HBox.h>
#include <GUI/VBox.h>

#include <units/UnitBase.h>
#include <units/MCV.h>
#include <units/Harvester.h>
#include <units/HarvesterHelpers.h>
#include <units/Devastator.h>
#include <mod/ModManager.h>

class UnitInterface : public DefaultObjectInterface {
public:
    static UnitInterface* create(int objectID) {
        UnitInterface* tmp = new UnitInterface(objectID);
        tmp->pAllocated = true;
        return tmp;
    }

protected:
    explicit UnitInterface(int objectID) : DefaultObjectInterface(objectID) {
        const int buttonGap = getRendererHeight() < 540 ? 2 : 6;
        Uint32 color = ownerAccentColor();

        mainHBox.addWidget(HSpacer::create(4));

        buttonVBox.addWidget(VSpacer::create(buttonGap));

        moveButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Move)});
        moveButton.setTooltipText(_("Move to a position (Hotkey: M)"));
        moveButton.setToggleButton(true);
        moveButton.setOnClick(std::bind(&UnitInterface::onMove, this));

        attackButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Attack)});
        attackButton.setTooltipText(settings.general.wasdCamera ? _("Attack (Shift+A)") : _("Attack a unit, structure or position (Hotkey: A)"));
        attackButton.setToggleButton(true);
        attackButton.setOnClick(std::bind(&UnitInterface::onAttack, this));

        carryallDropButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Drop)});
        carryallDropButton.setTooltipText(settings.general.wasdCamera ? _("Request Carryall drop (Shift+D)") : _("Request Carryall drop to a position (Hotkey: D)"));
        carryallDropButton.setToggleButton(true);
        carryallDropButton.setOnClick(std::bind(&UnitInterface::onCarryallDrop, this));

        captureButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Capture)});
        captureButton.setTooltipText(_("Capture a building (Hotkey: C)"));
        captureButton.setVisible((itemID == Unit_Soldier) || (itemID == Unit_Trooper));
        captureButton.setToggleButton(true);
        captureButton.setOnClick(std::bind(&UnitInterface::onCapture, this));

        returnButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Return)});
        returnButton.setTooltipText(_("Return harvester to refinery (Hotkey: H)"));
        returnButton.setVisible(isHarvesterLikeUnit(itemID));
        returnButton.setOnClick(std::bind(&UnitInterface::onReturn, this));

        deployButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Deploy)});
        deployButton.setTooltipText(_("Build a new construction yard"));
        deployButton.setVisible( (itemID == Unit_MCV) );
        deployButton.setOnClick(std::bind(&UnitInterface::onDeploy, this));

        destructButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Destruct)});
        destructButton.setTooltipText(_("Self-destruct this unit"));
        destructButton.setVisible( (itemID == Unit_Devastator) );
        destructButton.setOnClick(std::bind(&UnitInterface::onDestruct, this));

        healButton.setVisible(false);
        if(ModManager::instance().isTornieContentActive()) {

            healButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Heal)});
            healButton.setTooltipText(_("Heal an allied unit"));
            healButton.setToggleButton(true);
            healButton.setOnClick(std::bind(&UnitInterface::onHeal, this));

        }

        sendToRepairButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Repair)});
        sendToRepairButton.setTooltipText(_("Repair this unit (Hotkey: R)"));
        sendToRepairButton.setOnClick(std::bind(&UnitInterface::OnSendToRepair, this));

        movementPathsButton.setSymbol(sdl2::surface_ptr{CursorAppearance::createIcon(CursorAppearance::Action::Paths)});
        movementPathsButton.setToggleButton(true);
        movementPathsButton.setTooltipText(_("Show or hide movement paths for selected units"));
        movementPathsButton.setOnClick([]() { currentGame->toggleMovementPaths(); });
        actionBar.setButtons({&moveButton, &attackButton, &movementPathsButton,
            &carryallDropButton, &captureButton, &returnButton, &deployButton,
            &destructButton, &healButton, &sendToRepairButton});
        buttonVBox.addWidget(&actionBar);

        buttonVBox.addWidget(VSpacer::create(buttonGap));

        guardButton.setText(_("Guard"));
        guardButton.setTextColor(color);
        guardButton.setTooltipText(_("Unit will not move from location"));
        guardButton.setToggleButton(true);
        guardButton.setOnClick(std::bind(&UnitInterface::onGuard, this));
        buttonVBox.addWidget(&guardButton, 26);

        buttonVBox.addWidget(VSpacer::create(buttonGap));

        areaGuardButton.setText(_("Area Guard"));
        areaGuardButton.setTextColor(color);
        areaGuardButton.setTooltipText(_("Unit will engage any unit within guard range"));
        areaGuardButton.setToggleButton(true);
        areaGuardButton.setOnClick(std::bind(&UnitInterface::onAreaGuard, this));
        buttonVBox.addWidget(&areaGuardButton, 26);

        buttonVBox.addWidget(VSpacer::create(buttonGap));

        stopButton.setText(_("Stop"));
        stopButton.setTextColor(color);
        stopButton.setTooltipText(settings.general.wasdCamera ? _("Stop (Shift+S): unit will not move or attack") : _("Stop (S): unit will not move or attack"));
        stopButton.setToggleButton(true);
        stopButton.setOnClick(std::bind(&UnitInterface::onStop, this));
        buttonVBox.addWidget(&stopButton, 26);

        buttonVBox.addWidget(VSpacer::create(buttonGap));

        ambushButton.setText(_("Ambush"));
        ambushButton.setTextColor(color);
        ambushButton.setTooltipText(_("Unit will not move until enemy unit spotted"));
        ambushButton.setToggleButton(true);
        ambushButton.setOnClick(std::bind(&UnitInterface::onAmbush, this));
        buttonVBox.addWidget(&ambushButton, 26);

        buttonVBox.addWidget(VSpacer::create(buttonGap));

        huntButton.setText(_("Hunt"));
        huntButton.setTextColor(color);
        huntButton.setTooltipText(_("Unit will immediately start to engage an enemy unit"));
        huntButton.setToggleButton(true);
        huntButton.setOnClick(std::bind(&UnitInterface::onHunt, this));
        buttonVBox.addWidget(&huntButton, 26);

        buttonVBox.addWidget(VSpacer::create(buttonGap));

        retreatButton.setText(_("Retreat"));
        retreatButton.setTextColor(color);
        retreatButton.setTooltipText(_("Unit will retreat back to base"));
        retreatButton.setToggleButton(true);
        retreatButton.setOnClick(std::bind(&UnitInterface::onRetreat, this));
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
    void onHeal() {
        currentGame->setCursorMode(Game::CursorMode_Heal);
    }

    void onCapture() {
        currentGame->setCursorMode(Game::CursorMode_Capture);
    }

    void onCarryallDrop() {
        currentGame->setCursorMode(Game::CursorMode_CarryallDrop);
    }

    void OnSendToRepair() {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        GroundUnit* pGroundUnit = dynamic_cast<GroundUnit*>(pObject);
        if((pGroundUnit != nullptr) && (pGroundUnit->getHealth() < pGroundUnit->getMaxHealth())) {
            pGroundUnit->handleSendToRepairClick();
        }
    }

    void onReturn() {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        if(isHarvesterLikeObject(pObject)) {
            harvesterHandleReturnClick(pObject);
        }
    }

    void onDeploy() {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        MCV* pMCV = dynamic_cast<MCV*>(pObject);
        if(pMCV != nullptr) {
            pMCV->handleDeployClick();
        }
    }

    void onDestruct() {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        Devastator* pDevastator = dynamic_cast<Devastator*>(pObject);
        if(pDevastator != nullptr) {
            pDevastator->handleStartDevastateClick();
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
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        UnitBase* pUnit = dynamic_cast<UnitBase*>(pObject);

        if(pUnit != nullptr) {
            pUnit->handleSetAttackModeClick(newAttackMode);
            pUnit->playConfirmSound();

            update();
        }
    }

    /**
        This method updates the object interface.
        If the object doesn't exists anymore then update returns false.
        \return true = everything ok, false = the object container should be removed
    */
    bool update() override
    {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        if(pObject == nullptr) {
            return false;
        }

        movementPathsButton.setToggleState(settings.general.showMovementPaths);
        moveButton.setToggleState(currentGame->currentCursorMode == Game::CursorMode_Move);
        attackButton.setToggleState(currentGame->currentCursorMode == Game::CursorMode_Attack);
        healButton.setToggleState(currentGame->currentCursorMode == Game::CursorMode_Heal);
        attackButton.setVisible(pObject->canAttack());
        healButton.setVisible(pObject->canHeal());
        captureButton.setToggleState(currentGame->currentCursorMode == Game::CursorMode_Capture);
        carryallDropButton.setToggleState(currentGame->currentCursorMode == Game::CursorMode_CarryallDrop);
        carryallDropButton.setVisible(currentGame->getGameInitSettings().getGameOptions().manualCarryallDrops && pObject->getOwner()->hasCarryalls());
        sendToRepairButton.setVisible(pObject->getHealth() < pObject->getMaxHealth());
        actionBar.refresh();

        UnitBase* pUnit = dynamic_cast<UnitBase*>(pObject);
        if(pUnit != nullptr) {
            ATTACKMODE AttackMode = pUnit->getAttackMode();

            guardButton.setToggleState( AttackMode == GUARD );
            areaGuardButton.setToggleState( AttackMode == AREAGUARD );
            stopButton.setToggleState( AttackMode == STOP );
            ambushButton.setToggleState( AttackMode == AMBUSH );
            huntButton.setToggleState( AttackMode == HUNT );
            retreatButton.setToggleState( AttackMode == RETREAT );
        }

        return true;
    }

    HBox            buttonHBox;
    VBox            buttonVBox;
    UnitActionBar   actionBar;

    SymbolButton    movementPathsButton;
    SymbolButton    moveButton;
    SymbolButton    attackButton;
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
    TextButton      ambushButton;
    TextButton      huntButton;
    TextButton      retreatButton;

};

#endif //DEFAULTUNITINTERFACE_H
