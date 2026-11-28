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

#ifndef BRIEFINGMENU_H
#define BRIEFINGMENU_H

#include "MentatMenu.h"
#include <MissionObjectives.h>
#include <FileClasses/Animation.h>
#include <GUI/PictureButton.h>
#include <GUI/dune/AnimationLabel.h>

#include <memory>

#define BRIEFING        0
#define DEBRIEFING_WIN  1
#define DEBRIEFING_LOST 2

class BriefingMenu : public MentatMenu
{
public:
    /// `scenarioFilename` names the mission's map: a DuneCity campaign mission (DCITY_M??) takes
    /// its text from the map's [BRIEFING] section (Text, Win, Lose) instead of the TEXT files.
    BriefingMenu(int newHouse,int mission, int type, const std::string& scenarioFilename = "");
    virtual ~BriefingMenu();

    /// A DuneCity campaign briefing ends on the mission's name and goals, shown with Proceed.
    void setObjectives(const MissionObjectivePolicy::Briefing& briefing);

    void onMentatTextFinished() override;

    int showMenu() override;

private:
    std::string text;
    std::string missionName; ///< [BRIEFING] Name of a DuneCity campaign mission

    void onRepeat();
    void onProcced();
    int mission;
    int type;
    PictureButton   proccedButton;
    PictureButton   repeatButton;
    std::unique_ptr<Animation> tornieHouseAnimation;
    AnimationLabel  animation;
};

#endif //BRIEFINGMENU_H
