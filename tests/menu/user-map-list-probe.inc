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
}
