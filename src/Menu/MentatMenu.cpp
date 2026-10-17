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

#include <Menu/MentatMenu.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>

#include <mmath.h>
#include <mod/ModManager.h>
#include <misc/MenuPalette.h>

#include <algorithm>
#include <regex>

MentatMenu::MentatMenu(int newHouse)
 : MenuBase(), currentMentatTextIndex(-1), nextMentatTextSwitch(0)
{
    nextSpecialAnimation = SDL_GetTicks() + getRandomInt(8000, 20000);

    Animation* anim;

    disableQuiting(true);
    house = newHouse;
    const int mentatAssetHouse = newHouse >= HOUSE_WILDSPADE && newHouse <= HOUSE_THARPIQUE
        ? HOUSE_NEUTRAL + (newHouse - HOUSE_WILDSPADE)
        : newHouse;
    mentatPresentationHouse = mentatAssetHouse == HOUSE_INVALID
        ? HOUSE_INVALID
        : ModManager::instance().getEffectiveMentatIdentity(mentatAssetHouse);

    // set up window
    SDL_Texture *pBackground;
    if(house == HOUSE_INVALID) {
        pBackground = pGFXManager->getUIGraphic(UI_MentatBackgroundBene);
    } else {
        pBackground = pGFXManager->getUIGraphic(UI_MentatBackground,mentatAssetHouse);
    }

    setBackground(pBackground);

    setCurrentPosition(calcAlignedDrawingRect(pBackground, HAlign::Center, VAlign::Center));

    setWindowWidget(&windowWidget);

    if(house == HOUSE_INVALID) {
        anim = pGFXManager->getAnimation(Anim_BeneEyes);
        eyesAnim.setAnimation(anim);
        windowWidget.addWidget(&eyesAnim, Point(128,160), eyesAnim.getMinimumSize());

        anim = pGFXManager->getAnimation(Anim_BeneMouth);
        mouthAnim.setAnimation(anim);
        windowWidget.addWidget(&mouthAnim, Point(112,192), mouthAnim.getMinimumSize());
    } else {
        ModManager& modManager = ModManager::instance();
        const ModMentatInfo& info = modManager.getActiveMentatInfo(mentatAssetHouse);
        const int identity = modManager.getEffectiveMentatIdentity(mentatAssetHouse);

        Point eyesPosition;
        Point mouthPosition;
        switch(identity) {
            case HOUSE_HARKONNEN:
            case HOUSE_SARDAUKAR:
                eyesPosition = Point(64,176);
                mouthPosition = Point(64,208);
                break;
            case HOUSE_ORDOS:
            case HOUSE_MERCENARY:
                eyesPosition = Point(32,160);
                mouthPosition = Point(32,192);
                break;
            default:
                eyesPosition = Point(80,160);
                mouthPosition = Point(80,192);
                break;
        }
        if(info.enabled) {
            if(info.eyesX >= 0) eyesPosition.x = info.eyesX;
            if(info.eyesY >= 0) eyesPosition.y = info.eyesY;
            if(info.mouthX >= 0) mouthPosition.x = info.mouthX;
            if(info.mouthY >= 0) mouthPosition.y = info.mouthY;
        }

        eyesAnim.setAnimation(pGFXManager->getMentatEyesAnimation(mentatAssetHouse));
        windowWidget.addWidget(&eyesAnim, eyesPosition, eyesAnim.getMinimumSize());
        mouthAnim.setAnimation(pGFXManager->getMentatMouthAnimation(mentatAssetHouse));
        windowWidget.addWidget(&mouthAnim, mouthPosition, mouthAnim.getMinimumSize());

        const bool useBaseExtras = !info.enabled || info.useBaseExtras;
        if(useBaseExtras) {
            switch(identity) {
                case HOUSE_HARKONNEN:
                    shoulderAnim.setAnimation(pGFXManager->getAnimation(Anim_HarkonnenShoulder));
                    break;
                case HOUSE_ATREIDES:
                    specialAnim.setAnimation(pGFXManager->getAnimation(Anim_AtreidesBook));
                    windowWidget.addWidget(&specialAnim, Point(145,305), specialAnim.getMinimumSize());
                    shoulderAnim.setAnimation(pGFXManager->getAnimation(Anim_AtreidesShoulder));
                    break;
                case HOUSE_FREMEN:
                    specialAnim.setAnimation(pGFXManager->getAnimation(Anim_FremenBook));
                    windowWidget.addWidget(&specialAnim, Point(145,305), specialAnim.getMinimumSize());
                    shoulderAnim.setAnimation(pGFXManager->getAnimation(Anim_FremenShoulder));
                    break;
                case HOUSE_SARDAUKAR:
                    shoulderAnim.setAnimation(pGFXManager->getAnimation(Anim_SardaukarShoulder));
                    break;
                case HOUSE_ORDOS:
                    specialAnim.setAnimation(pGFXManager->getAnimation(Anim_OrdosRing));
                    specialAnim.getAnimation()->setCurrentFrameNumber(specialAnim.getAnimation()->getNumberOfFrames()-1);
                    windowWidget.addWidget(&specialAnim, Point(178,289), specialAnim.getMinimumSize());
                    shoulderAnim.setAnimation(pGFXManager->getAnimation(Anim_OrdosShoulder));
                    break;
                case HOUSE_MERCENARY:
                    specialAnim.setAnimation(pGFXManager->getAnimation(Anim_MercenaryRing));
                    specialAnim.getAnimation()->setCurrentFrameNumber(specialAnim.getAnimation()->getNumberOfFrames()-1);
                    windowWidget.addWidget(&specialAnim, Point(178,289), specialAnim.getMinimumSize());
                    shoulderAnim.setAnimation(pGFXManager->getAnimation(Anim_MercenaryShoulder));
                    break;
                default:
                    break;
            }
        }
    }
    // Narration is drawn over the original Mentat artwork, not a gold panel.
    textLabel.setTextColor(COLOR_WHITE,
        MenuTheme::isDesertUISkin() ? COLOR_BLACK : COLOR_TRANSPARENT);
    textLabel.setAlignment((Alignment_Enum) (Alignment_Left | Alignment_Top));
    textLabel.setVisible(false);
}

MentatMenu::~MentatMenu() = default;

void MentatMenu::setText(const std::string& text) {
    std::regex rgx("[^\\.\\!\\?]*[\\.\\!\\?]\\s?");
    mentatTexts = std::vector<std::string>(std::sregex_token_iterator(text.begin(), text.end(), rgx), std::sregex_token_iterator());
    if(mentatTexts.empty()) {
        mentatTexts.push_back(text);
    }

    mouthAnim.getAnimation()->setNumLoops(mentatTexts[0].empty() ? 0 : mentatTexts[0].length()/25 + 1);
    textLabel.setText(mentatTexts[0]);
    textLabel.setVisible(true);
    textLabel.resize(620,240);

    currentMentatTextIndex = 0;
    nextMentatTextSwitch = SDL_GetTicks() + mentatTexts[0].length() * 75 + 1000;
}

void MentatMenu::update() {
    // speedup blink of the eye
    eyesAnim.getAnimation()->setFrameRate((eyesAnim.getAnimation()->getCurrentFrameNumber() == MentatEyesClosed) ? 4.0 : 0.5);

    if(SDL_GetTicks() > nextMentatTextSwitch) {
        currentMentatTextIndex++;

        std::string text;
        if(currentMentatTextIndex >= (int) mentatTexts.size()) {
            onMentatTextFinished();
            nextMentatTextSwitch = 0xFFFFFFFF;
            text = "";
        } else {
            text = mentatTexts[currentMentatTextIndex];
            if(text.empty()) {
                onMentatTextFinished();
                nextMentatTextSwitch = 0xFFFFFFFF;
                currentMentatTextIndex = mentatTexts.size();
            } else {
                nextMentatTextSwitch = SDL_GetTicks() + text.length() * 75 + 1000;
            }
        }

        mouthAnim.getAnimation()->setNumLoops(text.empty() ? 0 : text.length()/25 + 1);

        textLabel.setText(text);
        textLabel.setVisible(true);
        textLabel.resize(620,240);
    }

    if(specialAnim.getAnimation() != nullptr && specialAnim.getAnimation()->isFinished()) {
        if(nextSpecialAnimation < SDL_GetTicks()) {
            specialAnim.getAnimation()->setNumLoops(1);
            nextSpecialAnimation = SDL_GetTicks() + getRandomInt(8000, 20000);
        }
    }

    const Point mouse(drawnMouseX - getPosition().x, drawnMouseY - getPosition().y);
    bool bPressed = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT));

    const Point eyesPos = windowWidget.getWidgetPosition(&eyesAnim);
    const Point& eyesSize = eyesAnim.getSize();
    const Point eyesCenter = eyesPos + eyesSize/2;
    const Point mouseEyePos = mouse - eyesCenter;

    eyesAnim.getAnimation()->resetFrameOverride();

    if((mouseEyePos.x >= -eyesSize.x/2 - 30) && (mouseEyePos.x <= -eyesSize.x/2) && (mouseEyePos.y >= -eyesSize.y/2 - 20) && (mouseEyePos.y <= eyesSize.y/2)) {
         eyesAnim.getAnimation()->setFrameOverride(MentatEyesLeft);
    } else if((mouseEyePos.x <= eyesSize.x/2 + 30) && (mouseEyePos.x >= eyesSize.x/2) && (mouseEyePos.y >= -eyesSize.y/2 - 20) && (mouseEyePos.y <= eyesSize.y/2)) {
         eyesAnim.getAnimation()->setFrameOverride(MentatEyesRight);
    } else if((abs(mouseEyePos.x) < eyesSize.x) && (mouseEyePos.y >= -eyesSize.y/2 - 20) && (mouseEyePos.y <= eyesSize.y/2)) {
         eyesAnim.getAnimation()->setFrameOverride(MentatEyesNormal);
    } else if((abs(mouseEyePos.x) < eyesSize.x) && (mouseEyePos.y > eyesSize.y/2) && (mouseEyePos.y <= eyesSize.y/2 + 15)) {
         eyesAnim.getAnimation()->setFrameOverride(MentatEyesDown);
    }

    if(bPressed && (abs(mouseEyePos.x) <= eyesSize.x/2) && (abs(mouseEyePos.y) <= eyesSize.y/2)) {
        eyesAnim.getAnimation()->setFrameOverride(MentatEyesClosed);
    }

    const Point mouthPos = windowWidget.getWidgetPosition(&mouthAnim);
    const Point& mouthSize = mouthAnim.getSize();
    const Point mouthCenter = mouthPos + mouthSize/2;
    const Point mouseMouthPos = mouse - mouthCenter;

    if(bPressed) {
        if((abs(mouseMouthPos.x) <= mouthSize.x/2) && (abs(mouseMouthPos.y) <= mouthSize.y/2)) {
            if(mouthAnim.getAnimation()->getCurrentFrameOverride() == INVALID_FRAME) {
                mouthAnim.getAnimation()->setFrameOverride(getRandomOf({MentatMouthOpen1, MentatMouthOpen2, MentatMouthOpen3, MentatMouthOpen4}));
            }
        } else {
            mouthAnim.getAnimation()->resetFrameOverride();
        }
    } else {
        mouthAnim.getAnimation()->resetFrameOverride();
    }
}

void MentatMenu::drawSpecificStuff() {
    Point shoulderPos;
    const int shoulderIdentity = house == HOUSE_INVALID ? HOUSE_INVALID
        : ModManager::instance().getEffectiveMentatIdentity(house);
    switch(shoulderIdentity) {
        case HOUSE_HARKONNEN:
        case HOUSE_SARDAUKAR: {
            shoulderPos = Point(256,209) + getPosition();
        } break;

        case HOUSE_ATREIDES:
        case HOUSE_FREMEN: {
            shoulderPos = Point(256,257) + getPosition();
        } break;

        case HOUSE_NEUTRAL:
        case HOUSE_REBELS: {
            shoulderPos = Point(0,0) + getPosition();
        } break;

        case HOUSE_ORDOS:
        case HOUSE_MERCENARY: {
            shoulderPos = Point(256,257) + getPosition();
        } break;

        default: {
            shoulderPos = Point(256,257) + getPosition();
        } break;
    }

    SDL_Texture* foreground = pGFXManager->getMentatForeground(mentatPresentationHouse);
    if(foreground != nullptr) {
        SDL_Rect destination{ getPosition().x, getPosition().y, 0, 0 };
        if(SDL_QueryTexture(foreground, nullptr, nullptr, &destination.w, &destination.h) == 0) {
            SDL_RenderCopy(renderer, foreground, nullptr, &destination);
        }
    } else {
        shoulderAnim.draw(shoulderPos);
    }
    textLabel.draw(Point(10,5) + getPosition());
}



int MentatMenu::getMissionSpecificAnim(int missionnumber) const {

    static const int missionnumber2AnimID[] = { Anim_ConstructionYard,
                                                Anim_Harvester,
                                                Anim_Radar,
                                                Anim_Quad,
                                                Anim_Tank,
                                                Anim_RepairYard,
                                                Anim_HeavyFactory,
                                                Anim_IX,
                                                Anim_Palace,
                                                Anim_Sardaukar };

    if(missionnumber < 0 || missionnumber > 9) {
        return missionnumber2AnimID[0];
    } else {
        return missionnumber2AnimID[missionnumber];
    }
}
