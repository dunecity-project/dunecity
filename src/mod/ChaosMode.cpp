// SPDX-License-Identifier: GPL-2.0-or-later
#include <mod/ChaosMode.h>
#include <misc/Random.h>
#include <algorithm>
#include <stdexcept>

void ChaosMode::reset() {
    enabled = false;
    for(int house = 0; house < NUM_HOUSES; ++house) donors[house].fill(house);
}

bool ChaosMode::includesStructure(int item) {
    return std::find(Structures.begin(), Structures.end(), item) != Structures.end();
}

int ChaosMode::getTechnologyHouse(int house, int structure) const {
    if(!enabled || !isPlayerHouseID(house)) return house;
    const auto found = std::find(Structures.begin(), Structures.end(), structure);
    return found == Structures.end() ? house : donors[house][found - Structures.begin()];
}

void ChaosMode::generate(TechnologyTable& objectData, bool requested, Random& random) {
    reset();
    if(!requested) return;
    enabled = true;
    std::array<std::array<ObjectData::ObjectDataStruct, NUM_HOUSES>, Structures.size()> original;
    for(size_t index = 0; index < Structures.size(); ++index)
        std::copy(std::begin(objectData[Structures[index]]),
                  std::end(objectData[Structures[index]]), original[index].begin());
    // Draw only from playable houses, in fixed item/house order on the synchronized stream.
    // Natives neither receives nor donates a technology, even if its data is enabled.
    for(size_t index = 0; index < Structures.size(); ++index) {
        const int structure = Structures[index];
        for(int house = 0; house < NUM_PLAYER_HOUSES; ++house) {
            const auto& recipient = original[index][house];
            if(recipient.techLevel < 0) continue;
            std::array<int, NUM_PLAYER_HOUSES> candidates{};
            int count = 0;
            for(int donor = 0; donor < NUM_PLAYER_HOUSES; ++donor) {
                const auto& source = original[index][donor];
                if(source.enabled && source.builder != ItemID_Invalid
                   && source.techLevel == recipient.techLevel) candidates[count++] = donor;
            }
            if(count > 1) {
                const auto end = std::remove(candidates.begin(), candidates.begin() + count, house);
                count = static_cast<int>(end - candidates.begin());
            }
            if(count == 0) continue;
            const int donor = candidates[random.rand(0, count - 1)];
            donors[house][index] = donor;
            objectData[structure][house] = original[index][donor];
        }
    }
}

void ChaosMode::save(OutputStream& stream) const {
    stream.writeBool(enabled);
    if(!enabled) return;
    stream.writeUint32(NUM_HOUSES);
    stream.writeUint32(static_cast<Uint32>(Structures.size()));
    for(size_t index = 0; index < Structures.size(); ++index) {
        stream.writeUint32(Structures[index]);
        for(int house = 0; house < NUM_HOUSES; ++house) stream.writeUint8(donors[house][index]);
    }
}

void ChaosMode::load(InputStream& stream) {
    reset();
    if(!stream.readBool()) return;
    if(stream.readUint32() != NUM_HOUSES || stream.readUint32() != Structures.size())
        throw std::runtime_error("Invalid Chaos Mode technology table dimensions");
    std::array<bool, Structures.size()> seen{};
    for(size_t entry = 0; entry < Structures.size(); ++entry) {
        const auto item = stream.readUint32();
        const auto found = std::find(Structures.begin(), Structures.end(), static_cast<int>(item));
        if(found == Structures.end()) throw std::runtime_error("Invalid Chaos Mode building");
        const auto index = static_cast<size_t>(found - Structures.begin());
        if(seen[index]) throw std::runtime_error("Duplicate Chaos Mode building");
        seen[index] = true;
        for(int house = 0; house < NUM_HOUSES; ++house) {
            const int donor = stream.readUint8();
            if((isPlayerHouseID(house) && !isPlayerHouseID(donor))
               || (house == HOUSE_NATIVES && donor != HOUSE_NATIVES))
                throw std::runtime_error("Invalid Chaos Mode donor faction");
            donors[house][index] = donor;
        }
    }
    enabled = true;
}
