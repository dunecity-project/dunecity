#include <catch2/catch_all.hpp>

#include <structures/FactoryQueuePolicy.h>

#include <algorithm>
#include <array>
#include <list>

namespace {
struct Entry {
    std::uint32_t itemID;
    int startingCreditsPaid = 0;
};
const auto anyItem = [](std::uint32_t) { return true; };
}

TEST_CASE("Factory queue: only ordinary pay-as-built factories share orders", "[factoryqueue][eligibility]") {
    const std::array<std::uint32_t, 6> eligible = {Structure_LightFactory, Structure_HeavyFactory,
        Structure_HighTechFactory, Structure_Barracks, Structure_WOR, Structure_Worfinery};
    for(std::uint32_t item = 0; item < Num_ItemID; ++item) {
        INFO("builder item " << item);
        REQUIRE(FactoryQueue::builderSharesQueue(item)
            == (std::find(eligible.begin(), eligible.end(), item) != eligible.end()));
    }
    REQUIRE_FALSE(FactoryQueue::builderSharesQueue(ItemID_Invalid));
    REQUIRE_FALSE(FactoryQueue::builderSharesQueue(Structure_ConstructionYard));
    REQUIRE_FALSE(FactoryQueue::builderSharesQueue(Structure_StarPort));
    REQUIRE_FALSE(FactoryQueue::builderSharesQueue(Structure_ChaosFactory));
    REQUIRE_FALSE(FactoryQueue::builderSharesQueue(Structure_LoveFactory));
}

TEST_CASE("Factory queue: earliest waiting position then structure-list order wins", "[factoryqueue][ordering]") {
    using FactoryQueue::SharedSlot;
    using FactoryQueue::shouldPickOrder;
    REQUIRE(shouldPickOrder({1, 9}, {2, 0}));
    REQUIRE_FALSE(shouldPickOrder({2, 0}, {1, 9}));
    REQUIRE(shouldPickOrder({1, 2}, {1, 3}));
    REQUIRE_FALSE(shouldPickOrder({1, 3}, {1, 2}));
    REQUIRE_FALSE(shouldPickOrder({1, 2}, {1, 2}));
    REQUIRE(shouldPickOrder({0, 9}, {1, 0}));

    const std::array<SharedSlot, 4> offers = {{{2, 0}, {1, 3}, {1, 1}, {3, 2}}};
    std::array<int, 4> order = {0, 1, 2, 3};
    do {
        const SharedSlot* selected = nullptr;
        for(int i : order) {
            if(selected == nullptr || shouldPickOrder(offers[i], *selected)) selected = &offers[i];
        }
        REQUIRE(selected == &offers[2]);
    } while(std::next_permutation(order.begin(), order.end()));
}

TEST_CASE("Factory queue: duplicate orders protect only the first active entry", "[factoryqueue][progress]") {
    std::list<Entry> queue = {{Unit_Tank}, {Unit_Tank}, {Unit_Tank}, {Unit_Tank}, {Unit_Tank}};
    auto waiting = FactoryQueue::firstWaitingOrder(queue, Unit_Tank, anyItem);
    REQUIRE(waiting.first == std::next(queue.begin()));
    REQUIRE(waiting.second == 1);
    REQUIRE(queue.size() == 5); // Picking never mutates the donor or its paid progress.

    SECTION("a current item need not be entry zero") {
        queue = {{Unit_SiegeTank}, {Unit_Tank}, {Unit_Tank}};
        waiting = FactoryQueue::firstWaitingOrder(queue, Unit_Tank,
            [](std::uint32_t item) { return item == Unit_Tank; });
        REQUIRE(waiting.first == std::next(queue.begin(), 2));
        REQUIRE(waiting.second == 2);
    }
    SECTION("an earlier noncurrent entry is waiting, not paid progress") {
        queue = {{Unit_SiegeTank}, {Unit_Tank}, {Unit_Tank}};
        waiting = FactoryQueue::firstWaitingOrder(queue, Unit_Tank, anyItem);
        REQUIRE(waiting.first == queue.begin());
        REQUIRE(waiting.second == 0);
    }
    SECTION("an invalid current item conservatively protects entry zero") {
        waiting = FactoryQueue::firstWaitingOrder(queue, ItemID_Invalid, anyItem);
        REQUIRE(waiting.first == std::next(queue.begin()));
        REQUIRE(waiting.second == 1);
    }
}

TEST_CASE("Factory queue: paid and unavailable or unit-limited waiting entries stay put", "[factoryqueue][availability]") {
    std::list<Entry> queue = {{Unit_Tank}, {Unit_Tank, 25}, {Unit_Launcher}, {Unit_Harvester}, {Unit_SiegeTank}};
    // The engine supplies build-list/upgrade availability and the house's per-item unit limit.
    auto waiting = FactoryQueue::firstWaitingOrder(queue, Unit_Tank,
        [](std::uint32_t item) { return item != Unit_Launcher && item != Unit_Harvester; });
    REQUIRE(waiting.first == std::next(queue.begin(), 4));
    REQUIRE(waiting.second == 4);
    REQUIRE(std::next(queue.begin())->startingCreditsPaid == 25);

    SECTION("when no waiting item can be built there is no offer") {
        waiting = FactoryQueue::firstWaitingOrder(queue, Unit_Tank, [](std::uint32_t) { return false; });
        REQUIRE(waiting.first == queue.end());
    }
    SECTION("empty queues and active-only queues offer nothing") {
        queue.clear();
        waiting = FactoryQueue::firstWaitingOrder(queue, Unit_Tank, anyItem);
        REQUIRE(waiting.first == queue.end());
        queue.push_back({Unit_Tank});
        waiting = FactoryQueue::firstWaitingOrder(queue, Unit_Tank, anyItem);
        REQUIRE(waiting.first == queue.end());
    }
    SECTION("an upfront-paid waiting entry is never an offer even when buildable") {
        queue = {{Unit_Tank}, {Unit_Tank, 25}};
        waiting = FactoryQueue::firstWaitingOrder(queue, Unit_Tank, anyItem);
        REQUIRE(waiting.first == queue.end());
    }
}
