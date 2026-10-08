#ifndef DUNECITY_MAP_METADATA_H
#define DUNECITY_MAP_METADATA_H
#include <FileClasses/INIFile.h>
#include <algorithm>
#include <cctype>
#include <string>

// BASIC.Version is the map FORMAT. MapVersion/Workshop.Version describe a revision.
//
// The map CATEGORY (MapMetadata::mod) is derived from the buildings the map
// actually places in [STRUCTURES]. Sparse city-named starter maps also count
// as DuneCity. The active mod,
// units and previously stored tags do not decide the category. Only three categories exist:
// "vanilla", "tornie" and "dunecity"; city content wins over Tornie content on
// mixed maps. The name tables below mirror getItemIDByName()/getItemNameByID()
// in src/sand.cpp for the sets described by DuneCity::isCityOnlyStructure()
// (include/dunecity/CityConstants.h) and isTornieExclusiveItem() (include/data.h);
// they are duplicated here on purpose so the classifier stays header-only and
// usable from the tests and tools without linking the engine.
struct MapMetadata {
    static constexpr const char* ModVanilla  = "vanilla";
    static constexpr const char* ModTornie   = "tornie";
    static constexpr const char* ModDuneCity = "dunecity";

    // Which content set a single serialized building name belongs to.
    enum class BuildingClass { Vanilla, Tornie, City };

    std::string name, mod, dependency;
    int width=0, height=0, players=0, version=0;
    static std::string canonicalMod(std::string value) {
        std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return std::tolower(c);});
        if(value=="?") return "";
        if(value=="dune city") return ModDuneCity;
        if(value=="dune legacy") return ModVanilla;
        return value;
    }
    // Normalise any incoming tag onto the three categories that exist. Anything
    // that is not city or Tornie content is plain vanilla content.
    static std::string canonicalCategory(const std::string& value) {
        const std::string id=canonicalMod(value);
        return (id==ModTornie||id==ModDuneCity)?id:ModVanilla;
    }
    // Canonical category id -> label shown in the user interface.
    static std::string modLabel(const std::string& category) {
        if(category==ModTornie) return "Tornie";
        if(category==ModDuneCity) return "DuneCity";
        return "Vanilla";
    }
    static std::string trimmedLower(std::string value) {
        const auto first=value.find_first_not_of(" \t\r\n");
        if(first==std::string::npos) return "";
        value=value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
        std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return std::tolower(c);});
        return value;
    }
    static BuildingClass classifyBuilding(const std::string& buildingName) {
        const std::string n=trimmedLower(buildingName);
        for(const char* city:{"residential zone","zone residential","commercial zone","zone commercial",
                              "industrial zone","zone industrial","road","power line","powerline",
                              "nuclear","nuclear plant","police station","police","stadium","airport"})
            if(n==city) return BuildingClass::City;
        for(const char* tornie:{"advanced windtrap","advanced wind trap","advanced windtrap 3x3","advanced wind trap 3x3",
                                "advanced windtrap mk2","advanced wind trap mk2","advanced windtrap 2x3","advanced wind trap 2x3",
                                "advanced windtrap mk3","advanced wind trap mk3","advanced windtrap 3x2","advanced wind trap 3x2",
                                "worfinery","tech center","techcenter","scoutpost","scout post","green post",
                                "sentinel post","avant-poste","avant poste","flamepost","flame post",
                                "chemipost","chemi post","love factory","lovefactory","chaos factory","chaosfactory"})
            if(n==tornie) return BuildingClass::Tornie;
        return BuildingClass::Vanilla;
    }
    // Derive the map category from the buildings in [STRUCTURES]. Entries are
    // "ID###=House,Building,health,position" or "GEN###=House,Building"; only the
    // building field matters. Unknown names never change the category.
    static int countPlayers(const INIFile& ini) {
        int result=0;
        for(const char* name:{"Harkonnen","Atreides","Ordos","Fremen","Sardaukar","Mercenary","Rebels","Custom","Wildspade","Kleshmersh","Tharpique"})
            if(ini.hasSection(name)) ++result;
        for(int i=1;i<=12;++i) if(ini.hasSection("Player"+std::to_string(i))) ++result;
        return result;
    }
    static std::string inferMod(const INIFile& ini, const std::string& fallbackName="") {
        bool tornie=false;
        int buildings=0;
        if(ini.hasSection("STRUCTURES")) for(const INIFile::Key& key : ini.getSection("STRUCTURES")) {
            const auto id=trimmedLower(key.getKeyName());
            const size_t prefix=id.rfind("id",0)==0?2:id.rfind("gen",0)==0?3:0;
            if(!prefix || id.size()==prefix || !std::all_of(id.begin()+prefix,id.end(),[](unsigned char c){return std::isdigit(c);})) continue;
            const std::string value=key.getStringValue();
            const auto house=value.find(',');
            if(house==std::string::npos) continue;
            const auto end=value.find(',',house+1);
            const std::string building=trimmedLower(value.substr(house+1,end==std::string::npos?std::string::npos:end-house-1));
            if(building!="wall" && building!="concrete" && building!="slab1" && building!="slab4") ++buildings;
            switch(classifyBuilding(building)) {
                case BuildingClass::City: return ModDuneCity;
                case BuildingClass::Tornie: tornie=true; break;
                case BuildingClass::Vanilla: break;
            }
        }
        if(tornie) return ModTornie;
        const auto name=trimmedLower(ini.getStringValue("BASIC","Name",fallbackName));
        if((name.find("city")!=std::string::npos || name.find("cities")!=std::string::npos)
           && buildings<=std::max(4,2*countPlayers(ini))) return ModDuneCity;
        return ModVanilla;
    }
    static MapMetadata read(const INIFile& ini, const std::string& fallbackName) {
        MapMetadata m;
        m.name=ini.getStringValue("BASIC","Name",fallbackName);
        // Old tags do not override content. A verified sidecar may supply a
        // different exact gameplay dependency to the map chooser.
        m.mod=inferMod(ini,fallbackName);
        m.dependency=m.mod;
        m.version=std::max(0,ini.getIntValue("BASIC","MapVersion",0));
        if(ini.hasKey("MAP","Seed")) {
            const int scale=ini.getIntValue("BASIC","MapScale",-1);
            m.width=m.height=scale==0?62:scale==1?32:scale==2?21:64;
        } else {
            m.width=ini.getIntValue("MAP","SizeX",0);m.height=ini.getIntValue("MAP","SizeY",0);
        }
        m.players=countPlayers(ini);
        return m;
    }
    // The type filter accepts a map by its derived category OR by the exact mod
    // dependency a verified sidecar recorded for it. The two disagree precisely for
    // a locally edited workshop map: the edit can remove the content that named the
    // category (a downloaded city map saved without city buildings classifies as
    // vanilla) while the revision it still belongs to is unchanged. Filtering on the
    // category alone hid such a map from the very mod it was authored for. Bundled,
    // downloaded and metaserver rows are unaffected: read() sets dependency=mod, and
    // a metaserver row leaves it empty.
    bool matches(const std::string& filterMod, int sizeBand, int maxPlayers) const {
        const int extent=std::max(width,height);
        const int band=extent<=64?1:extent<=128?2:extent<=192?3:extent<=256?4:5;
        return (filterMod.empty() || mod==filterMod || dependency==filterMod)
            && (!sizeBand || band==sizeBand) && (!maxPlayers || players==maxPlayers);
    }
};
#endif
