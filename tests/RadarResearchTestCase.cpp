// SPDX-License-Identifier: GPL-2.0-or-later
#include <catch2/catch_all.hpp>
#include <Command.h>
#include <CommandValidation.h>
#include <mod/RadarResearch.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <string>

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
    IMemoryStream input(saved.getData(), saved.getDataLength());
    Command restored(input);
    REQUIRE(restored.getPlayerID() == original.getPlayerID());
    REQUIRE(restored.getCommandID() == CMD_RESEARCH_TECH_LEVEL);
    REQUIRE((restored.getParameter() == std::vector<Uint32>{0x12345678u}));
    OMemoryStream resaved;
    resaved.open();
    restored.save(resaved);
    REQUIRE(std::string(saved.getData(), saved.getDataLength())
            == std::string(resaved.getData(), resaved.getDataLength()));
}
