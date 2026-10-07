#include <catch2/catch_test_macros.hpp>
#include <mod/Workshop.h>
#include <fstream>
#include <filesystem>

namespace {
struct Fixture {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("workshop-test-"+Workshop::newID());
    Workshop::Store store{root/"cache"};
    std::filesystem::path source=root/"draft";
    Fixture(){std::filesystem::create_directories(source);write("mod.ini","[Mod]\nDisplay Name = Test\n");write("ObjectData.ini","[Tank]\nPrice = 500\n");}
    ~Fixture(){std::error_code e;std::filesystem::remove_all(root,e);}
    void write(const std::string& name,const std::string& data){auto p=source/name;std::filesystem::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary);out<<data;}
    Workshop::Revision save(const std::string& id=std::string(32,'a')){return store.capture("mod",id,"Test","vanilla","",source);}
};
}
TEST_CASE("Workshop SHA256 and hex match published vectors","[workshop]") {
    REQUIRE(Workshop::hashBytes("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(Workshop::hashBytes("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(Workshop::unhex(Workshop::hex(std::string("a\0b",3)))==std::string("a\0b",3));
    REQUIRE_THROWS(Workshop::unhex("0x"));
}
TEST_CASE("Workshop snapshots preserve full assets and only changed saves create versions","[workshop]") {
    Fixture f;f.write("graphics/units/tank.png",std::string(100000,'x'));f.write("campaign/SCENA001.INI","scenario");
    auto first=f.save();REQUIRE(first.version==1);REQUIRE(first.files.size()==4);
    auto same=f.save();REQUIRE(same.hash==first.hash);REQUIRE(same.version==1);
    f.write("ObjectData.ini","[Tank]\nPrice = 600\n");auto second=f.save();REQUIRE(second.version==2);REQUIRE(second.hash!=first.hash);
    REQUIRE(f.store.get(first.hash).hash==first.hash);REQUIRE(f.store.list().size()==2);
    REQUIRE_THROWS(f.store.verifyDirectory(first,f.source));
    REQUIRE_NOTHROW(f.store.verifyDirectory(second,f.source));
    f.write("unexpected.ini","extra");REQUIRE_THROWS(f.store.verifyDirectory(second,f.source));
}
TEST_CASE("Workshop same-name independent items do not share version history","[workshop]") {
    Fixture f;auto a=f.save();f.write("ObjectData.ini","different");auto b=f.save(std::string(32,'b'));
    REQUIRE(a.name==b.name);REQUIRE(a.id!=b.id);REQUIRE(b.version==1);
}
TEST_CASE("Workshop identity for bundled packages includes optional files","[workshop]") {
    Fixture f;auto a=f.save("");auto same=f.save("");REQUIRE(a.hash==same.hash);
    f.write("graphics/optional.png","pixels");auto b=f.save("");REQUIRE(a.id!=b.id);
}
TEST_CASE("Workshop downloaded bytes must match manifest and corrupt cache can be repaired","[workshop]") {
    Fixture f;auto r=f.save();Workshop::Store other(f.root/"download");
    f.write("ObjectData.ini","bad");REQUIRE_THROWS(other.importRevision(r.manifest,r.hash,1,f.source));REQUIRE(other.list().empty());
    auto imported=other.importRevision(r.manifest,r.hash,1,r.directory);REQUIRE(imported.hash==r.hash);
    {std::ofstream bad(std::filesystem::path(imported.directory)/"ObjectData.ini");bad<<"tampered";}
    REQUIRE_THROWS(other.get(r.hash));
    REQUIRE_NOTHROW(other.importRevision(r.manifest,r.hash,1,r.directory));
    REQUIRE(other.get(r.hash).hash==r.hash);
    REQUIRE_THROWS(other.importRevision(r.manifest,std::string(64,'0'),1,r.directory));
}
TEST_CASE("Workshop rejects unsafe paths case aliases and noncanonical manifests","[workshop]") {
    Fixture f;auto r=f.save();
    auto bad=r.manifest;bad.replace(bad.find("kind=mod"),8,"kind=map");REQUIRE_THROWS(Workshop::parseManifest(bad));
    auto row="file="+Workshop::hashBytes("x")+",1,";
    for(const std::string path:{"../escape","/tmp/x","a//b","a/../b","NUL.ini","CON","a\\b"," a","a. ","foo/bar/"}) {
        bad=r.manifest+row+Workshop::hex(path)+"\n";REQUIRE_THROWS(Workshop::parseManifest(bad));
    }
    bad=r.manifest+row+Workshop::hex("mod.ini")+"\n";REQUIRE_THROWS(Workshop::parseManifest(bad));
    f.write("DATA/a","one");f.write("data/b","two"); // directory case aliases differ on case-sensitive platforms only
    if(std::filesystem::exists(f.source/"DATA/b"))std::filesystem::remove_all(f.source/"DATA");
    else REQUIRE_THROWS(f.save());
}
TEST_CASE("Workshop rejects symlink payloads when the test account can create them","[workshop]") {
    Fixture f;
    std::error_code error;
    std::filesystem::create_symlink(f.source/"mod.ini",f.source/"link.ini",error);
#ifdef _WIN32
    if(error.value() == 1314) { // ERROR_PRIVILEGE_NOT_HELD; do not change OS policy.
        SKIP("Windows test account lacks the privilege to create a symlink fixture");
    }
#endif
    REQUIRE_FALSE(error);
    REQUIRE_THROWS(f.save());
}
TEST_CASE("Workshop maps pin required mod without confusing map format version","[workshop]") {
    Fixture f;auto mod=f.save();auto mapdir=f.root/"map";std::filesystem::create_directories(mapdir);
    {std::ofstream out(mapdir/"map.ini");out<<"[BASIC]\nVersion = 2\n[MAP]\nSizeX = 64\n";}
    auto map=f.store.capture("map",std::string(32,'b'),"Desert","",mod.hash,mapdir);
    REQUIRE(map.modHash==mod.hash);REQUIRE(map.version==1);REQUIRE(map.files.size()==1);
    REQUIRE(Workshop::parseManifest(map.manifest).kind=="map");
    f.write("ObjectData.ini","changed");auto nextmod=f.save();
    auto nextmap=f.store.capture("map",map.id,"Desert","",nextmod.hash,mapdir);
    REQUIRE(nextmap.version==2);REQUIRE(nextmap.hash!=map.hash);
}
TEST_CASE("Workshop publication assigns server numbers without duplicate local versions","[workshop]") {
    Fixture f;auto a=f.save();f.write("ObjectData.ini","second");auto b=f.save();
    f.store.setSharedVersion(a.hash,2);
    REQUIRE(f.store.get(a.hash).version==2);REQUIRE(f.store.get(b.hash).version==3);
    f.store.setSharedVersion(a.hash,2);REQUIRE(f.store.get(a.hash).version==2);
    auto same=f.save();REQUIRE(same.hash==b.hash);REQUIRE(same.version==3);
    REQUIRE(f.store.owner()==f.store.owner());REQUIRE(f.store.owner().size()==64);
}

#include <INIMap/MapMetadata.h>
TEST_CASE("Map catalogue separates format version from map revision and counts every slot", "[workshop][map-catalogue]") {
    INIFile ini(false,std::string("metadata fixture"));
    ini.setIntValue("BASIC","Version",2);
    ini.setIntValue("BASIC","MapVersion",7);
    ini.setStringValue("BASIC","Mod","DuneCity");
    ini.setStringValue("BASIC","Name","Alkozeltser 4 Cities");
    ini.setIntValue("MAP","SizeX",256);ini.setIntValue("MAP","SizeY",128);
    ini.setIntValue("Rebels","Credits",1000);ini.setIntValue("Player12","Credits",1000);
    auto m=MapMetadata::read(ini,"fallback");
    REQUIRE(m.name=="Alkozeltser 4 Cities");REQUIRE(m.version==7);
    REQUIRE(m.mod=="dunecity");REQUIRE(m.players==2);
    REQUIRE(m.matches("dunecity",4,2));
    REQUIRE_FALSE(m.matches("vanilla",4,2));REQUIRE_FALSE(m.matches("",3,2));REQUIRE_FALSE(m.matches("",0,6));
}
TEST_CASE("Untagged legacy maps default to Vanilla without inventing a revision", "[workshop][map-catalogue]") {
    INIFile ini(false,std::string("legacy fixture"));
    ini.setIntValue("MAP","Seed",123);ini.setIntValue("BASIC","MapScale",0);
    ini.setIntValue("BASIC","Version",1);
    auto m=MapMetadata::read(ini,"Legacy map");
    REQUIRE(m.width==62);REQUIRE(m.height==62);REQUIRE(m.version==0);REQUIRE(m.mod=="vanilla");
    REQUIRE(m.matches("vanilla",1,0));REQUIRE_FALSE(m.matches("dunecity",0,0));
}

TEST_CASE("Map category uses buildings and sparse city names rather than old tags", "[workshop][map-catalogue]") {
    INIFile ini(false,std::string("category fixture"));
    ini.setStringValue("BASIC","Mod","dunecity");
    ini.setStringValue("UNITS","ID0","Atreides,Rocket Trike,256,12");
    ini.setStringValue("STRUCTURES","Description","Atreides,Nuclear,256,12");
    REQUIRE(MapMetadata::inferMod(ini,"Desert")=="vanilla");
    ini.setStringValue("STRUCTURES","ID0","Atreides,Advanced Wind Trap MK2,256,12");
    REQUIRE(MapMetadata::inferMod(ini,"City")=="tornie");
    ini.setStringValue("STRUCTURES","GEN14","Atreides,Road");
    REQUIRE(MapMetadata::inferMod(ini,"Desert")=="dunecity");
    ini.clearSection("STRUCTURES");
    ini.setIntValue("Player1","Credits",1000);ini.setIntValue("Player2","Credits",1000);
    for(int i=0;i<4;++i) ini.setStringValue("STRUCTURES","ID"+std::to_string(i),"Atreides,Const Yard,256,12");
    REQUIRE(MapMetadata::inferMod(ini,"Twin Cities")=="dunecity");
    ini.setStringValue("STRUCTURES","ID4","Atreides,Refinery,256,24");
    REQUIRE(MapMetadata::inferMod(ini,"Twin Cities")=="vanilla");
    ini.setStringValue("STRUCTURES","ID4","Atreides,Concrete");
    REQUIRE(MapMetadata::inferMod(ini,"Twin Cities")=="dunecity");
    REQUIRE(MapMetadata::read(ini,"Desert").mod=="vanilla");
    REQUIRE(MapMetadata::modLabel("vanilla")=="Vanilla");
}

TEST_CASE("Repeated map metadata updates replace complete INI values", "[workshop][map-metadata]") {
    Fixture f;
    f.write("metadata.ini", "[BASIC]\nMod=vanilla ; keep comment\n");
    INIFile ini((f.source/"metadata.ini").string());
    for(const std::string value : {"dunecity", "vanilla", "City starter", "x", "", "tornie"}) {
        ini.setStringValue("BASIC", "Mod", value);
        REQUIRE(ini.getStringValue("BASIC", "Mod")==value);
        REQUIRE(ini.saveChangesTo((f.source/"roundtrip.ini").string()));
        INIFile saved((f.source/"roundtrip.ini").string());
        REQUIRE(saved.getStringValue("BASIC", "Mod")==value);
    }
    std::ifstream input(f.source/"roundtrip.ini");
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    REQUIRE(text.find("; keep comment")!=std::string::npos);
}
