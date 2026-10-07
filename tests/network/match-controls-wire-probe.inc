#include <Network/GamePayloadRouter.h>
#include <Definitions.h>
// Linked to the production payload decoder by the real-engine probe.
static void probeMatchControlWire() {
    struct Peer final : GamePayloadPeer {
        int refusals=0; std::string value="bound-player";
        Uint32 clientId() const override {return 2;}
        const std::string& name() const override {return value;}
        bool nameAssigned() const override {return true;}
        bool bindName(const std::string&) override {return false;}
        bool isHostPeer() const override {return true;}
        void refuse(const char*) override {++refusals;}
        void replyConfigHash() override {}
        std::string& gameVersion() override {return value;}
        std::string& quantBotConfigHash() override {return value;}
        std::string& objectDataHash() override {return value;}
        std::string& modRevisionHash() override {return value;}
        void disconnectWithCause(int) override {++refusals;}
    } peer;
    int controls=0,requests=0;Uint32 expectedSpeed=GAMESPEED_DEFAULT;
    const std::function<void(Uint32,Uint32,Uint32,Uint32)> control=[&](Uint32 rev,Uint32 speed,Uint32 pause,Uint32 resumed){
        if(rev!=1 || speed!=expectedSpeed || pause!=111 || resumed!=111) throw std::runtime_error("control fields corrupted");
        ++controls;
    };
    const std::function<void(const std::string&,Uint32)> request=[&](const std::string& name,Uint32 pause){
        if(name!="bound-player" || pause!=111) throw std::runtime_error("resume identity was not bound to connection");
        ++requests;
    };
    GamePayloadContext context;context.inGame=true;context.simulationSeed=42;
    NetworkSessionCallbacks callbacks;callbacks.onReceiveMatchControl=&control;callbacks.onReceiveMatchResumeRequest=&request;
    const auto send=[&](Uint32 type,std::initializer_list<Uint32> fields,bool good,bool old=false){
        OMemoryStream out;out.open();for(auto field:fields) out.writeUint32(field);
        IMemoryStream in(out.getData(),out.getDataLength());
        const int before=controls+requests,refused=peer.refusals;
        bool truncated=false;
        try {GamePayloadRouter::handle(type,in,peer,context,callbacks);} catch(const std::exception&) {truncated=true;}
        if((controls+requests==before+1)!=good || (good && truncated)
            || (!good && !old && peer.refusals==refused && !truncated))
            throw std::runtime_error("match control wire validation failed");
    };
    send(NETWORKPACKET_MATCH_CONTROL,{42,1,16,111,111},true);
    send(NETWORKPACKET_MATCH_CONTROL,{41,1,16,111,111},false,true);
    send(NETWORKPACKET_MATCH_CONTROL,{42,0,16,111,111},false);
    // The two fastest settings are real shared pacing and travel over this packet; only a
    // value below GAMESPEED_MIN is refused. Protocol 56 introduced that gate and the shipped
    // version has advanced since, so peers below NETWORK_PROTOCOL_VERSION never see these values.
    expectedSpeed=3;send(NETWORKPACKET_MATCH_CONTROL,{42,1,3,111,111},true);
    expectedSpeed=GAMESPEED_MIN;send(NETWORKPACKET_MATCH_CONTROL,{42,1,GAMESPEED_MIN,111,111},true);
    expectedSpeed=GAMESPEED_DEFAULT;
    send(NETWORKPACKET_MATCH_CONTROL,{42,1,GAMESPEED_MIN-1,111,111},false);
    send(NETWORKPACKET_MATCH_CONTROL,{42,1,0,111,111},false);
    send(NETWORKPACKET_MATCH_CONTROL,{42,1,33,111,111},false);
    send(NETWORKPACKET_MATCH_CONTROL,{42,1,16,111,112},false);
    send(NETWORKPACKET_MATCH_CONTROL,{42,1,16,111,111,0},false);
    send(NETWORKPACKET_MATCH_CONTROL,{42,1,16},false);
    send(NETWORKPACKET_MATCH_RESUME_REQUEST,{42,111},true);
    send(NETWORKPACKET_MATCH_RESUME_REQUEST,{41,111},false,true);
    send(NETWORKPACKET_MATCH_RESUME_REQUEST,{42,0},false);
    send(NETWORKPACKET_MATCH_RESUME_REQUEST,{42,111,0},false);
    send(NETWORKPACKET_MATCH_RESUME_REQUEST,{42},false);
}
