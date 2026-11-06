// SPDX-License-Identifier: GPL-2.0-or-later
#include <catch2/catch_all.hpp>
#include <Command.h>
#include <CommandValidation.h>
#include <TechResearch.h>
#include <fixmath/FixPoint.h>
#include <mod/RadarResearch.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <string>
#include <limits>

TEST_CASE("Radar research commands preserve object and kind across replay serialization",
          "[tornie][radar][command][determinism]") {
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_RADAR_RESEARCH, 2));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_RADAR_RESEARCH, 1));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_RADAR_RESEARCH, 3));
    for(int kind = 0; kind < radar::Count; ++kind) {
        CAPTURE(kind);
        Command command(0x21, CMD_RADAR_RESEARCH, 0x12345678u, kind);
        OMemoryStream saved;
        saved.open();
        command.save(saved);
        REQUIRE(saved.getDataLength() == 17);
        IMemoryStream input(saved.getData(), saved.getDataLength());
        Command restored(input);
        REQUIRE(restored.getPlayerID() == 0x21);
        REQUIRE(restored.getCommandID() == CMD_RADAR_RESEARCH);
        REQUIRE((restored.getParameter() == std::vector<Uint32>{0x12345678u, static_cast<Uint32>(kind)}));
        OMemoryStream resaved;
        resaved.open();
        restored.save(resaved);
        REQUIRE(std::string(saved.getData(), saved.getDataLength())
                == std::string(resaved.getData(), resaved.getDataLength()));
    }
}

TEST_CASE("House technology research uses a single owned lab command in saves and on the wire",
          "[research][command][determinism]") {
    REQUIRE(Structure_ResearchLab == 88);
    REQUIRE(isStructure(Structure_ResearchLab));
    REQUIRE_FALSE(isTornieExclusiveItem(Structure_ResearchLab));
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_RESEARCH_TECH_LEVEL, 1));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_RESEARCH_TECH_LEVEL, 0));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_RESEARCH_TECH_LEVEL, 2));
    Command original(0x21, CMD_RESEARCH_TECH_LEVEL, 0x12345678u);
    OMemoryStream saved;
    saved.open();
    original.save(saved);
    REQUIRE(saved.getDataLength() == 13); // No payment terms are smuggled into the authorized lab command.
    IMemoryStream input(saved.getData(), saved.getDataLength());
    Command restored(input);
    REQUIRE(restored.getPlayerID() == original.getPlayerID());
    REQUIRE(restored.getCommandID() == CMD_RESEARCH_TECH_LEVEL);
    REQUIRE((restored.getParameter() == std::vector<Uint32>{0x12345678u}));
    REQUIRE(input.getRemainingLength() == 0);
    OMemoryStream resaved;
    resaved.open();
    restored.save(resaved);
    REQUIRE(std::string(saved.getData(), saved.getDataLength())
            == std::string(resaved.getData(), resaved.getDataLength()));
}

TEST_CASE("Research installments distribute every fixed-point remainder without overflow",
          "[research][installments][determinism]") {
    const Sint64 total = GENERATE(Sint64(0), Sint64(1), Sint64(2), Sint64(7),
        FixPoint(1000).getRawValue(), FixPoint(32000).getRawValue(), std::numeric_limits<Sint64>::max());
    const int duration = GENERATE(1, 3, 7, 7500, 135000);
    CAPTURE(total, duration);
    REQUIRE(TechResearch::cumulativeDueRaw(total, 0, duration) == 0);
    Sint64 expected = 0, carry = 0;
    const Sint64 quotient = total / duration, remainder = total % duration;
    for(int completed = 1; completed <= duration; ++completed) {
        expected += quotient;
        carry += remainder;
        if(carry >= duration) {
            expected += carry / duration;
            carry %= duration;
        }
        REQUIRE(TechResearch::cumulativeDueRaw(total, completed, duration) == expected);
    }
    REQUIRE(expected == total);
    REQUIRE(carry == 0);
}

TEST_CASE("Builder upgrades carry only the authorized object through replay and wire commands",
          "[upgrade][installments][command][determinism]") {
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_BUILDER_UPGRADE, 1));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_BUILDER_UPGRADE, 0));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_BUILDER_UPGRADE, 2));
    Command original(0x21, CMD_BUILDER_UPGRADE, 0x12345678u);
    OMemoryStream saved; saved.open(); original.save(saved);
    REQUIRE(saved.getDataLength() == 13);
    IMemoryStream input(saved.getData(), saved.getDataLength());
    Command restored(input);
    REQUIRE(restored.getPlayerID() == original.getPlayerID());
    REQUIRE(restored.getCommandID() == CMD_BUILDER_UPGRADE);
    REQUIRE((restored.getParameter() == std::vector<Uint32>{0x12345678u}));
    REQUIRE(input.getRemainingLength() == 0);
    OMemoryStream resaved; resaved.open(); restored.save(resaved);
    REQUIRE(std::string(saved.getData(), saved.getDataLength())
            == std::string(resaved.getData(), resaved.getDataLength()));
}
