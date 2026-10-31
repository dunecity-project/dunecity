// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef TORNIE_ACHIEVEMENTS_WINDOW_H
#define TORNIE_ACHIEVEMENTS_WINDOW_H
#include <GUI/Window.h>
#include <GUI/StaticContainer.h>
#include <GUI/Label.h>
#include <GUI/TextButton.h>
#include <GUI/TextView.h>
class AchievementsWindow final : public Window {
public:
    AchievementsWindow();
private:
    void showAchievements();
    void showStatistics();
    StaticContainer contents;
    Label caption;
    TextView text;
    TextButton achievementButton,statisticsButton,closeButton;
    bool french;
};
#endif
