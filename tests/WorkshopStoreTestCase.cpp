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
TEST_CASE("Workshop content hash is the captured hash, with substituted bytes and nothing stored","[workshop]") {
    // A save's shipped mod pins mod.ini with the release that wrote it; the same files under
    // another stamp must be recognisable without capturing a revision of bytes nobody has.
    Fixture f;const std::string restamped="[Mod]\nDisplay Name = Test\nGame Version = 1.0.850\n";
    const auto hash=Workshop::contentHash("mod","","Test","vanilla","",f.source);
    const auto other=Workshop::contentHash("mod","","Test","vanilla","",f.source,{{"mod.ini",restamped}});
    REQUIRE(f.store.list().empty());REQUIRE(other!=hash);
    REQUIRE(f.save("").hash==hash);
    f.write("mod.ini",restamped);REQUIRE(f.save("").hash==other);
    REQUIRE(Workshop::contentHash("mod",std::string(32,'a'),"Test","vanilla","",f.source)==f.save().hash);
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
TEST_CASE("Opening a profile drops the retired sharing queue and keeps every revision","[workshop][outbox]") {
    namespace fs=std::filesystem;
    Fixture f;auto mod=f.save();
    // An older release left queued uploads and a collection receipt beside the revision.
    const auto cache=f.root/"cache";
    fs::create_directories(cache/"outbox");
    for(const auto& marker:{mod.hash,std::string(64,'c')}) std::ofstream(cache/"outbox"/marker)<<"collect";
    std::ofstream(cache/"revisions"/mod.hash/"collected")<<std::string(64,'d');
    Workshop::Store reopened(cache);
    REQUIRE_FALSE(fs::exists(cache/"outbox"));
    REQUIRE(reopened.get(mod.hash).hash==mod.hash);
    REQUIRE(reopened.list("mod").size()==1);
    Workshop::Store again(cache); // A later start has nothing left to remove.
    REQUIRE(again.get(mod.hash).hash==mod.hash);
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


#include <mod/WorkshopDuplicates.h>
#include <FileClasses/INIFile.h>
#include <Definitions.h>
#include <cstring>
namespace {
namespace fs=std::filesystem;
/// A profile's mods folder with a shipped "dunecity" stamped 1.0.852 and installed Workshop copies.
struct ShippedFixture {
    fs::path root=fs::temp_directory_path()/("workshop-dup-"+Workshop::newID());
    fs::path mods=root/"mods";
    Workshop::ShippedMod shipped{"dunecity","Dune City","dunecity",mods/"dunecity"};
    ShippedFixture() {
        // Stamped the way ModManager::writeModInfo stamps it, so a same-stamp copy is byte-identical.
        put(shipped.path/"mod.ini","[Mod]\nDisplay Name = Dune City\nGame Version = 0\nAuthor = Dune City\n");
        INIFile ini((shipped.path/"mod.ini").string());ini.setStringValue("Mod","Game Version","1.0.852");
        ini.saveChangesTo((shipped.path/"mod.ini").string());
        put(shipped.path/"ObjectData.ini","[Tank]\nPrice = 500\n");
        put(shipped.path/"graphics/zone.png",std::string(5000,'z'));
    }
    ~ShippedFixture(){std::error_code e;fs::remove_all(root,e);}
    static void put(const fs::path& p,const std::string& data){fs::create_directories(p.parent_path());std::ofstream(p,std::ios::binary)<<data;}
    /// What an older release installed: the shipped files with mod.ini restamped as writeModInfo did.
    std::string install(const std::string& stamp,const std::string& extraFile={}) {
        const auto stage=root/("stage-"+Workshop::newID());
        fs::copy(shipped.path,stage,fs::copy_options::recursive);
        INIFile ini((stage/"mod.ini").string());ini.setStringValue("Mod","Game Version",stamp);ini.saveChangesTo((stage/"mod.ini").string());
        if(!extraFile.empty()) put(stage/extraFile,"extra");
        const auto hash=Workshop::contentHash("mod","","Dune City","dunecity","",stage);
        put(stage/"workshop-revision.ini","[Workshop]\nID = x\nVersion = 1\nHash = "+hash+"\nName = Dune City\nBase = dunecity\nImmutable = true\n");
        fs::rename(stage,mods/("ws-"+hash));
        return hash;
    }
};
}
TEST_CASE("Shipped-mod copies differing only in the Game Version stamp are found","[workshop][duplicates]") {
    ShippedFixture f;
    const auto old=f.install("1.0.840");
    const auto same=f.install("1.0.852");                       // byte-identical to the shipped mod
    f.install("1.0.841","maps/extra.ini");                      // more than a stamp differs
    REQUIRE(Workshop::stampedModHash(f.shipped,"1.0.840")==old);
    REQUIRE(Workshop::stampedModHash(f.shipped,{})==same);
    const auto copies=Workshop::findShippedModCopies(f.mods,{f.shipped});
    REQUIRE(copies.size()==2);
    for(const auto& copy:copies) {
        REQUIRE(copy.shippedMod=="dunecity");
        REQUIRE(((copy.hash==old && copy.gameVersion=="1.0.840") || (copy.hash==same && copy.gameVersion=="1.0.852")));
    }
    // A copy whose files were changed after installation is no copy of the shipped mod.
    ShippedFixture::put(f.mods/("ws-"+old)/"ObjectData.ini","[Tank]\nPrice = 1\n");
    REQUIRE(Workshop::findShippedModCopies(f.mods,{f.shipped}).size()==1);
}

TEST_CASE("Legacy builtin metadata pins require unchanged payload and mode","[workshop][compatibility]") {
    for(bool city : {false, true}) {
        ShippedFixture f;
        f.shipped.name = city ? "dunecity" : "vanilla";
        f.shipped.displayName = city ? "Dune City" : "Vanilla";
        f.shipped.base = f.shipped.name;
        INIFile current(false, std::string("Mod metadata"));
        current.setStringValue("Mod", "Display Name", f.shipped.displayName);
        current.setStringValue("Mod", "Author", city ? "Stefan" : "Dune City");
        current.setStringValue("Mod", "Description", city
            ? "Hybrid RTS + city-builder mode (zones, overlays, city sim)." : "Default game settings");
        current.setStringValue("Mod", "Version", city ? "1.002" : "");
        current.setStringValue("Mod", "Game Version", "1.0.852");
        current.setStringValue("Mod", "Base Mod", "");
        current.setBoolValue("Mod", "Enables City Mode", city);
        const auto metadataPath = (f.shipped.path / "mod.ini").string();
        REQUIRE(current.saveChangesTo(metadataPath));

        const auto historical = f.root / "historical";
        fs::copy(f.shipped.path, historical, fs::copy_options::recursive);
        // Captured legacy profile layouts, independently of the compatibility implementation.
        const std::string metadata = city
            ? "[Mod]\nDisplay Name = \"Dune City\"\nAuthor = Stefan\n"
              "Description = \"Hybrid RTS + city-builder mode (zones, overlays, city sim).\"\n"
              "Version = \nGame Version = 1.0.849\nEnables City Mode = true\n"
              "Version = 1.002\nBase Mod = \"\"\n"
            : "[Mod]\nDisplay Name = Vanilla\nAuthor = \"Dune City\"\n"
              "Description = \"Default game settings\"\nVersion = \nGame Version = 1.0.849\n"
              "Enables City Mode = false\nVersion = \"\"\nBase Mod = \"\"\n";
        ShippedFixture::put(historical / "mod.ini", metadata);
        const auto pin = Workshop::contentHash("mod", "", f.shipped.displayName, f.shipped.base, "", historical);
        REQUIRE(Workshop::legacyBundledModHash(f.shipped, "1.0.849") == pin);
        REQUIRE(Workshop::legacyBundledModHash(f.shipped, "1.0.850") != pin);

        INIFile resaved((historical / "mod.ini").string());
        resaved.setStringValue("Mod", "Game Version", "1.0.852");
        REQUIRE(resaved.saveChangesTo((historical / "mod.ini").string()));
        const auto currentPin = Workshop::contentHash("mod", "", f.shipped.displayName, f.shipped.base, "", historical);
        REQUIRE(Workshop::legacyBundledModHash(f.shipped, "1.0.852") == currentPin);

        current.setBoolValue("Mod", "Enables City Mode", !city);
        REQUIRE(current.saveChangesTo(metadataPath));
        REQUIRE(Workshop::legacyBundledModHash(f.shipped, "1.0.849").empty());
        current.setBoolValue("Mod", "Enables City Mode", city);
        current.setStringValue("Mentat 1", "Name", "Different presentation rules");
        REQUIRE(current.saveChangesTo(metadataPath));
        REQUIRE(Workshop::legacyBundledModHash(f.shipped, "1.0.849").empty());
        current.removeSection("Mentat 1");
        REQUIRE(current.saveChangesTo(metadataPath));

        ShippedFixture::put(f.shipped.path / "ObjectData.ini", "[Tank]\nPrice = 1\n");
        REQUIRE(Workshop::legacyBundledModHash(f.shipped, "1.0.849") != pin);
        f.shipped.name = "authored";
        REQUIRE(Workshop::legacyBundledModHash(f.shipped, "1.0.849").empty());
    }
}
TEST_CASE("A shipped-mod copy is needed only by a pin resolveMod would load through it","[workshop][duplicates]") {
    ShippedFixture f;
    const auto old=f.install("1.0.840");
    const auto copy=Workshop::findShippedModCopies(f.mods,{f.shipped}).at(0);
    const auto needs=[&](const std::string& mod,const std::string& hash,const std::string& version) {
        return Workshop::pinNeedsCopy({"save",mod,hash,version},copy,{f.shipped},"1.0.852");
    };
    REQUIRE_FALSE(needs("dunecity",old,"1.0.840"));   // re-stamped to the shipped mod
    REQUIRE_FALSE(needs("dunecity",std::string(64,'a'),"1.0.840"));   // another revision
    REQUIRE(needs("dunecity",old,"1.0.845"));          // another release saved it: no re-stamp
    REQUIRE(needs("dunecity",old,""));                 // a replay records no release
    REQUIRE(needs("ws-"+old,old,"1.0.840"));           // saved while running as the copy
    REQUIRE(needs("vanilla",old,"1.0.840"));
}
TEST_CASE("Pruning removes unneeded shipped-mod copies and keeps the rest","[workshop][duplicates]") {
    ShippedFixture f;
    const auto a=f.install("1.0.840"), b=f.install("1.0.845"), c=f.install("1.0.846");
    Workshop::ProfilePins pins;
    pins.pins.push_back({"needs-b.dls","ws-"+b,b,"1.0.845"});
    pins.pins.push_back({"fine-a.dls","dunecity",a,"1.0.840"});
    auto report=Workshop::pruneShippedModCopies(f.mods,{f.shipped},pins,"ws-"+c,"1.0.852");
    REQUIRE(report.removed.size()==1);REQUIRE(report.removed[0].hash==a);
    REQUIRE(report.kept.size()==2);REQUIRE_FALSE(report.complete);   // the active copy may go later
    REQUIRE_FALSE(fs::exists(f.mods/("ws-"+a)));
    REQUIRE(fs::exists(f.mods/("ws-"+b)));REQUIRE(fs::exists(f.mods/("ws-"+c)));
    REQUIRE(fs::exists(f.shipped.path/"mod.ini"));
    for(const auto& entry:fs::directory_iterator(f.mods)) REQUIRE(entry.path().filename().string().find(".removing")==std::string::npos);
    // A save from a newer release could pin anything: nothing goes.
    pins.unreadableNewer.push_back("newer.dls");
    report=Workshop::pruneShippedModCopies(f.mods,{f.shipped},pins,"dunecity","1.0.852");
    REQUIRE(report.removed.empty());REQUIRE_FALSE(report.complete);
    pins.unreadableNewer.clear();
    report=Workshop::pruneShippedModCopies(f.mods,{f.shipped},pins,"dunecity","1.0.852");
    REQUIRE(report.removed.size()==1);REQUIRE(report.removed[0].hash==c);REQUIRE(report.complete);
}
TEST_CASE("Pruning removes store-backed copies of shipped-mod revisions, needed or not","[workshop][duplicates]") {
    ShippedFixture f;
    const auto older=f.install("1.0.790","maps/older.ini");   // an older release's rules: no stamp-only copy
    const auto stale=f.install("1.0.840");                     // stamp-only, but a save runs as it
    const auto active=f.install("1.0.841","maps/active.ini");
    const std::string authored(64,'b');                         // a revision built on no shipped mod
    ShippedFixture::put(f.mods/("ws-"+authored)/"workshop-revision.ini",
        "[Workshop]\nHash = "+authored+"\nName = Mine\nBase = mymod\nImmutable = true\n");
    Workshop::ProfilePins pins;
    pins.pins.push_back({"older.dls","dunecity",older,"1.0.790"});
    pins.pins.push_back({"stale.dls","ws-"+stale,stale,"1.0.841"});
    // Without the store nothing of these may go.
    auto report=Workshop::pruneShippedModCopies(f.mods,{f.shipped},pins,"ws-"+active,"1.0.852",
                                                [](const std::string&) { return false; });
    REQUIRE(report.removed.empty());REQUIRE(report.kept.size()==3);
    // With every revision in the store, all but the active copy go; the authored one is untouched.
    report=Workshop::pruneShippedModCopies(f.mods,{f.shipped},pins,"ws-"+active,"1.0.852",
                                           [](const std::string&) { return true; });
    REQUIRE(report.removed.size()==2);REQUIRE(report.kept.size()==1);REQUIRE_FALSE(report.complete);
    REQUIRE_FALSE(fs::exists(f.mods/("ws-"+older)));REQUIRE_FALSE(fs::exists(f.mods/("ws-"+stale)));
    REQUIRE(fs::exists(f.mods/("ws-"+active)));REQUIRE(fs::exists(f.mods/("ws-"+authored)));
}
TEST_CASE("Profile pins skip damaged saves and flag a newer release's","[workshop][duplicates]") {
    ShippedFixture f;
    ShippedFixture::put(f.root/"save"/"damaged.dls","not a save");
    std::string newer(8,'\0');
    const Uint32 header[2]={SAVEMAGIC,SAVEGAMEVERSION+1};
    std::memcpy(newer.data(),header,sizeof(header));
    ShippedFixture::put(f.root/"save"/"newer.dls",newer);
    ShippedFixture::put(f.root/"replay"/"damaged.rpl","x");
    const auto pins=Workshop::readProfilePins({f.root/"save",f.root/"missing"},{f.root/"replay"});
    REQUIRE(pins.pins.empty());
    REQUIRE(pins.unreadableNewer.size()==1);
}
