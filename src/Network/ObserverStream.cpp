#include <Network/NetworkManager.h>
#include <Network/GameInitSettingsPolicy.h>
#include <Network/ObserverStreamPolicy.h>
#include <misc/OMemoryStream.h>
#include <misc/IMemoryStream.h>
#include <algorithm>

namespace {
// The 4 MiB save is supplemented by AI/path runtime state. A 256x256 Twin
// Cities checkpoint already exceeds 5 MiB with two AIs. Keep a bounded 8 MiB
// envelope on both endpoints; ordinary network-save and chunk limits stay fixed.
constexpr Uint32 maxSnapshot=SnapshotTransfer::maxBytes;
constexpr Uint32 chunkBytes=ObserverStreamPolicy::chunkBytes;
// Kept cycles bound how far behind a spectator can be and still be served: when the host's game
// ends, a spectator that is behind receives everything it has not had yet (endObserverStreams).
// 8,192 cycles are over two minutes at the default speed; the byte bound holds either way.
constexpr Uint32 maxHistoryCycles=8192;
// What the end of a stream may send per update, in batches of at most one chunk.
constexpr std::size_t endBytesPerUpdate=256u*1024;
constexpr std::size_t maxHistoryBytes=4u*1024*1024;
}

// These operations use the existing shared, authorized JOIN_SYNC/JOIN_ACK parser.
// Unlike player admission, no observer operation changes the host's join stage.
bool NetworkManager::sendObserverPacket(Uint32 peer, Uint32 op, Uint32 epoch, Uint32 offset, const std::string& bytes) {
    NetworkPacketOStream packet(NETWORK_PACKET_FLAG_RELIABLE);
    packet.writeUint32(bIsServer ? NETWORKPACKET_JOIN_SYNC : NETWORKPACKET_JOIN_ACK);
    packet.writeUint32(op); packet.writeUint32(epoch); packet.writeUint32(offset); packet.writeString(bytes);
    return sendPacketOverRelay(packet,0,peer);
}

std::vector<Uint32> NetworkManager::observersNeedingSnapshot() const {
    std::vector<Uint32> result;
    const auto* direct=getDirectTransport();
    if(!direct || !bIsServer || !bGameInProgress || lateJoinPaused()) return result;
    for(const auto& peer : direct->peers())
        if(peer.spectator && peer.name!=joinName && direct->peerConnected(peer.id) && !observerTransfers.count(peer.id)) result.push_back(peer.id);
    return result;
}

bool NetworkManager::beginObserverSnapshot(Uint32 peer, const GameInitSettings& snapshot, const std::string& runtime, Uint32 cycle) {
    auto* direct=getDirectTransport();
    if(!direct || !bIsServer || !direct->isSpectatorPeer(peer) || observerTransfers.count(peer)) return false;
    std::string error;
    if(!GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(snapshot,error)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"Spectator checkpoint refused: %s",error.c_str());
        return false;
    }
    OMemoryStream out; out.open(); snapshot.save(out); out.writeString(runtime); out.writeUint32(cycle);
    if(out.getDataLength()>maxSnapshot) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"Spectator checkpoint refused: envelope %u exceeds %u bytes",
            static_cast<unsigned>(out.getDataLength()),maxSnapshot);
        return false;
    }
    ObserverTransfer transfer;
    if(!transfer.checkpoint.prepare(std::string(out.getData(),out.getDataLength()))) return false;
    transfer.epoch=simulationSeed+(++observerSnapshotSerial);
    if(transfer.epoch==0) transfer.epoch=++observerSnapshotSerial;
    transfer.nextCycle=transfer.ackCycle=cycle;
    transfer.deadline=SDL_GetTicks()+120000;
    SDL_Log("Spectator checkpoint sending: peer=%u bytes=%u raw=%u cycle=%u",peer,
        static_cast<unsigned>(transfer.checkpoint.bytes.size()),static_cast<unsigned>(out.getDataLength()),cycle);
    observerTransfers.emplace(peer,std::move(transfer));
    return true;
}

void NetworkManager::publishObserverCycle(Uint32 cycle, const std::string& bytes) {
    if(!bIsServer || observerTransfers.empty()) return;
    if(bytes.size()>chunkBytes) {
        auto* direct=getDirectTransport();
        std::vector<Uint32> ids; for(const auto& item : observerTransfers) ids.push_back(item.first);
        observerTransfers.clear();
        for(auto id : ids) direct->disconnectSpectator(id,"This game update is too large to spectate.");
        return;
    }
    observerHistory.emplace_back(cycle,bytes); observerHistoryBytes+=bytes.size();
    while(observerHistory.size()>maxHistoryCycles || observerHistoryBytes>maxHistoryBytes) {
        observerHistoryBytes-=observerHistory.front().second.size(); observerHistory.pop_front();
    }
}

void NetworkManager::updateObservers() {
    auto* direct=getDirectTransport();
    if(observerResyncPending && SDL_TICKS_PASSED(SDL_GetTicks(),observerResyncDeadline)) {
        failObserver("The host did not send a fresh game state in time.\nPlease try joining again.");
        if(pOnPeerDisconnected) pOnPeerDisconnected({},true,NETWORKDISCONNECT_TIMEOUT);
        return;
    }
    if(!direct || !bIsServer || lateJoinPaused()) return;
    std::vector<Uint32> dropped, restart;
    // A bounded per-update allowance across all viewers. Player traffic uses different
    // connections and is never included in these acknowledgements or deadlines.
    unsigned sends=0; std::size_t bytesSent=0;
    std::vector<Uint32> order; for(const auto& item : observerTransfers) order.push_back(item.first);
    if(!order.empty()) {
        const auto first=std::upper_bound(order.begin(),order.end(),observerSendCursor);
        std::rotate(order.begin(),first,order.end());
    }
    for(auto peer : order) {
        auto& t=observerTransfers.at(peer);
        if(!direct->isSpectatorPeer(peer)) { dropped.push_back(peer); continue; }
        if(observerEndCycle && t.ready) {
            // The host's game is over: no more cycles come, so the replay window and the expiry
            // no longer apply. Send the rest in batches, then the end marker.
            if(t.endSent) continue;
            std::size_t budget=endBytesPerUpdate;
            bool failed=false;
            while(t.nextCycle<observerEndCycle && budget>0) {
                if(observerHistory.empty() || t.nextCycle<observerHistory.front().first
                   || t.nextCycle>observerHistory.back().first) { failed=true; break; }
                OMemoryStream batch; batch.open();
                const Uint32 first=t.nextCycle;
                for(auto it=observerHistory.begin()+(t.nextCycle-observerHistory.front().first);
                    it!=observerHistory.end() && t.nextCycle<observerEndCycle; ++it) {
                    if(it->first!=t.nextCycle) { failed=true; break; }
                    if(batch.getDataLength()+4+it->second.size()>chunkBytes) break;
                    batch.writeString(it->second); ++t.nextCycle;
                }
                if(failed || t.nextCycle==first
                   || !sendObserverPacket(peer,18,t.epoch,first,std::string(batch.getData(),batch.getDataLength()))) { failed=true; break; }
                budget-=std::min<std::size_t>(budget,batch.getDataLength()+24);
            }
            if(failed) { dropped.push_back(peer); continue; }
            if(t.nextCycle==observerEndCycle) {
                if(!sendObserverPacket(peer,19,t.epoch,observerEndCycle,{})) { dropped.push_back(peer); continue; }
                t.endSent=true;
                SDL_Log("Spectator stream ended: peer=%u end=%u acknowledged=%u",peer,observerEndCycle,t.ackCycle);
            }
            continue;
        }
        const bool idle=t.ready && t.nextCycle==t.ackCycle && (observerHistory.empty() || t.nextCycle>observerHistory.back().first);
        if(idle) t.deadline=SDL_GetTicks()+30000;
        if(SDL_TICKS_PASSED(SDL_GetTicks(),t.deadline)
           || (!observerHistory.empty() && t.nextCycle<observerHistory.front().first)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Spectator stream expired: peer=%u acknowledged=%u sent=%u total=%u ready=%d cycle=%u oldest=%u timeout=%d",
                peer,t.offset,t.sent,static_cast<unsigned>(t.checkpoint.bytes.size()),t.ready,t.nextCycle,
                observerHistory.empty() ? t.nextCycle : observerHistory.front().first,
                SDL_TICKS_PASSED(SDL_GetTicks(),t.deadline));
            if(observerRestarts[peer]<ObserverStreamPolicy::maxRestarts) restart.push_back(peer);
            else dropped.push_back(peer);
            continue;
        }
        if(sends>=8 || bytesSent>=64u*1024) continue;
        observerSendCursor=peer;
        if(!t.began) {
            t.began=true; t.sent=~Uint32(0);
            const auto header=t.checkpoint.header();
            if(!sendObserverPacket(peer,10,t.epoch,t.checkpoint.bytes.size(),header)) dropped.push_back(peer);
            ++sends; bytesSent+=24+header.size(); continue;
        }
        if(t.sent==~Uint32(0)) continue; // Wait for the header acknowledgement.
        if(ObserverStreamPolicy::canSendChunk(t.offset,t.sent,t.checkpoint.bytes.size())) {
            const auto bytes=t.checkpoint.bytes.substr(t.sent,chunkBytes);
            if(bytesSent+bytes.size()+24>64u*1024) continue;
            const auto offset=t.sent;
            t.sent+=bytes.size();
            if(!sendObserverPacket(peer,11,t.epoch,offset,bytes)) dropped.push_back(peer);
            ++sends; bytesSent+=bytes.size()+24; continue;
        }
        if(!t.ready) continue;
        if(SDL_TICKS_PASSED(SDL_GetTicks(),t.progressAt)
           && sends<8 && bytesSent+24<=64u*1024) {
            if(!sendObserverPacket(peer,16,t.epoch,observerHistory.empty() ? t.nextCycle : observerHistory.back().first+1,{})) { dropped.push_back(peer); continue; }
            t.progressAt=SDL_GetTicks()+250; ++sends; bytesSent+=24;
        }
        if(t.nextCycle-t.ackCycle>=ObserverStreamPolicy::replayWindow) continue;
        for(const auto& frame : observerHistory) {
            if(frame.first<t.nextCycle) continue;
            if(sends>=8 || bytesSent+frame.second.size()+24>64u*1024 || t.nextCycle-t.ackCycle>=ObserverStreamPolicy::replayWindow) break;
            if(frame.first!=t.nextCycle || !sendObserverPacket(peer,12,t.epoch,frame.first,frame.second)) {
                dropped.push_back(peer); break;
            }
            ++t.nextCycle; ++sends; bytesSent+=frame.second.size()+24;
        }
    }
    for(const auto peer : restart) {
        ++observerRestarts[peer];
        observerTransfers.erase(peer);
        SDL_Log("Spectator transfer retry: peer=%u attempt=%u",peer,observerRestarts[peer]);
    }
    for(const auto peer : dropped) {
        observerTransfers.erase(peer);
        direct->disconnectSpectator(peer,"The spectator connection could not keep up. Please spectate again.");
    }
    if(observerTransfers.empty()) { observerHistory.clear(); observerHistoryBytes=0; }
    for(auto it=observerRestarts.begin();it!=observerRestarts.end();) {
        if(!direct->isSpectatorPeer(it->first)) it=observerRestarts.erase(it); else ++it;
    }
}

void NetworkManager::receiveObserverPacket(Uint32 peer, Uint32 op, Uint32 epoch, Uint32 offset, const std::string& bytes) {
    auto* direct=getDirectTransport(); if(!direct) return;
    if(bIsServer) {
        const auto found=observerTransfers.find(peer);
        if(!direct->isSpectatorPeer(peer) || found==observerTransfers.end() || found->second.epoch!=epoch || !bytes.empty()) return;
        auto& t=found->second;
        if(op==17 && t.ready) {
            if(observerRestarts[peer]>=ObserverStreamPolicy::maxRestarts) {
                observerTransfers.erase(peer);
                direct->disconnectSpectator(peer,"Could not synchronize after two retries. Please rejoin the game.");
            } else {
                ++observerRestarts[peer]; observerTransfers.erase(peer);
                SDL_Log("Spectator requested fresh checkpoint: peer=%u cycle=%u attempt=%u",peer,offset,observerRestarts[peer]);
            }
            return;
        }
        if(op==10 && t.began && t.sent==~Uint32(0)) {
            const auto ack=t.checkpoint.acknowledge(offset);
            if(ack==SnapshotTransfer::Sender::Ack::Invalid) return;
            if(ack==SnapshotTransfer::Sender::Ack::RetryRaw) {
                t.began=false;
                SDL_Log("Spectator checkpoint legacy fallback: peer=%u bytes=%u",peer,
                    static_cast<unsigned>(t.checkpoint.bytes.size()));
            } else t.sent=t.offset=0;
        }
        else if(op==11 && ObserverStreamPolicy::validChunkAck(t.offset,t.sent,t.checkpoint.bytes.size(),offset)) t.offset=offset;
        else if(op==13 && !t.ready && t.offset==t.checkpoint.bytes.size() && offset==t.nextCycle) {
            t.ready=true;
            SDL_Log("Spectator checkpoint loaded: peer=%u bytes=%u cycle=%u",peer,t.offset,offset);
        }
        else if(op==14 && t.ready && offset>t.ackCycle && offset<=t.nextCycle) t.ackCycle=offset;
        else if(op==19 && t.endSent && offset==observerEndCycle) t.endAcked=true;
        else return;
        t.deadline=SDL_GetTicks()+30000;
        return;
    }
    if(peer!=relayHostPeerId()) return;
    if(op==15) {
        try {
            IMemoryStream in(bytes.data(),bytes.size());
            const auto name=in.readString(), message=in.readString();
            if(in.getRemainingLength()!=0 || !RoomRelay::isAcceptableDisplayName(name) || message.size()>512) return;
            if(pOnReceiveChatMessage) pOnReceiveChatMessage(name,message);
        } catch(const std::exception&) { }
        return;
    }
    if(!direct->isSpectating()) return;
    const auto fail=[&]() { failObserver("The game transfer could not be read. Please try joining again."); if(pOnPeerDisconnected) pOnPeerDisconnected({},true,NETWORKDISCONNECT_TIMEOUT); };
    if(op==10) {
        if(!SnapshotTransfer::readHeader(bytes,offset,observerDecodedSize)) { fail(); return; }
        observerEpoch=epoch; observerTotal=offset; observerBytes.clear(); observerIncoming.clear();
        joinSnapshot.reset(); observerRuntime.clear(); observerNextCycle=0;
        joinStage=JoinStage::Receiving; joinStatus="Downloading game state";
        observerCatchup=false; observerResyncPending=false; observerStartCycle=observerHostCycle=observerAppliedCycle=0;
        observerEndCycle=0;
        joinFailureReason.clear();
        SDL_Log("Spectator checkpoint receiving: bytes=%u decoded=%u epoch=%u",offset,observerDecodedSize ? observerDecodedSize : offset,epoch);
        bGameInProgress=false;
        sendObserverPacket(peer,10,epoch,observerDecodedSize ? SnapshotTransfer::compressedAck : 0,{}); return;
    }
    if(epoch!=observerEpoch) return;
    if(op==11) {
        if(joinStage!=JoinStage::Receiving || offset!=observerBytes.size() || bytes.empty()
           || bytes.size()>chunkBytes || bytes.size()>observerTotal-observerBytes.size()) { fail(); return; }
        observerBytes+=bytes;
        if(observerBytes.size()==observerTotal) {
            try {
                std::string decoded;
                if(!SnapshotTransfer::decode(observerBytes,observerDecodedSize,decoded)) throw std::runtime_error("Invalid compressed checkpoint");
                IMemoryStream in(decoded.data(),decoded.size());
                auto next=std::make_unique<GameInitSettings>(in);
                auto runtime=in.readString(); observerNextCycle=in.readUint32();
                std::string error;
                if(in.getRemainingLength()!=0 || !GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(*next,error)
                   || (next->getGameType()!=GameType::LoadMultiplayer && next->getGameType()!=GameType::LoadCoop)) throw std::runtime_error("Invalid checkpoint");
                joinSnapshot=std::move(next); observerRuntime=std::move(runtime);
                joinSpectators={playerName}; joinTransaction=epoch; joinStage=JoinStage::Ready;
            } catch(const std::exception&) { fail(); return; }
        }
        sendObserverPacket(peer,11,epoch,observerBytes.size(),{}); return;
    }
    if(op==16) {
        if(!bytes.empty() || offset<observerHostCycle) { fail(); return; }
        observerHostCycle=offset;
        observerAdvanced(observerAppliedCycle);
        return;
    }
    if(op==12) {
        if(observerResyncPending) return; // Drain old reliable ticks until the new epoch header.
        if(!bGameInProgress || offset!=observerNextCycle || bytes.empty() || bytes.size()>chunkBytes || observerIncoming.size()>=ObserverStreamPolicy::replayWindow) { fail(); return; }
        observerIncoming.emplace_back(offset,bytes); ++observerNextCycle;
        return;
    }
    if(op==18) {
        // The rest of an ended game, past the replay window (endObserverStreams).
        if(observerResyncPending) return;
        if(!bGameInProgress || offset!=observerNextCycle || bytes.empty()) { fail(); return; }
        try {
            IMemoryStream in(bytes.data(),bytes.size());
            while(in.getRemainingLength()!=0) {
                auto frame=in.readString();
                if(frame.empty() || frame.size()>chunkBytes || observerIncoming.size()>=maxHistoryCycles)
                    throw std::runtime_error("Invalid spectator batch");
                observerIncoming.emplace_back(observerNextCycle++,std::move(frame));
            }
        } catch(const std::exception&) { fail(); return; }
        return;
    }
    if(op==19) {
        if(observerResyncPending) return;
        if(!bytes.empty() || offset!=observerNextCycle) { fail(); return; }
        observerEndCycle=offset;
        observerHostCycle=std::max(observerHostCycle,offset);
        SDL_Log("Spectator stream end received: end=%u applied=%u buffered=%zu",offset,observerAppliedCycle,observerIncoming.size());
        sendObserverPacket(peer,19,epoch,offset,{});
    }
}

void NetworkManager::endObserverStreams(Uint32 endCycle) {
    if(!bIsServer || observerEndCycle) return;
    observerEndCycle=endCycle;
    if(!observerTransfers.empty()) SDL_Log("Spectator streams end at cycle %u (%zu spectators)",endCycle,observerTransfers.size());
}

bool NetworkManager::observerStreamsDelivered() const {
    const auto* direct=getDirectTransport();
    if(!bIsServer || !observerEndCycle || !direct || !pRelayClient || !pRelayClient->isJoined()) return true;
    for(const auto& entry : observerTransfers)
        if(entry.second.ready && !entry.second.endAcked && direct->isSpectatorPeer(entry.first)) return false;
    return true;
}

void NetworkManager::observerLoaded(Uint32 cycle) {
    if(!isSpectating()) return;
    observerBytes.clear(); joinStatus="Synchronizing with the host";
    observerStartCycle=observerAppliedCycle=cycle; observerHostCycle=0; observerCatchup=true;
    SDL_Log("Spectator checkpoint loaded locally: cycle=%u bytes=%u",cycle,observerTotal);
    sendObserverPacket(relayHostPeerId(),13,observerEpoch,cycle,{});
}

bool NetworkManager::takeObserverCycle(Uint32 cycle, std::string& bytes) {
    if(observerIncoming.empty() || observerIncoming.front().first!=cycle) return false;
    bytes=std::move(observerIncoming.front().second); observerIncoming.pop_front();
    sendObserverPacket(relayHostPeerId(),14,observerEpoch,cycle+1,{});
    return true;
}

void NetworkManager::forwardObserverChat(const std::string& sender, const std::string& message) {
    auto* direct=getDirectTransport();
    if(!direct || !bIsServer || !bGameInProgress || message.size()>512) return;
    OMemoryStream out; out.open(); out.writeString(sender); out.writeString(message);
    const std::string bytes(out.getData(),out.getDataLength());
    const bool fromObserver=isSpectator(sender);
    // Copy IDs: a full viewer channel may be removed by sendObserverPacket.
    std::vector<Uint32> recipients;
    for(const auto& peer : direct->peers()) if(peer.name!=sender && (peer.spectator || fromObserver)) recipients.push_back(peer.id);
    for(auto id : recipients) sendObserverPacket(id,15,simulationSeed,0,bytes);
}

void NetworkManager::observerAdvanced(Uint32 cycle) {
    observerAppliedCycle=cycle;
    if(observerResyncPending) return;
    if(observerCatchup && observerHostCycle && cycle+16>=observerHostCycle) {
        observerCatchup=false; joinStatus="Connected — spectating";
        SDL_Log("Spectator synchronized: cycle=%u host=%u",cycle,observerHostCycle);
    }
}

void NetworkManager::failObserver(const std::string& reason) {
    joinFailureReason=reason;
    joinStatus=reason;
    observerCatchup=false; observerResyncPending=false;
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"Online join failed: %s",reason.c_str());
    disconnect();
}

int NetworkManager::lateJoinPercent() const {
    if(observerResyncPending) return 0;
    if(observerCatchup) {
        if(observerHostCycle<=observerStartCycle) return 0;
        const auto done=observerAppliedCycle>observerStartCycle ? observerAppliedCycle-observerStartCycle : 0;
        return static_cast<int>(std::min<Uint64>(99,100ull*done/(observerHostCycle-observerStartCycle)));
    }
    if(joinStage==JoinStage::Receiving) {
        const auto total=isSpectating() ? observerTotal : joinTotal;
        const auto received=isSpectating() ? observerBytes.size() : joinBytes.size();
        return total ? static_cast<int>(100ull*received/total) : 0;
    }
    if(joinStage==JoinStage::Sending) return joinBytes.empty() ? 0 : static_cast<int>(100ull*joinOffset/joinBytes.size());
    return joinStage==JoinStage::Ready || joinStage==JoinStage::Starting ? 100 : 0;
}

std::string NetworkManager::lateJoinProgressText() const {
    if(observerResyncPending) return "Refreshing game state — retry "+std::to_string(observerResyncAttempts)+" / 2";
    if(observerCatchup) {
        if(!observerHostCycle) return "Waiting for the host's current game state...";
        return "Catching up with the game: "+std::to_string(lateJoinPercent())+"%";
    }
    if(joinStage==JoinStage::Receiving || joinStage==JoinStage::Sending) {
        const auto total=isSpectating() ? observerTotal : (bIsServer ? joinBytes.size() : joinTotal);
        const auto received=isSpectating() ? observerBytes.size() : (bIsServer ? joinOffset : joinBytes.size());
        return std::string(bIsServer ? "Sending game state: " : "Downloading game state: ")+std::to_string(lateJoinPercent())+"% ("
            +std::to_string(received/1024)+" / "+std::to_string(total/1024)+" KiB)";
    }
    if(joinStage==JoinStage::Ready) return "Download complete. Loading map...";
    return joinStatus;
}

bool NetworkManager::requestObserverResync(Uint32 cycle) {
    if(!isSpectating() || observerResyncAttempts>=ObserverStreamPolicy::maxRestarts || observerResyncPending) return false;
    ++observerResyncAttempts;
    observerResyncPending=true; observerResyncDeadline=SDL_GetTicks()+30000;
    observerIncoming.clear(); observerCatchup=true;
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"Spectator refreshing checkpoint: cycle=%u attempt=%u",cycle,observerResyncAttempts);
    return sendObserverPacket(relayHostPeerId(),17,observerEpoch,cycle,{});
}
