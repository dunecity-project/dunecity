// Real editor and Custom Game lists, with unchanged files in the isolated profile.
#include <MapEditor/LoadMapWindow.h>
void verifySavedUserMapLists() {
    namespace fs=std::filesystem;
    menuRequire(std::getenv("DUNECITY_USERDIR")!=nullptr,"Map list check needs an isolated profile");
    const auto directory=fs::path(std::getenv("DUNECITY_USERDIR"))/"maps/singleplayer";
    fs::create_directories(directory);
    const std::array<std::string,2> names={"map-list-regression-New-Beginning","map-list-regression-BigCityLife"};
    const std::array<std::string,2> labels={"4P - 192x192 - New Beginning","4P - 192x192 - BigCityLife"};
    std::array<std::string,2> contents;
    for(int i=0;i<2;++i) {
        std::string bytes="[BASIC]\nVersion=2\nMod=vanilla\nName="+labels[i]+"\n[MAP]\nSizeX=192\nSizeY=192\n";
        for(int y=0;y<192;++y) {
            std::string row(192,'%');
            if(i==1 && y==0)row[0]='-'; // distinct playable content, not a duplicate copy
            const auto number=std::to_string(1000+y).substr(1);
            bytes+=number+"="+row+"\n";
        }
        for(int h=1;h<=4;++h)bytes+="[Player"+std::to_string(h)+"]\nCredits=2000\n";
        contents[i]=bytes;
        {std::ofstream map(directory/(names[i]+".ini"));map<<bytes;}
        {std::ofstream metadata(directory/(names[i]+".ini.workshop.ini"));
         metadata<<"[Workshop]\nName="<<labels[i]<<"\nMod=dunecity\nVersion=2\nImmutable=false\n";}
    }
    {
        LoadMapWindow editor;
        editor.onMapTypeChange(0);
        for(const auto& name:names) {
            bool present=false;
            for(int i=0;i<editor.mapList.getNumEntries();++i)
                if(editor.mapList.getEntry(i)==name)present=true;
            menuRequire(present,"Editor lost a saved map");
        }
        for(int i=0;i<editor.mapList.getNumEntries();++i)
            menuRequire(editor.mapList.getEntry(i).find(".ini.workshop")==std::string::npos,
                "Editor listed revision metadata as a map");
        menuCapture(editor,"saved-user-maps-editor");
    }
    {
        CustomPlaySetup setup;setup.mods=ModManager::instance().listModChoices();setup.rules=effectiveGameOptions;
        CustomGameMenu browser(false,false,&setup);
        browser.onMapTypeChange(1);
        browser.mapModFilter.setSelectedItem(3); // DuneCity
        browser.mapSizeFilter.setSelectedItem(3); // 192
        browser.mapPlayersFilter.setSelectedItem(4);
        browser.rebuildMapList();
        for(int n=0;n<2;++n) {
            bool present=false;
            for(const auto visible:browser.visibleMaps) {
                const auto& entry=browser.mapEntries[visible];
                if(fs::path(entry.path).filename()==names[n]+".ini") {
                    present=true;
                    menuRequire(entry.metadata.dependency=="dunecity","Authored dependency changed");
                    if(n==0)menuRequire(entry.metadata.mod=="vanilla","Filtering rewrote map category");
                }
            }
            menuRequire(present,"DuneCity filter lost an authored saved map");
        }
        menuCapture(browser,"saved-user-maps-custom-game");
    }
    for(int i=0;i<2;++i) {
        const auto path=directory/(names[i]+".ini");
        menuRequire(readCompleteFile(path.string())==contents[i],"Browsing modified a saved map");
        fs::remove(path);fs::remove(path.string()+".workshop.ini");
    }

    // Global copies must remain usable on a fresh profile without the author's
    // private Workshop cache. Catalogue hints are not a pinned revision.
    {
        CustomPlaySetup setup;setup.mods=ModManager::instance().listModChoices();setup.rules=effectiveGameOptions;
        CustomGameMenu browser(false,false,&setup);
        browser.onMapTypeChange(0); // installed single-player maps
        browser.mapModFilter.setSelectedItem(3); // DuneCity, all sizes and slot counts
        browser.rebuildMapList();
        const std::array<const char*,11> bundled={
            "2P - 128x128 Four Quadrants.ini", "4P - 128x128 - 4 corners.ini",
            "4P - 192x192 - BigCityLife.ini", "4P - 192x192 - DuneCity.ini",
            "4P - 192x192 - New Beginning.ini", "4P - 384x384 - new beginning.ini",
            "5P - 256x256 - test.ini", "8P - 128x128 - city seige.ini",
            "8P - 256x256 - Colonists.ini", "8P - 384x384 - Colonist.ini",
            "3P - 192x192 - 3waysplit.ini"};
        for(const auto* name:bundled) {
            bool present=false;
            for(const auto visible:browser.visibleMaps) {
                const auto& entry=browser.mapEntries[visible];
                if(fs::path(entry.path).filename()!=name)continue;
                present=true;
                menuRequire(entry.metadata.dependency=="dunecity","Bundled scenario lost its authored mod");
                const auto before=readCompleteFile(entry.path);
                GameInitSettings init(name,before,false,setup.rules);
                menuRequire(!WorkshopGameContent::applyMapRevisionForSelectedMod(entry.path,init),
                    "Catalogue-only metadata became a pinned revision");
                menuRequire(init.getMapRevisionHash().empty(),"Fresh scenario unexpectedly needs an old revision");
                menuRequire(readCompleteFile(entry.path)==before,"Selecting a bundled map changed its contents");
            }
            if(!present) throw std::runtime_error(std::string("Global DuneCity list lost ")+name);
        }
        menuCapture(browser,"bundled-user-scenarios");
    }

    // A missing nonempty revision must still fail instead of silently starting
    // the unpinned map. Only catalogue metadata with no hash uses the fallback.
    const auto bad=directory/"map-list-bad-revision.ini";
    {std::ofstream map(bad);map<<contents[0];}
    {std::ofstream metadata(bad.string()+".workshop.ini");
     metadata<<"[Workshop]\nMod=dunecity\nHash="<<std::string(64,'f')<<"\n";}
    bool rejected=false;
    try {
        GameInitSettings init(bad.filename().string(),contents[0],false,effectiveGameOptions);
        WorkshopGameContent::applyMapRevisionForSelectedMod(bad.string(),init);
    } catch(const std::exception&) {rejected=true;}
    menuRequire(rejected,"Invalid pinned revision was silently accepted");
    fs::remove(bad);fs::remove(bad.string()+".workshop.ini");
}
