// SPDX-License-Identifier: GPL-2.0-or-later
#include <GUI/dune/AchievementsWindow.h>
#include <Achievements/AchievementManager.h>
#include <globals.h>
#include <algorithm>
#include <sstream>

AchievementsWindow::AchievementsWindow()
 : Window(0,0,std::min(740,getRendererWidth()-24),std::min(500,getRendererHeight()-24)),french(settings.general.language=="fr") {
    setCurrentPosition((getRendererWidth()-getSize().x)/2,(getRendererHeight()-getSize().y)/2,getSize().x,getSize().y);
    setWindowWidget(&contents);
    caption.setAlignment(Alignment_HCenter);
    contents.addWidget(&caption,Point(16,10),Point(getSize().x-32,30));
    text.setTextFontSize(14);
    text.setAlignment(Alignment_Left);
    contents.addWidget(&text,Point(18,48),Point(getSize().x-36,getSize().y-112));
    achievementButton.setText(french?"Hauts faits":"Achievements");
    achievementButton.setOnClick([this]{showAchievements();});
    contents.addWidget(&achievementButton,Point(18,getSize().y-45),Point(135,26));
    statisticsButton.setText(french?"Statistiques":"Statistics");
    statisticsButton.setOnClick([this]{showStatistics();});
    contents.addWidget(&statisticsButton,Point(165,getSize().y-45),Point(135,26));
    closeButton.setText(french?"Fermer":"Close");
    closeButton.setOnClick([this]{if(auto* parent=dynamic_cast<Window*>(getParent()))parent->closeChildWindow();});
    contents.addWidget(&closeButton,Point(getSize().x-143,getSize().y-45),Point(125,26));
    showAchievements();
}
void AchievementsWindow::showAchievements(){
    auto& manager=achievements::AchievementManager::instance();std::ostringstream out;unsigned count=0;
    if(manager.completed()) out<<(french ? "JEU COMPLÉTÉ À 100 % — Tous les hauts faits obtenus !\n\n" : "GAME 100% COMPLETE — All achievements unlocked!\n\n");
    for(const auto& a:manager.definitions()){
        const bool unlocked=manager.unlocked(a.id);count+=unlocked;
        out<<(unlocked?"[OK] ":"[--] ")<<manager.displayName(a,french)<<"\n";
        out<<manager.displayDescription(a,french)<<"\n";
        out<<(french?(unlocked?"Déverrouillé":"Verrouillé"):(unlocked?"Unlocked":"Locked"));
        if(!unlocked&&!a.secret&&(a.rule=="Lifetime"||a.rule=="CampaignCount"||a.rule=="CampaignHouse"||a.rule=="ModCampaignCount"||a.rule=="VictoryHouses"))out<<" — "<<manager.progress(a)<<" / "<<a.target;
        out<<"\n\n";
    }
    caption.setText(std::string(french?"Hauts faits":"Achievements")+" ("+std::to_string(count)+" / "+std::to_string(manager.definitions().size())+")");
    if(!manager.error().empty())out<<(french?"La progression n’a pas pu être enregistrée.\n":"Progress could not be saved.\n")<<manager.error();
    text.setText(out.str());
}
void AchievementsWindow::showStatistics(){
    auto& manager=achievements::AchievementManager::instance();std::ostringstream out;
    const char* keys[]={"GamesWon","GamesLost","SpiceHarvested","UnitsBuilt","UnitsLost","EnemyUnitsDestroyed","EnemyStructuresDestroyed","StructuresBuilt","StructuresSold","CreditsFromSales","StructuresCaptured","SandwormsKilled","InfantryCrushed","SpiceBloomsTriggered","PalaceAbilitiesUsed","FlameTankKills","CoopVictoriesRescued","CoopVictoriesRescuing","PalacesSabotaged"};
    const char* labels[]={"Victoires","Défaites","Crédits d’épice raffinés","Unités produites","Unités perdues","Unités ennemies détruites","Bâtiments ennemis détruits","Structures construites","Bâtiments vendus","Crédits récupérés (ventes)","Structures capturées","Vers des sables tués","Fantassins écrasés","Éclosions d’épice déclenchées","Pouvoirs de palais utilisés","Éliminations avec des chars lance-flammes","Victoires coop après son élimination","Victoires coop pour un allié éliminé","Palais détruits par sabotage"};
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i)out<<(french?labels[i]:keys[i])<<" : "<<manager.statistic(keys[i])<<"\n";
    out<<"\n"<<(french?"Victoires par faction":"Victories by faction")<<"\n\n";
    const char* houses[]={"Atreides","Harkonnen","Ordos","Fremen","Sardaukar","Mercenary","Neutral","Rebels","Corruptique","Wildspade","Kleshmersh","Tharpique"};
    for(auto house:houses)out<<house<<" : "<<manager.statistic(std::string("Victories")+house)<<"\n";
    caption.setText(french?"Statistiques cumulées":"Lifetime statistics");text.setText(out.str());
}
