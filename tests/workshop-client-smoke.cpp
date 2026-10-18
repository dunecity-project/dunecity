// Real Workshop client/store + bounded curl transport against the PHP content service.
// Test-only host plumbing supplies an isolated Store, settings, and the native no-op sync hook.
#include <mod/Workshop.h>
#include <mod/WorkshopClient.h>
#include <Network/BoundedHttpClient.h>
#include <DataTypes.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>

// Advance only the client's retry clock; curl deadlines and the smoke-test deadline remain real.
// This lets the PHP fixture inject HTTP 429 without adding a minute to the suite.
extern "C" Uint32 SDLCALL SDL_GetTicks(void) {
    static Uint32 ticks=0;
    return ticks+=1000;
}
SettingsClass settings;
std::string getDuneLegacyDataDir() { return "."; }
namespace WebRuntime { void syncPersistentFiles() {} }
namespace Workshop {
std::unique_ptr<Store> testStore;
Store& store() { return *testStore; }
}
namespace fs = std::filesystem;

static void check(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}
static void write(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);out.write(data.data(),data.size());out.close();
    check(bool(out), "Cannot write fixture");
}
static void finish(Workshop::Client& client) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(client.status()==Workshop::Client::Status::Busy && std::chrono::steady_clock::now()<deadline) {
        client.update();std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(client.status()==Workshop::Client::Status::Succeeded,client.message());
}
static std::map<std::string,std::string> post(const std::string& route,const std::string& body) {
    auto client=createBoundedHttpClient();BoundedHttpClient::Request request;
    request.url=settings.network.activeDirectEndpoint()+"/v1/content/"+route;
    request.body=body;request.maxResponseBytes=BoundedHttpClient::kMaxResponseBytes;
    client->begin(request);BoundedHttpClient::Result result;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    for(;;) {
        client->update();if(client->poll(result))break;
        check(std::chrono::steady_clock::now()<deadline,"Raw fixture request timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(result.httpStatus==200,result.body+result.transportError);
    std::map<std::string,std::string> fields;std::istringstream in(result.body);std::string line;
    while(std::getline(in,line)){auto eq=line.find('=');fields[line.substr(0,eq)]=line.substr(eq+1);}
    return fields;
}

int main(int argc,char** argv) {
    try {
        check(argc==3,"Usage: workshop-client-smoke <loopback-base-url> <private-fixture-root>");
        const fs::path root=argv[2];
        settings.network.relayUseDevelopmentEndpoint=true;
        settings.network.directDevelopmentEndpoint=argv[1];
        Workshop::testStore=std::make_unique<Workshop::Store>(root/"creator");
        const auto modSource=root/"mod-source", mapSource=root/"map-source";
        std::string atlas(150000,'\0');
        for(size_t i=0;i<atlas.size();++i)atlas[i]=static_cast<char>((i*31+7)%256);
        write(modSource/"mod.ini","[Mod]\nName=Wire test\n");
        write(modSource/"idle.png",atlas);write(modSource/"movement.png",atlas);write(modSource/"empty","");
        auto mod=Workshop::store().capture("mod",Workshop::newID(),"Wire test","Dune2R","",modSource);
        // Canonical display names must agree with the server's UTF-8 validator.
        auto withName=[&](const std::string& name) {
            auto text=mod.manifest;const auto start=text.find("name=")+5,end=text.find('\n',start);
            text.replace(start,end-start,Workshop::hex(name));return text;
        };
        for(const auto& name:std::vector<std::string>{"   ",std::string("\xc0\xaf",2),
                std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),
                std::string("\x80",1),std::string("\xe2\x82",2)}) {
            bool refused=false;
            try {Workshop::parseManifest(withName(name));}catch(const std::exception&){refused=true;}
            check(refused,"Client accepted a name rejected by the server");
        }
        check(Workshop::parseManifest(withName(u8"Dunes \u00e9 \U0001f30d")).name==u8"Dunes \u00e9 \U0001f30d",
              "Valid Unicode display name was refused");
        // Resume a server-side upload whose first two chunks succeeded before interruption.
        auto receipt=post("begin","hash="+mod.hash+"&manifest="+Workshop::hex(mod.manifest)+"&owner="+Workshop::store().owner());
        auto hash=Workshop::hashBytes(atlas);
        for(size_t offset:{size_t(0),size_t(65536)})
            post("chunk","upload="+receipt.at("upload")+"&file="+hash+"&offset="+std::to_string(offset)
                 +"&data="+Workshop::hex(atlas.substr(offset,65536)));
        Workshop::Client client;client.publish(mod);finish(client);
        check(client.result().hash==mod.hash&&client.result().version==1,"Wrong published mod revision");
        client.publish(mod);finish(client);check(client.result().version==1,"Unchanged share incremented version");
        write(mapSource/"map.ini","[BASIC]\nVersion=2\nAuthor=Smoke\n");
        const auto mapID=Workshop::newID();
        auto map=Workshop::store().capture("map",mapID,"Wire map","",mod.hash,mapSource);
        client.publish(map);finish(client);check(client.result().version==1,"Wrong map version");
        write(mapSource/"map.ini","[BASIC]\nVersion=2\nAuthor=Smoke\nChanged=yes\n");
        auto map2=Workshop::store().capture("map",mapID,"Wire map","",mod.hash,mapSource);
        client.publish(map2);finish(client);check(client.result().version==2,"Changed share did not allocate version2");
        // Independent profile starts with no files and must fetch the complete exact dependency.
        // The first manifest request is answered with HTTP 429 and retried by the client.
        Workshop::testStore=std::make_unique<Workshop::Store>(root/"recipient");
        client.download(map2.hash);finish(client);
        auto downloaded=Workshop::store().get(map2.hash);
        auto downloadedMod=Workshop::store().get(mod.hash);
        check(downloaded.version==2&&downloaded.modHash==mod.hash,"Wrong received map dependency/version");
        check(downloadedMod.hash==mod.hash,"Missing received dependency");
        // Corrupt local content must be recoverable through the same download action.
        write(fs::path(downloaded.directory)/"map.ini","corrupt");
        client.download(map2.hash);finish(client);
        check(Workshop::store().get(map2.hash).hash==map2.hash,"Corrupt cache was not repaired");
        // Public exact snapshots may be re-shared without stealing ownership.
        client.publish(Workshop::store().get(map2.hash));finish(client);check(client.result().version==2,"Mirror changed shared numbering");
        write(mapSource/"map.ini","[BASIC]\nVersion=2\nAuthor=Someone else\n");
        auto changed=Workshop::store().capture("map",mapID,"Wire map","",mod.hash,mapSource);
        client.publish(changed);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(client.status()==Workshop::Client::Status::Busy&&std::chrono::steady_clock::now()<deadline){client.update();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        check(client.status()==Workshop::Client::Status::Failed,"Borrower changed the original author's lineage");
        std::cout<<"PASS: real client/server resume, duplicate large assets, dependency closure, versions, cache repair, rate-limit retry and owner rejection\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
