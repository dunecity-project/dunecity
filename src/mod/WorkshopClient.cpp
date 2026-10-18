#include <mod/WorkshopClient.h>
#include <Network/BoundedHttpClient.h>
#include <Network/RelayWebSocket.h>
#include <globals.h>
#include <misc/WebRuntime.h>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace Workshop {
namespace fs = std::filesystem;
namespace {
using Fields=std::map<std::string,std::string>;
Fields fields(const std::string& body) {
    Fields out;std::istringstream in(body);std::string line;
    while(std::getline(in,line)) {
        const auto at=line.find('=');if(at==std::string::npos) throw std::runtime_error("The metaserver sent an invalid response.");
        if(!out.emplace(line.substr(0,at),line.substr(at+1)).second)throw std::runtime_error("The metaserver repeated a response field.");
    }
    return out;
}
unsigned integer(const std::string& s) {
    if(s.empty()||s.size()>9||s.find_first_not_of("0123456789")!=std::string::npos)
        throw std::runtime_error("The metaserver sent an invalid version.");
    return static_cast<unsigned>(std::stoul(s));
}
bool digest(const std::string& s) { return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos; }
}
class Client::Impl {
public:
    enum class Phase { None, Begin, Chunk, Commit, Manifest, Blob };
    std::unique_ptr<BoundedHttpClient> http=createBoundedHttpClient();
    Status status=Status::Idle;
    Phase phase=Phase::None;
    std::string message,upload,wanted;
    Revision current,result;
    std::deque<Revision> publishQueue;
    std::deque<std::string> downloadQueue;
    fs::path stage;
    size_t fileIndex=0;
    uint64_t offset=0,lastChunk=0;
    unsigned retries=0;
    Uint32 retryAt=0;
    BoundedHttpClient::Request lastRequest;
    ~Impl(){cancel();}
    void cancel(){http->cancel();if(!stage.empty()){std::error_code e;fs::remove_all(stage,e);stage.clear();}status=Status::Idle;}
    void reset(){cancel();retries=0;retryAt=0;publishQueue.clear();downloadQueue.clear();result={};current={};message.clear();}
    void send(Phase p,const std::string& route,const std::string& body) {
        phase=p;BoundedHttpClient::Request request;
        auto base=settings.network.activeDirectEndpoint();
        std::string mapped,error;
        if(base.rfind("https://",0)==0)mapped="wss://"+base.substr(8);
        else if(base.rfind("http://",0)==0)mapped="ws://"+base.substr(7);
        if(!mapped.empty()&&mapped.back()=='/')mapped.pop_back();
        if(!isAcceptableRelayUrl(mapped,settings.network.relayUseDevelopmentEndpoint,error))
            throw std::runtime_error("The metaserver must use a secure HTTPS address.");
        if(!base.empty()&&base.back()=='/')base.pop_back();
        request.url=base+"/v1/content/"+route;
        request.body=body;request.maxResponseBytes=BoundedHttpClient::kMaxResponseBytes;request.timeoutSeconds=60;
        lastRequest=request;http->begin(request);status=Status::Busy;
    }
    void beginPublish() {
        current=publishQueue.front();publishQueue.pop_front();fileIndex=0;offset=0;
        message="Sharing "+current.name+" (version "+std::to_string(current.version)+") for this online game...";
        // A host upload: the server records it as an unpromoted, host-sourced revision.
        send(Phase::Begin,"begin","hash="+current.hash+"&manifest="+hex(current.manifest)+"&owner="+store().owner()
             +"&source=host&promoted=0");
    }
    void published(unsigned version) {
        if(!version)throw std::runtime_error("The metaserver did not assign a version.");
        store().setSharedVersion(current.hash,version);current.version=version;result=current;
        if(!publishQueue.empty())beginPublish();
        else {status=Status::Succeeded;message="Shared "+result.name+" (version "+std::to_string(result.version)+")"+".";WebRuntime::syncPersistentFiles();}
    }
    void sendChunk() {
        if(fileIndex==current.files.size()){send(Phase::Commit,"commit","upload="+upload);return;}
        const auto& f=current.files[fileIndex];
        const auto n=static_cast<size_t>(std::min<uint64_t>(65536,f.size-offset));
        std::ifstream in(fs::path(current.directory)/f.path,std::ios::binary);in.seekg(offset);
        std::string data(n,'\0');in.read(data.data(),n);
        if(!in || static_cast<size_t>(in.gcount())!=n)throw std::runtime_error("The saved content could not be read.");
        lastChunk=n;
        message="Saving to metaserver: "+current.name+": file "+std::to_string(fileIndex+1)+"/"+std::to_string(current.files.size())
               +" ("+std::to_string(offset/1024)+"/"+std::to_string(f.size/1024)+" KB)";
        send(Phase::Chunk,"chunk","upload="+upload+"&file="+f.hash+"&offset="+std::to_string(offset)+"&data="+hex(data));
    }
    void beginDownload() {
        auto hash=downloadQueue.front();downloadQueue.pop_front();current={};current.hash=hash;
        try {
            current=store().get(hash);
            downloaded();return;
        } catch(const std::exception&) { /* A missing or corrupt cache is downloaded again. */ }
        message="Finding the selected version...";
        send(Phase::Manifest,"manifest","hash="+hash);
    }
    void receiveBlob() {
        if(fileIndex==current.files.size()) {
            auto imported=store().importRevision(current.manifest,current.hash,current.version,stage);
            current=std::move(imported);fs::remove_all(stage);stage.clear();downloaded();return;
        }
        const auto& f=current.files[fileIndex];
        if(f.size==0){fs::create_directories((stage/f.path).parent_path());std::ofstream(stage/f.path,std::ios::binary);++fileIndex;receiveBlob();return;}
        const auto count=std::min<uint64_t>(65536,f.size-offset);
        message="Downloading "+current.name+": file "+std::to_string(fileIndex+1)+"/"+std::to_string(current.files.size());
        lastChunk=count;
        send(Phase::Blob,"blob","hash="+current.hash+"&file="+f.hash+"&offset="+std::to_string(offset)+"&count="+std::to_string(count));
    }
    void downloaded() {
        if(current.hash==wanted)result=current;
        if(!current.modHash.empty()) {
            try {store().get(current.modHash);}catch(...) {downloadQueue.push_back(current.modHash);}
        }
        if(!downloadQueue.empty())beginDownload();
        else{status=Status::Succeeded;message="Downloaded and verified "+result.name+" (version "+std::to_string(result.version)+")"+".";WebRuntime::syncPersistentFiles();}
    }
    void update() {
        if(status!=Status::Busy)return;
        if(retryAt) {
            if(static_cast<Sint32>(SDL_GetTicks()-retryAt)<0)return;
            retryAt=0;http->begin(lastRequest);
        }
        http->update();BoundedHttpClient::Result response;if(!http->poll(response))return;
        if(response.httpStatus==429 && retries++<3) {
            retryAt=SDL_GetTicks()+61000;
            message="The metaserver is busy. Resuming this transfer in a minute...";
            return;
        }
        retries=0;
        try {
            if(!response.transportError.empty())throw std::runtime_error("Could not reach the metaserver. Saved versions are still available locally.");
            auto f=fields(response.body);
            if(response.httpStatus!=200||f["status"]!="ok") {
                std::string error="The metaserver could not complete this request.";
                if(f.count("message")){try{error=unhex(f["message"]);}catch(...) {}}
                throw std::runtime_error(error);
            }
            switch(phase) {
            case Phase::Begin:
                if(f.count("version")){if(f["hash"]!=current.hash)throw std::runtime_error("Server revision mismatch.");published(integer(f["version"]));}
                else{upload=f["upload"];if(!digest(upload))throw std::runtime_error("Invalid upload receipt.");sendChunk();}break;
            case Phase::Chunk:
                { const auto accepted = integer(f["next"]);
                  if(accepted < offset+lastChunk || accepted > current.files[fileIndex].size)
                      throw std::runtime_error("Upload was not acknowledged completely.");
                  offset=accepted;if(offset==current.files[fileIndex].size){++fileIndex;offset=0;}sendChunk();break; }
            case Phase::Commit:
                if(f["hash"]!=current.hash)throw std::runtime_error("Server revision mismatch.");published(integer(f["version"]));break;
            case Phase::Manifest: {
                auto r=parseManifest(unhex(f["manifest"]));
                if(r.hash!=current.hash)throw std::runtime_error("Downloaded manifest does not match the requested version.");
                r.version=integer(f["version"]);if(!r.version)throw std::runtime_error("Invalid downloaded version.");
                current=std::move(r);stage=store().root()/(".download-"+newID());fs::create_directories(stage);
                fileIndex=0;offset=0;receiveBlob();break;
            }
            case Phase::Blob: {
                auto data=unhex(f["data"]);if(data.size()!=lastChunk)throw std::runtime_error("Incomplete content download.");
                const auto path=stage/current.files[fileIndex].path;fs::create_directories(path.parent_path());
                std::ofstream out(path,std::ios::binary|std::ios::app);out.write(data.data(),data.size());out.close();
                if(!out)throw std::runtime_error("Could not save downloaded content. Check free disk space.");
                offset+=data.size();if(offset==current.files[fileIndex].size){++fileIndex;offset=0;}receiveBlob();break;
            }
            default:break;
            }
        } catch(const std::exception& e){http->cancel();status=Status::Failed;message=e.what();}
    }
};
Client::Client():impl_(std::make_unique<Impl>()){}
Client::~Client()=default;
void Client::publish(const Revision& revision){impl_->reset();
    try{auto r=store().get(revision.hash);if(!r.modHash.empty())impl_->publishQueue.push_back(store().get(r.modHash));impl_->publishQueue.push_back(r);impl_->beginPublish();}
    catch(const std::exception& e){impl_->status=Status::Failed;impl_->message=e.what();}}
void Client::download(const std::string& hash){impl_->reset();impl_->wanted=hash;
    if(!digest(hash)){impl_->status=Status::Failed;impl_->message="Invalid content checksum.";return;}
    try{impl_->downloadQueue.push_back(hash);impl_->beginDownload();}
    catch(const std::exception& e){impl_->status=Status::Failed;impl_->message=e.what();}}
void Client::update(){impl_->update();}
void Client::cancel(){impl_->cancel();}
Client::Status Client::status()const{return impl_->status;}
const std::string& Client::message()const{return impl_->message;}
const Revision& Client::result()const{return impl_->result;}

}
