/*
 *  FileSystemTestCase.cpp - getBasename and getDirname
 *
 *  Ported from the original Dune Legacy CppUnit suite, which only the retired
 *  autotools build listed and so never ran.
 */

#include <catch2/catch_test_macros.hpp>
#include <misc/FileSystem.h>

TEST_CASE("getBasename drops directories and trailing slashes", "[filesystem][paths]") {
    CHECK(getBasename("test") == "test");
    CHECK(getBasename("/test") == "test");
    CHECK(getBasename("/test/") == "test");
    CHECK(getBasename("./test.txt") == "test.txt");
    CHECK(getBasename("/bla/test.txt") == "test.txt");
    CHECK(getBasename("/") == "/");
}

TEST_CASE("getBasename can also drop the last extension", "[filesystem][paths]") {
    CHECK(getBasename("test.txt", true) == "test");
    CHECK(getBasename("/test.txt", true) == "test");
    CHECK(getBasename("/test.txt/", true) == "test");
    CHECK(getBasename("./test.txt.txt", true) == "test.txt");
    CHECK(getBasename("/bla/test.txt.txt", true) == "test.txt");
}

TEST_CASE("getDirname drops the last path element", "[filesystem][paths]") {
    CHECK(getDirname("/test/test") == "/test");
    CHECK(getDirname("/test/test/") == "/test");
    CHECK(getDirname("test") == ".");
    CHECK(getDirname("/") == "/");
}
