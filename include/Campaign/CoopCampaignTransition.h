#ifndef COOP_CAMPAIGN_TRANSITION_H
#define COOP_CAMPAIGN_TRANSITION_H

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class InputStream;
class OutputStream;

namespace coop {
struct Context;

enum class Outcome : unsigned char { Aborted = 0, Lost = 1, Won = 2 };
enum class Advance : unsigned char { Aborted = 0, Retry = 1, Next = 2, Complete = 3 };
enum class ControlOperation : unsigned char { Ready = 1, Advance = 2 };

// playerName is bound to the authenticated connection, never serialized.
struct ControlMessage {
    ControlOperation operation = ControlOperation::Ready;
    std::uint32_t epoch = 0;
    std::string sessionId;
    int stage = 0;
    unsigned char value = 0;
    std::string settingsBlob;
    std::string playerName;
};

void writeControl(OutputStream& stream, const ControlMessage& message);
ControlMessage readControl(InputStream& stream);
bool validControl(const ControlMessage& message);
bool sameCampaignContext(const Context& first, const Context& second);

// All controllers, not just one partner, must report the same completed mission.
// Conflicting duplicate reports latch an abort instead of advancing progression.
class MissionBarrier {
public:
    MissionBarrier(std::string session, int stage, std::string partner)
        : MissionBarrier(std::move(session), stage,
              partner.empty() ? std::vector<std::string>{} : std::vector<std::string>{std::move(partner)}) {}
    MissionBarrier(std::string session, int stage, const std::vector<std::string>& partners);
    bool receive(const std::string& partner, const std::string& session, int stage, Outcome outcome);
    std::optional<Advance> decision(Outcome local, int finalStage) const;
    bool accepts(Advance advance, Outcome local, int finalStage) const;

private:
    std::string session_;
    int stage_;
    std::map<std::string, std::optional<Outcome>> remote_;
};

} // namespace coop
#endif
