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

#include <GUI/dune/GameOptionsWindow.h>

#include <globals.h>

#include <FileClasses/TextManager.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/FontManager.h>
#include <misc/MenuPalette.h>
#include <misc/string_util.h>

void DamageRulesCheckbox::setExplanation(const std::string& title, const std::string& text) {
    explanationTitle = title;
    tooltipText = text;
    tooltipTexture.reset();
    explanationWidth = 0;
}

void DamageRulesCheckbox::drawOverlay(Point) {
    explanationBounds = {};
    if(!isVisible() || !isEnabled() || !bHover || !pFontManager
       || SDL_GetTicks() - tooltipLastMouseMotion <= 750) return;

    const auto screen = getRendererSize();
    const int width = std::min(460, screen.w - 16);
    if(width <= 32) return;
    if(!tooltipTexture || explanationWidth != width) {
        const auto lines = greedyWordWrap(tooltipText, width - 24,
            [](const std::string& text) { return pFontManager->getTextWidth(text, 14); });
        const int lineHeight = pFontManager->getTextHeight(14) + 3;
        const int titleHeight = pFontManager->getTextHeight(16);
        const int height = 30 + titleHeight + static_cast<int>(lines.size()) * lineHeight;
        auto surface = GUIStyle::getInstance().createBackground(width, height);
        if(!surface) return;
        auto title = pFontManager->createSurfaceWithText(explanationTitle, MenuTheme::accent, 16);
        if(title) {
            auto dest = calcDrawingRect(title.get(), 12, 10);
            SDL_BlitSurface(title.get(), nullptr, surface.get(), &dest);
        }
        int y = 20 + titleHeight;
        for(const auto& line : lines) {
            auto text = pFontManager->createSurfaceWithText(line, MenuTheme::text, 14);
            if(text) {
                auto dest = calcDrawingRect(text.get(), 12, y);
                SDL_BlitSurface(text.get(), nullptr, surface.get(), &dest);
            }
            y += lineHeight;
        }
        tooltipTexture = convertSurfaceToTexture(std::move(surface));
        explanationWidth = width;
    }
    if(!tooltipTexture) return;
    auto dest = calcDrawingRect(tooltipTexture.get(), drawnMouseX, drawnMouseY - 8,
                                HAlign::Left, VAlign::Bottom);
    dest.x = std::clamp(dest.x, 0, std::max(0, screen.w - dest.w));
    dest.y = std::clamp(dest.y, 0, std::max(0, screen.h - dest.h));
    explanationBounds = dest;
    SDL_RenderCopy(renderer, tooltipTexture.get(), nullptr, &dest);
}


GameOptionsWindow::GameOptionsWindow(SettingsClass::GameOptionsClass& initialGameOptions)
 : Window(50,50,626,370), gameOptions(initialGameOptions) {

    setWindowWidget(&vbox);
    vbox.addWidget(VSpacer::create(6));

    captionlabel.setText(_("Game Rules"));
    captionlabel.setAlignment(Alignment_HCenter);
    captionlabel.setTextFontSize(22);
    vbox.addWidget(&captionlabel,32);
    vbox.addWidget(VSpacer::create(3));
    vbox.addWidget(&hbox);
    vbox.addWidget(VSpacer::create(6));
    hbox.addWidget(Spacer::create());
    hbox.addWidget(&vboxLeft);
    hbox.addWidget(HSpacer::create(20));
    hbox.addWidget(&vboxRight);
    hbox.addWidget(Spacer::create());
    vboxLeft.addWidget(Spacer::create());

    startWithExploredMapCheckbox.setText(_("Start with Explored Map"));
    startWithExploredMapCheckbox.setTooltipText(_("If checked the complete map is unhidden at the beginning of the game."));
    startWithExploredMapCheckbox.setChecked(gameOptions.startWithExploredMap);
    vboxLeft.addWidget(&startWithExploredMapCheckbox);
    vboxLeft.addWidget(VSpacer::create(6));

    sandwormsRespawnCheckbox.setText(_("Killed Sandworms Respawn"));
    sandwormsRespawnCheckbox.setTooltipText(_("If checked killed sandworms respawn after some time."));
    sandwormsRespawnCheckbox.setChecked(gameOptions.sandwormsRespawn);
    vboxLeft.addWidget(&sandwormsRespawnCheckbox);
    vboxLeft.addWidget(VSpacer::create(6));

    killedSandwormsDropSpiceCheckbox.setText(_("Killed Sandworms Drop Spice"));
    killedSandwormsDropSpiceCheckbox.setTooltipText(_("If checked killed sandworms drop some spice."));
    killedSandwormsDropSpiceCheckbox.setChecked(gameOptions.killedSandwormsDropSpice);
    vboxLeft.addWidget(&killedSandwormsDropSpiceCheckbox);
    vboxLeft.addWidget(VSpacer::create(6));

    manualCarryallDropsCheckbox.setText(_("Manual Carryall Drops"));
    manualCarryallDropsCheckbox.setTooltipText(_("If checked player can request carryall to transport units."));
    manualCarryallDropsCheckbox.setChecked(gameOptions.manualCarryallDrops);
    vboxLeft.addWidget(&manualCarryallDropsCheckbox);
    vboxLeft.addWidget(VSpacer::create(6));

    immortalHumanPlayerCheckbox.setText(_("Immortal Human Player"));
    immortalHumanPlayerCheckbox.setTooltipText(_("God mode / cheat: Human-controlled units and structures are invulnerable to damage. Single-player only."));
    immortalHumanPlayerCheckbox.setChecked(gameOptions.immortalHumanPlayer);
    vboxLeft.addWidget(&immortalHumanPlayerCheckbox);
    vboxLeft.addWidget(VSpacer::create(6));

    maxUnitsOverrideCheckbox.setText(_("Override max. number of units"));
    maxUnitsOverrideCheckbox.setTooltipText(_("If checked the maximum number of units per house can be overridden; otherwise it is map dependent. 0 = unlimited."));
    maxUnitsOverrideCheckbox.setChecked(gameOptions.maximumNumberOfUnitsOverride >= 0);
    maxUnitsOverrideCheckbox.setOnClick([this]() { maxUnitsOverrideTextBox.setVisible(maxUnitsOverrideCheckbox.isChecked()); });
    maxUnitsOverrideHBox.addWidget(&maxUnitsOverrideCheckbox);
    maxUnitsOverrideTextBox.setMinMax(0,999);
    maxUnitsOverrideTextBox.setValue( (gameOptions.maximumNumberOfUnitsOverride < 0) ? 0 : gameOptions.maximumNumberOfUnitsOverride );
    maxUnitsOverrideTextBox.setVisible(gameOptions.maximumNumberOfUnitsOverride >= 0);
    maxUnitsOverrideHBox.addWidget(&maxUnitsOverrideTextBox);
    vboxLeft.addWidget(&maxUnitsOverrideHBox, 24);
    vboxLeft.addWidget(VSpacer::create(6));

    maxHarvestersOverrideCheckbox.setText(_("Override max. number of harvesters"));
    maxHarvestersOverrideCheckbox.setTooltipText(_("Limit harvesters per house. Unchecked or 0 means unlimited; each AI chooses its own economic target."));
    maxHarvestersOverrideCheckbox.setChecked(gameOptions.maximumNumberOfHarvestersOverride >= 0);
    maxHarvestersOverrideCheckbox.setOnClick([this]() { maxHarvestersOverrideTextBox.setVisible(maxHarvestersOverrideCheckbox.isChecked()); });
    maxHarvestersOverrideHBox.addWidget(&maxHarvestersOverrideCheckbox);
    maxHarvestersOverrideTextBox.setMinMax(0,999);
    maxHarvestersOverrideTextBox.setValue( (gameOptions.maximumNumberOfHarvestersOverride < 0) ? 0 : gameOptions.maximumNumberOfHarvestersOverride );
    maxHarvestersOverrideTextBox.setVisible(gameOptions.maximumNumberOfHarvestersOverride >= 0);
    maxHarvestersOverrideHBox.addWidget(&maxHarvestersOverrideTextBox);
    vboxLeft.addWidget(&maxHarvestersOverrideHBox, 24);
    vboxLeft.addWidget(VSpacer::create(6));

    maxConstructionYardsOverrideCheckbox.setText(_("Limit construction yards per house"));
    maxConstructionYardsOverrideCheckbox.setTooltipText(_("Unchecked or 0 means unlimited. At the limit, MCVs cannot deploy. Existing yards stay."));
    maxConstructionYardsOverrideCheckbox.setChecked(gameOptions.maximumNumberOfConstructionYardsOverride >= 0);
    maxConstructionYardsOverrideCheckbox.setOnClick([this]() { maxConstructionYardsOverrideTextBox.setVisible(maxConstructionYardsOverrideCheckbox.isChecked()); });
    maxConstructionYardsOverrideHBox.addWidget(&maxConstructionYardsOverrideCheckbox);
    maxConstructionYardsOverrideTextBox.setMinMax(0,999);
    maxConstructionYardsOverrideTextBox.setValue( (gameOptions.maximumNumberOfConstructionYardsOverride < 0) ? 0 : gameOptions.maximumNumberOfConstructionYardsOverride );
    maxConstructionYardsOverrideTextBox.setVisible(gameOptions.maximumNumberOfConstructionYardsOverride >= 0);
    maxConstructionYardsOverrideHBox.addWidget(&maxConstructionYardsOverrideTextBox);
    vboxLeft.addWidget(&maxConstructionYardsOverrideHBox, 24);
    vboxLeft.addWidget(VSpacer::create(6));

    vboxLeft.addWidget(VSpacer::create(14));


    concreteRequiredCheckbox.setText(_("Concrete is required"));
    /* xgettext:no-c-format */
    concreteRequiredCheckbox.setTooltipText(_("Enable concrete. Missing foundations reduce initial health and cause decay. Unchecked: concrete is disabled and these penalties do not apply."));
    concreteRequiredCheckbox.setChecked(gameOptions.concreteRequired);
    vboxRight.addWidget(&concreteRequiredCheckbox);
    vboxRight.addWidget(VSpacer::create(6));

    fogOfWarCheckbox.setText(_("Fog of War"));
    fogOfWarCheckbox.setTooltipText(_("If checked explored terrain will become foggy when no unit or structure is next to it."));
    fogOfWarCheckbox.setChecked(gameOptions.fogOfWar);
    vboxRight.addWidget(&fogOfWarCheckbox);
    vboxRight.addWidget(VSpacer::create(6));

    instantBuildCheckbox.setText(_("Instant Build"));
    instantBuildCheckbox.setTooltipText(_("If checked the building of structures and units does not take any time."));
    instantBuildCheckbox.setChecked(gameOptions.instantBuild);
    vboxRight.addWidget(&instantBuildCheckbox);
    vboxRight.addWidget(VSpacer::create(6));

    rocketTurretsNeedPowerCheckbox.setText(_("Rocket-Turrets Need Power"));
    rocketTurretsNeedPowerCheckbox.setTooltipText(_("If checked rocket turrets are dysfunctional on power shortage."));
    rocketTurretsNeedPowerCheckbox.setChecked(gameOptions.rocketTurretsNeedPower);
    vboxRight.addWidget(&rocketTurretsNeedPowerCheckbox);
    vboxRight.addWidget(VSpacer::create(6));

    onlyOnePalaceCheckbox.setText(_("Only One Palace per House"));
    onlyOnePalaceCheckbox.setTooltipText(_("If checked only one palace can be build per house."));
    onlyOnePalaceCheckbox.setChecked(gameOptions.onlyOnePalace);
    vboxRight.addWidget(&onlyOnePalaceCheckbox);
    vboxRight.addWidget(VSpacer::create(6));

    originalUnitDamageCheckbox.setText(_("Original Dune II damage"));
    originalUnitDamageCheckbox.setChecked(gameOptions.originalUnitDamage);
    originalUnitDamageCheckbox.setExplanation(_("Original Dune II damage and splash"),
        _("Use original Dune II damage against ground units, as reproduced by Dune Dynasty classic. Tank hits rise from 12 to 25; Launcher rockets from 37 to 75. Splash halves every quarter tile and stops at one tile. Unchecked keeps DuneCity balance. Building damage, aircraft hits and special weapons keep their existing rules."));
    vboxRight.addWidget(&originalUnitDamageCheckbox);
    vboxRight.addWidget(VSpacer::create(6));

    gameSpeedMinus.setTextures(pGFXManager->getUIGraphic(UI_Minus), pGFXManager->getUIGraphic(UI_Minus_Pressed));
    gameSpeedMinus.setOnClick(std::bind(&GameOptionsWindow::onGameSpeedMinus, this));
    gameSpeedHBox.addWidget(HSpacer::create(4));
    gameSpeedHBox.addWidget(&gameSpeedMinus);
    gameSpeedHBox.addWidget(HSpacer::create(2));

    gameSpeedBar.setText(_("Game speed"));
    gameSpeedHBox.addWidget(&gameSpeedBar);
    currentGameSpeed = gameOptions.gameSpeed;
    updateGameSpeedBar();
    gameSpeedPlus.setTextures(pGFXManager->getUIGraphic(UI_Plus), pGFXManager->getUIGraphic(UI_Plus_Pressed));
    gameSpeedPlus.setOnClick(std::bind(&GameOptionsWindow::onGameSpeedPlus, this));
    gameSpeedHBox.addWidget(HSpacer::create(2));
    gameSpeedHBox.addWidget(&gameSpeedPlus);
    gameSpeedHBox.addWidget(HSpacer::create(4));
    vboxRight.addWidget(&gameSpeedHBox, 24);
    vboxRight.addWidget(VSpacer::create(6));

    rememberDefaults.setText(_("Remember for new games"));
    vboxRight.addWidget(&rememberDefaults, 22);

    vboxRight.addWidget(VSpacer::create(6));

    okbutton.setText(_("OK"));
    okbutton.setOnClick(std::bind(&GameOptionsWindow::onOK, this));
    vboxRight.addWidget(&okbutton, 30);
    vboxRight.addWidget(VSpacer::create(6));


    int xpos = std::max(0,(getRendererWidth() - getSize().x)/2);
    int ypos = std::max(0,(getRendererHeight() - getSize().y)/2);

    setCurrentPosition(xpos,ypos,getSize().x,getSize().y);
}

GameOptionsWindow::~GameOptionsWindow() {
    ;
}

void GameOptionsWindow::onOK() {
    gameOptions.gameSpeed = currentGameSpeed;
    gameOptions.concreteRequired = concreteRequiredCheckbox.isChecked();
    gameOptions.fogOfWar = fogOfWarCheckbox.isChecked();
    gameOptions.startWithExploredMap = startWithExploredMapCheckbox.isChecked();
    gameOptions.instantBuild = instantBuildCheckbox.isChecked();
    gameOptions.onlyOnePalace = onlyOnePalaceCheckbox.isChecked();
    gameOptions.originalUnitDamage = originalUnitDamageCheckbox.isChecked();
    gameOptions.rocketTurretsNeedPower = rocketTurretsNeedPowerCheckbox.isChecked();
    gameOptions.sandwormsRespawn = sandwormsRespawnCheckbox.isChecked();
    gameOptions.killedSandwormsDropSpice = killedSandwormsDropSpiceCheckbox.isChecked();
    gameOptions.manualCarryallDrops = manualCarryallDropsCheckbox.isChecked();
    gameOptions.immortalHumanPlayer = immortalHumanPlayerCheckbox.isChecked();
    gameOptions.maximumNumberOfUnitsOverride = maxUnitsOverrideCheckbox.isChecked() ? maxUnitsOverrideTextBox.getValue() : -1;
    gameOptions.maximumNumberOfHarvestersOverride = maxHarvestersOverrideCheckbox.isChecked() ? maxHarvestersOverrideTextBox.getValue() : -1;
    gameOptions.maximumNumberOfConstructionYardsOverride = maxConstructionYardsOverrideCheckbox.isChecked() ? maxConstructionYardsOverrideTextBox.getValue() : -1;

    if(rememberDefaults.isChecked()) saveGameOptionsAsDefaults(gameOptions);
    Window* pParentWindow = dynamic_cast<Window*>(getParent());
    if(pParentWindow != nullptr) {
        pParentWindow->closeChildWindow();
    }
}

void GameOptionsWindow::onGameSpeedMinus() {
    if(currentGameSpeed < GAMESPEED_MAX) {
        currentGameSpeed++;
        updateGameSpeedBar();
    }
}

void GameOptionsWindow::onGameSpeedPlus() {
    if(currentGameSpeed > GAMESPEED_MIN) {
        currentGameSpeed--;
        updateGameSpeedBar();
    }
}

void GameOptionsWindow::updateGameSpeedBar() {
    gameSpeedBar.setProgress( 100.0 - ((currentGameSpeed-GAMESPEED_MIN)*100.0)/(GAMESPEED_MAX - GAMESPEED_MIN) );
}
