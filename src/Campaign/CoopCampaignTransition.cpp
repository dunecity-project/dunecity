#include <Campaign/CoopCampaignTransition.h>
#include <Campaign/CoopCampaignSession.h>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>

namespace coop {

void writeControl(OutputStream& stream, const ControlMessage& message) {
    stream.writeUint8(static_cast<unsigned char>(message.operation));
    stream.writeUint32(message.epoch);
    stream.writeString(message.sessionId);
    stream.writeSint32(message.stage);
    stream.writeUint8(message.value);
    stream.writeString(message.settingsBlob);
}

ControlMessage readControl(InputStream& stream) {
    ControlMessage message;
    message.operation = static_cast<ControlOperation>(stream.readUint8());
    message.epoch = stream.readUint32();
    message.sessionId = stream.readString();
    message.stage = stream.readSint32();
    message.value = stream.readUint8();
    message.settingsBlob = stream.readString();
    return message;
}

bool validControl(const ControlMessage& message) {
    if(message.epoch == 0 || message.sessionId.empty() || message.sessionId.size() > 128
       || message.stage < 1 || message.stage > StageCount
       || message.settingsBlob.size() > 1024u * 1024u) return false;
    if(message.operation == ControlOperation::Ready)
        return message.value <= static_cast<unsigned char>(Outcome::Won) && message.settingsBlob.empty();
    if(message.operation != ControlOperation::Advance
       || message.value > static_cast<unsigned char>(Advance::Complete)) return false;
    const auto action = static_cast<Advance>(message.value);
    return (action == Advance::Next || action == Advance::Retry) != message.settingsBlob.empty();
}

bool sameCampaignContext(const Context& first, const Context& second) {
    if(first.sessionId != second.sessionId || first.modName != second.modName
       || first.campaign != second.campaign || first.seed != second.seed
       || first.mapLayout != second.mapLayout || first.sourceFaction != second.sourceFaction
       || first.enemyPresent != second.enemyPresent || first.slots.size() != second.slots.size()
       || first.roster != second.roster || first.stage != second.stage
       || first.completedMask != second.completedMask || first.chaosEligible != second.chaosEligible
       || first.alliedControl != second.alliedControl || first.alliedBuilding != second.alliedBuilding
       || first.easyMode != second.easyMode || first.extraEnemyForces != second.extraEnemyForces
       || first.extraHardEnemyForces != second.extraHardEnemyForces || first.archon != second.archon
       || first.alliedCapacity != second.alliedCapacity || first.randomVariants != second.randomVariants
       || first.missionVariant != second.missionVariant) return false;
    for(std::size_t i = 0; i < first.slots.size(); ++i) {
        const auto& a = first.slots[i];
        const auto& b = second.slots[i];
        if(a.house != b.house || a.faction != b.faction || a.color != b.color
           || a.playerName != b.playerName || a.playerClass != b.playerClass
           || a.controllers != b.controllers) return false;
    }
    return true;
}

MissionBarrier::MissionBarrier(std::string session, int stage, const std::vector<std::string>& partners)
    : session_(std::move(session)), stage_(stage) {
    for(const auto& partner : partners) remote_.emplace(partner, std::nullopt);
}

bool MissionBarrier::receive(const std::string& partner, const std::string& session,
                            int stage, Outcome outcome) {
    const auto found = remote_.find(partner);
    if(found == remote_.end() || session != session_ || stage != stage_
       || static_cast<unsigned>(outcome) > static_cast<unsigned>(Outcome::Won)) return false;
    if(found->second && *found->second != outcome) found->second = Outcome::Aborted;
    else found->second = outcome;
    return true;
}

std::optional<Advance> MissionBarrier::decision(Outcome local, int finalStage) const {
    if(local == Outcome::Aborted) return Advance::Aborted;
    for(const auto& report : remote_) {
        if(report.second && *report.second != local) return Advance::Aborted;
    }
    for(const auto& report : remote_) if(!report.second) return std::nullopt;
    if(local == Outcome::Lost) return Advance::Retry;
    return stage_ == finalStage ? Advance::Complete : Advance::Next;
}

bool MissionBarrier::accepts(Advance advance, Outcome local, int finalStage) const {
    if(advance == Advance::Aborted) return true;
    if(local == Outcome::Lost) return advance == Advance::Retry;
    if(local != Outcome::Won) return false;
    return advance == (stage_ == finalStage ? Advance::Complete : Advance::Next);
}

} // namespace coop
