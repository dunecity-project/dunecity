#include <catch2/catch_all.hpp>
#include <dunecity/HospitalCare.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>

#include <string>

using DuneCity::HospitalCareState;
using DuneCity::HospitalTrip;

namespace {
HospitalTrip trip(uint32_t hospital, HospitalTrip::Stage stage, int16_t x = 3, int16_t y = 4) {
    HospitalTrip t;
    t.hospitalID = hospital;
    t.spotX = x;
    t.spotY = y;
    t.stage = stage;
    t.startCycle = 1000 + hospital;
    return t;
}
}

TEST_CASE("Hospital beds: claims count every stage, patients only the healing ones", "[city][hospital]") {
    HospitalCareState care;
    REQUIRE(DuneCity::kHospitalBeds == 5);
    for(uint32_t unit = 1; unit <= 5; ++unit) care.setTrip(unit, trip(77, HospitalTrip::Stage::Healing));
    care.setTrip(6, trip(77, HospitalTrip::Stage::Waiting));
    care.setTrip(7, trip(77, HospitalTrip::Stage::Travelling));
    care.setTrip(8, trip(88, HospitalTrip::Stage::Travelling));
    REQUIRE(care.patients(77) == 5);
    REQUIRE_FALSE(care.bedFree(77));
    REQUIRE(care.claims(77) == 7);
    REQUIRE(care.bedFree(88));
    REQUIRE(care.isClaimed(88));
    REQUIRE_FALSE(care.isClaimed(99));

    // A discharged, dead or re-ordered soldier releases its claim; the lock ends with the last one.
    care.cancel(8);
    REQUIRE_FALSE(care.isClaimed(88));
    care.cancel(1);
    REQUIRE(care.bedFree(77));
    for(uint32_t unit = 2; unit <= 7; ++unit) care.cancel(unit);
    REQUIRE_FALSE(care.isClaimed(77));
}

TEST_CASE("Hospital care state round-trips through a save", "[city][hospital][save]") {
    HospitalCareState care;
    care.setOverlays({{40, 1}, {41, 2}, {55, 1}});
    care.setTrip(12, trip(40, HospitalTrip::Stage::Healing, 9, 10));
    auto carried = trip(55, HospitalTrip::Stage::Travelling, -1, 7);
    carried.carried = true;
    care.setTrip(13, carried);
    care.setTrip(14, trip(40, HospitalTrip::Stage::Waiting));
    care.setQuietSince({{21, 3000}, {22, 3100}});

    OMemoryStream out;
    out.open();
    care.save(out);
    out.writeUint32(0xfeedbeefu);
    IMemoryStream in(out.getData(), out.getDataLength());
    HospitalCareState loaded;
    loaded.setTrip(99, trip(1, HospitalTrip::Stage::Healing));   // replaced, not merged
    loaded.load(in);
    REQUIRE(in.readUint32() == 0xfeedbeefu);
    REQUIRE(loaded.overlays() == care.overlays());
    REQUIRE(loaded.trips() == care.trips());
    REQUIRE(loaded.quietSince() == care.quietSince());
    REQUIRE(loaded.trip(99) == nullptr);
}

TEST_CASE("Hospital care state refuses a damaged stream", "[city][hospital][save]") {
    HospitalCareState care;
    care.setTrip(12, trip(40, HospitalTrip::Stage::Healing));
    OMemoryStream out;
    out.open();
    care.save(out);
    std::string bytes(out.getData(), out.getDataLength());
    // overlay count (4) + trip count (4) + unit (4) + hospital (4) + x (2) + y (2) = the stage byte.
    bytes[20] = 3;
    IMemoryStream in(bytes.data(), bytes.size());
    HospitalCareState loaded;
    REQUIRE_THROWS(loaded.load(in));
}
