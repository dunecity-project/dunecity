// SPDX-License-Identifier: GPL-2.0-or-later
#include <catch2/catch_all.hpp>

#include <Command.h>
#include <CommandValidation.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <structures/BuildingSale.h>

#include <array>
#include <limits>
#include <string>
#include <vector>

TEST_CASE("Building sales refund completed investments at the house rate", "[tornie][sale][refund]") {
    CHECK(BuildingSale::refund(1001, 0, 0, 0, false, false) == 500);
    CHECK(BuildingSale::refund(1001, 0, 0, 0, false, true) == 750);
    // All completed factory levels are part of the investment, not paid again in full.
    CHECK(BuildingSale::refund(1000, 0, 3, 500, false, false) == 1250);
    CHECK(BuildingSale::refund(1000, 0, 3, 500, false, true) == 1875);
    // Scoutpost conversions retain their original purchase plus the full conversion charge.
    CHECK(BuildingSale::refund(250, 600, 0, 0, false, false) == 425);
    CHECK(BuildingSale::refund(250, 600, 0, 0, false, true) == 637);
    // The Rebels power upgrade is a completed 150-credit investment.
    CHECK(BuildingSale::refund(300, 0, 0, 0, true, false) == 225);
    CHECK(BuildingSale::refund(300, 0, 0, 0, true, true) == 337);
}

TEST_CASE("Building sale arithmetic applies rounding once and does not overflow the investment", "[tornie][sale][refund]") {
    CHECK(BuildingSale::refund(1, 1, 1, 1, false, false) == 1);
    CHECK(BuildingSale::refund(1, 1, 1, 1, false, true) == 2);
    CHECK(BuildingSale::refund(-100, -200, 2, -300, false, false) == 0);
    const int price = std::numeric_limits<int>::max();
    CHECK(BuildingSale::refund(price, 0, 1, price, false, false) == price);
}

TEST_CASE("Selling a builder refunds paid progress rather than unpaid queued prices", "[tornie][sale][refund]") {
    const auto one = BuildingSale::pendingRefund(1, 12.75_fix, 7.25_fix, false, 0, 0);
    const auto many = BuildingSale::pendingRefund(15, 12.75_fix, 7.25_fix, false, 0, 0);
    CHECK(one.total == 12.75_fix);
    CHECK(one.fromStarting == 7.25_fix);
    CHECK(many.total == one.total);
    CHECK(many.fromStarting == one.fromStarting);
    const auto unpaid = BuildingSale::pendingRefund(15, 0, 0, false, 0, 0);
    CHECK(unpaid.total == 0);
    CHECK(unpaid.fromStarting == 0);
}

TEST_CASE("Pending upgrade refunds preserve starting cash without refunding completed progress twice", "[tornie][sale][refund]") {
    const auto pending = BuildingSale::pendingRefund(1, 4.5_fix, 2_fix, true, 11.25_fix, 9_fix);
    CHECK(pending.total == 15.75_fix);
    CHECK(pending.fromStarting == 11_fix);
    const auto completed = BuildingSale::pendingRefund(0, 4.5_fix, 2_fix, false, 11.25_fix, 9_fix);
    CHECK(completed.total == 0);
    CHECK(completed.fromStarting == 0);
}

TEST_CASE("Sale and Windtrap commands require exactly one object id on the wire", "[tornie][sale][command]") {
    for(const auto id : {CMD_STRUCTURE_SELL, CMD_WINDTRAP_POWER_UPGRADE, CMD_SCOUTPOST_SLOWPOST_UPGRADE}) {
        CAPTURE(id);
        CHECK(CommandValidation::isWellFormedCommand(id, 1));
        CHECK_FALSE(CommandValidation::isWellFormedCommand(id, 0));
        CHECK_FALSE(CommandValidation::isWellFormedCommand(id, 2));
        CHECK(id > CMD_UNIT_FORMATION_HUNT); // No existing command id was repurposed.
    }
}

TEST_CASE("Sale and Windtrap commands have deterministic real serialization roundtrips", "[tornie][sale][command][determinism]") {
    for(const auto id : {CMD_STRUCTURE_SELL, CMD_WINDTRAP_POWER_UPGRADE, CMD_SCOUTPOST_SLOWPOST_UPGRADE}) {
        for(const Uint32 object : {0u, 0x12345678u, 0xffffffffu}) {
            CAPTURE(id, object);
            const Command original(7, id, object);
            OMemoryStream first;
            first.open();
            original.save(first);
            REQUIRE(first.getDataLength() == 13);
            const auto* bytes = reinterpret_cast<const unsigned char*>(first.getData());
            CHECK(bytes[0] == 7);
            for(int offset = 0; offset < 4; ++offset) {
                CHECK(bytes[1 + offset] == ((static_cast<Uint32>(id) >> (8 * offset)) & 0xff));
                CHECK(bytes[9 + offset] == ((object >> (8 * offset)) & 0xff));
            }
            CHECK(bytes[5] == 1);
            CHECK(bytes[6] == 0);
            CHECK(bytes[7] == 0);
            CHECK(bytes[8] == 0);
            IMemoryStream input(first.getData(), first.getDataLength());
            const Command restored(input);
            CHECK(restored.getPlayerID() == original.getPlayerID());
            CHECK(restored.getCommandID() == id);
            CHECK(restored.getParameter() == std::vector<Uint32>{object});
            OMemoryStream second;
            second.open();
            restored.save(second);
            CHECK(std::string(first.getData(), first.getDataLength())
                  == std::string(second.getData(), second.getDataLength()));
        }
    }
}
