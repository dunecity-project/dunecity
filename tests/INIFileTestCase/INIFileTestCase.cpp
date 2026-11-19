/*
 *  INIFileTestCase.cpp - INIFile reading and write-back
 *
 *  Ported from the original Dune Legacy CppUnit suite, which only the retired
 *  autotools build listed and so never ran. The fixtures are unchanged: each
 *  *.ref file is the exact output of saveChangesTo after one edit. Write-back
 *  must keep what was not edited (comments, blank lines, key order, spelling).
 */

#include <catch2/catch_test_macros.hpp>
#include <FileClasses/INIFile.h>

#include "TestSourceDir.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace {
std::string fixture(const std::string& name) {
    return (testSourceDir() / "tests" / "INIFileTestCase" / name).string();
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// Saves to a private directory and compares the bytes with the reference file.
void requireSavedAs(const INIFile& ini, const std::string& reference) {
    const auto dir = std::filesystem::temp_directory_path()
        / ("dunecity-inifile-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    const auto saved = dir / "saved.ini";
    const bool written = ini.saveChangesTo(saved.string());
    const std::string actual = written ? readFile(saved) : std::string();
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
    REQUIRE(written);
    CHECK(actual == readFile(fixture(reference)));
}
} // namespace

TEST_CASE("INIFile reads the anonymous section and reports sections and keys", "[ini]") {
    const INIFile ini(fixture("INIFileTestCase1.ini"));
    CHECK(ini.getStringValue("", "anonymous_section_key") == "exists");
    CHECK(ini.getStringValue("", "NonExistent", "SomeDefaultValue") == "SomeDefaultValue");

    CHECK(ini.hasSection(""));
    CHECK(ini.hasSection("Section6 extreme"));
    CHECK_FALSE(ini.hasSection("NonExistent"));

    CHECK(ini.hasKey("", "anonymous_section_key"));
    CHECK_FALSE(ini.hasKey("", "NonExistent"));
    CHECK(ini.hasKey("Section6 extreme", "a b c"));
    CHECK_FALSE(ini.hasKey("Section6 extreme", "NonExistent"));
    CHECK_FALSE(ini.hasKey("NonExistent", "NonExistent"));
}

TEST_CASE("INIFile reads strings, integers, booleans and doubles, with defaults", "[ini]") {
    const INIFile ini(fixture("INIFileTestCase1.ini"));
    CHECK(ini.getStringValue("Section1_StringKeys", "StringKey1") == "StringValue1");
    CHECK(ini.getStringValue("Section1_StringKeys", "StringKey2") == "StringValue2");
    CHECK(ini.getStringValue("Section1_StringKeys", "StringKey3") == "StringValue3");
    CHECK(ini.getStringValue("Section1_StringKeys", "StringKey4") == "Some text with multiple words ");
    CHECK(ini.getStringValue("Section1_StringKeys", "NonExistent", "SomeDefaultValue") == "SomeDefaultValue");
    CHECK(ini.getStringValue("NonExistent", "NonExistent", "SomeDefaultValue") == "SomeDefaultValue");

    CHECK(ini.getIntValue("Section2_IntKeys", "IntKey1") == 0);
    CHECK(ini.getIntValue("Section2_IntKeys", "IntKey2") == 0);
    CHECK(ini.getIntValue("Section2_IntKeys", "IntKey3") == 123);
    CHECK(ini.getIntValue("Section2_IntKeys", "IntKey4") == -5);
    CHECK(ini.getIntValue("Section2_IntKeys", "IntKey5") == 123);
    CHECK(ini.getIntValue("Section2_IntKeys", "NonExistent", 42) == 42);

    CHECK(ini.getBoolValue("Section3_BoolKeys", "BoolKey1"));
    CHECK_FALSE(ini.getBoolValue("Section3_BoolKeys", "BoolKey2"));
    CHECK(ini.getBoolValue("Section3_BoolKeys", "BoolKey3"));
    CHECK_FALSE(ini.getBoolValue("Section3_BoolKeys", "BoolKey4"));
    CHECK(ini.getBoolValue("Section3_BoolKeys", "BoolKey5"));
    CHECK_FALSE(ini.getBoolValue("Section3_BoolKeys", "BoolKey6"));
    CHECK(ini.getBoolValue("Section3_BoolKeys", "BoolKey7"));
    CHECK_FALSE(ini.getBoolValue("Section3_BoolKeys", "BoolKey8"));
    CHECK(ini.getBoolValue("Section3_BoolKeys", "NonExistent", true));
    CHECK_FALSE(ini.getBoolValue("Section3_BoolKeys", "NonExistent", false));

    CHECK(ini.getDoubleValue("Section4_DoubleKeys", "DoubleKey1") == 0.0);
    CHECK(ini.getDoubleValue("Section4_DoubleKeys", "DoubleKey2") == 1.0);
    CHECK(ini.getDoubleValue("Section4_DoubleKeys", "DoubleKey3") == 0.5);
    CHECK(ini.getDoubleValue("Section4_DoubleKeys", "DoubleKey4") == -3.0);
    CHECK(ini.getDoubleValue("Section4_DoubleKeys", "NonExistent", 42.0) == 42.0);
}

TEST_CASE("INIFile matches section and key names and boolean words in any case", "[ini]") {
    const INIFile ini(fixture("INIFileTestCase1.ini"));
    CHECK(ini.getStringValue("Section5_MiXeDcAsE", "MiXeDcAsEkEy") == "mixedCaseString");
    CHECK(ini.getBoolValue("Section5_MiXeDcAsE", "BoolKey1"));
    CHECK_FALSE(ini.getBoolValue("Section5_MiXeDcAsE", "BoolKey2"));
    CHECK(ini.getBoolValue("Section5_MiXeDcAsE", "BoolKey3"));
    CHECK_FALSE(ini.getBoolValue("Section5_MiXeDcAsE", "BoolKey4"));
    CHECK(ini.getBoolValue("Section5_MiXeDcAsE", "BoolKey5"));
    CHECK_FALSE(ini.getBoolValue("Section5_MiXeDcAsE", "BoolKey6"));
}

TEST_CASE("INIFile ignores comments, commented-out keys and fake sections", "[ini]") {
    const INIFile ini(fixture("INIFileTestCase1.ini"));
    CHECK(ini.getStringValue("Section6 extreme", "comment", "SomeDefaultValue") == "SomeDefaultValue");
    CHECK(ini.getStringValue("Section6 extreme", "#comment", "SomeDefaultValue") == "SomeDefaultValue");
    CHECK(ini.getStringValue("Section6 extreme", "a b c") == "xyz abc");
    CHECK(ini.getStringValue("Section6 extreme", "tricky string") == "comment #fake comment");
}

TEST_CASE("INIFile iterates sections and keys in file order", "[ini]") {
    const INIFile ini(fixture("INIFileTestCase1.ini"));
    std::vector<std::string> sections;
    for(auto section = ini.begin(); section != ini.end(); ++section) {
        sections.push_back(section->getSectionName());
    }
    CHECK(sections == std::vector<std::string>{"", "Section1_StringKeys", "Section2_IntKeys", "Section3_BoolKeys",
                                               "Section4_DoubleKeys", "Section5_mixedCase", "Section6 extreme"});

    auto key = ini.begin("Section6 extreme");
    REQUIRE(key != ini.end("Section6 extreme"));
    CHECK(key->getKeyName() == "a b c");
    CHECK(key->getStringValue() == "xyz abc");
    ++key;
    REQUIRE(key != ini.end("Section6 extreme"));
    CHECK(key->getKeyName() == "tricky string");
    CHECK(key->getStringValue() == "comment #fake comment");
    ++key;
    CHECK(key == ini.end("Section6 extreme"));
}

TEST_CASE("INIFile write-back changes only the edited lines", "[ini]") {
    SECTION("changed values keep their trailing comments") {
        INIFile ini(fixture("INIFileTestCase2.ini"));
        ini.setStringValue("General", "StringKey", "Some New Value");
        ini.setStringValue("", "anonymousKey", "SomeNewValue");
        ini.setBoolValue("General", "BoolKey", false);
        ini.setIntValue("General", "IntKey", 0);
        requireSavedAs(ini, "INIFileTestCase2.ini.ref1");
    }
    SECTION("new keys join their section; a comment character is quoted") {
        INIFile ini(fixture("INIFileTestCase2.ini"));
        ini.setStringValue("General", "NewStringKey", "NewValue");
        ini.setBoolValue("General", "NewBoolKey", true);
        ini.setStringValue("EmptySection", "NewKey", "Value with comment char # ");
        requireSavedAs(ini, "INIFileTestCase2.ini.ref2");
    }
    SECTION("a new section is appended") {
        INIFile ini(fixture("INIFileTestCase2.ini"));
        ini.setStringValue("NewSection", "NewStringKey", "NewValue");
        requireSavedAs(ini, "INIFileTestCase2.ini.ref3");
    }
    SECTION("removed keys leave the rest of the section") {
        INIFile ini(fixture("INIFileTestCase3.ini"));
        ini.removeKey("Section2", "Key1");
        ini.removeKey("Section2", "Key3");
        ini.removeKey("Section2", "Key5");
        ini.removeKey("", "Key3");
        requireSavedAs(ini, "INIFileTestCase3.ini.ref1");
    }
    SECTION("cleared sections keep their headers") {
        INIFile ini(fixture("INIFileTestCase3.ini"));
        ini.clearSection("Section3");
        ini.clearSection("");
        ini.clearSection("Section7");
        requireSavedAs(ini, "INIFileTestCase3.ini.ref2");
    }
    SECTION("removed sections disappear with their keys") {
        INIFile ini(fixture("INIFileTestCase3.ini"));
        ini.removeSection("Section3");
        ini.removeSection("");
        ini.removeSection("Section7");
        requireSavedAs(ini, "INIFileTestCase3.ini.ref3");
    }
    SECTION("a cleared section can be refilled") {
        INIFile ini(fixture("INIFileTestCase3.ini"));
        ini.clearSection("Section3");
        ini.setStringValue("Section3", "Key1", "a");
        ini.setStringValue("Section3", "Key2", "b");
        ini.setStringValue("Section3", "Key3", "c");
        ini.setStringValue("Section3", "Key4", "d");
        ini.setStringValue("Section3", "Key5", "e");
        requireSavedAs(ini, "INIFileTestCase3.ini.ref4");
    }
}
