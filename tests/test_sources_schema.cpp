#include <doctest/doctest.h>
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/sources/Laser.hpp"
#include "scrt/sources/SunSource.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <variant>

#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

using json = nlohmann::json;
using scrt::io::LaserSourceDoc;
using scrt::io::SunSourceDoc;

namespace {

/// The smallest legal scene body (materials, elements, receiver) to wrap a source block in.
json body() {
    return json::parse(R"({
      "materials": [ {"id":"m","type":"absorber"} ],
      "elements": [],
      "receiver": { "surface": {"type":"plane","half_width":0.05,"half_height":0.05},
                    "grid": {"nx":8,"ny":8} }
    })");
}

json legacy_sun() {
    return json::parse(R"({ "direction": [0,0,-1], "dni_wm2": 900.0,
                            "sunshape": {"type":"pillbox","half_angle_mrad":4.65} })");
}
json legacy_aperture() {
    return json::parse(R"({ "type":"disk", "center":[0,0,2], "normal":[0,0,1], "radius":0.7 })");
}
json laser_entry() {
    return json::parse(R"({ "type":"laser", "origin":[0,0,1], "direction":[0,0,-1],
                            "power_w": 5.0, "wavelength_nm": 1064.0,
                            "beam_diameter_m": 0.002, "divergence_mrad": 1.5 })");
}

json wrap(json scene) { return json{{"scene", std::move(scene)}}; }

} // namespace

TEST_CASE("Sources schema: the legacy sun + aperture pair desugars into one SunSourceDoc") {
    json s = body();
    s["sun"] = legacy_sun();
    s["aperture"] = legacy_aperture();
    const auto doc = scrt::io::parse_document(wrap(s), /*strict=*/true);

    REQUIRE(doc.sources.size() == 1);
    const auto* sun = std::get_if<SunSourceDoc>(&doc.sources[0]);
    REQUIRE(sun != nullptr);
    CHECK(sun->dni_wm2 == 900.0);
    CHECK(sun->aperture.radius == 0.7);
    CHECK(sun->aperture.mode == "fixed");
    CHECK(sun->wavelength_nm == 550.0);

    // Written back in the legacy shape: no `sources` key, and no wavelength key at 550.
    const json w = scrt::io::write_document(doc);
    CHECK(w["scene"].contains("sun"));
    CHECK(w["scene"].contains("aperture"));
    CHECK_FALSE(w["scene"].contains("sources"));
    CHECK_FALSE(w["scene"]["sun"].contains("wavelength_nm"));
    CHECK(scrt::io::write_document(scrt::io::parse_document(w, true)) == w);
}

TEST_CASE("Sources schema: a legacy sun without an aperture block auto-fits") {
    json s = body();
    s["sun"] = legacy_sun();
    const auto doc = scrt::io::parse_document(wrap(s), true);
    REQUIRE(doc.sources.size() == 1);
    CHECK(std::get<SunSourceDoc>(doc.sources[0]).aperture.mode == "auto_fit");
}

TEST_CASE("Sources schema: a sources array with a sun and a laser round-trips in strict mode") {
    json s = body();
    json sun = legacy_sun();
    sun["type"] = "sun";
    sun["aperture"] = legacy_aperture();
    s["sources"] = json::array({sun, laser_entry()});

    const auto doc = scrt::io::parse_document(wrap(s), true);
    REQUIRE(doc.sources.size() == 2);
    REQUIRE(std::holds_alternative<SunSourceDoc>(doc.sources[0]));
    REQUIRE(std::holds_alternative<LaserSourceDoc>(doc.sources[1]));
    const auto& l = std::get<LaserSourceDoc>(doc.sources[1]);
    CHECK(l.power_w == 5.0);
    CHECK(l.wavelength_nm == 1064.0);
    CHECK(l.beam_diameter_m == 0.002);
    CHECK(l.divergence_mrad == 1.5);
    CHECK(l.origin.z == 1.0);
    CHECK(std::get<SunSourceDoc>(doc.sources[0]).aperture.radius == 0.7);

    // Two sources cannot be spelt the legacy way, so the writer uses the array, and the array
    // is stable under parse/write/parse/write.
    const json w = scrt::io::write_document(doc);
    CHECK(w["scene"].contains("sources"));
    CHECK_FALSE(w["scene"].contains("sun"));
    CHECK_FALSE(w["scene"].contains("aperture"));
    REQUIRE(w["scene"]["sources"].size() == 2);
    CHECK(w["scene"]["sources"][1]["type"] == "laser");
    CHECK(w["scene"]["sources"][1]["wavelength_nm"] == 1064.0);
    CHECK(scrt::io::write_document(scrt::io::parse_document(w, true)) == w);
}

TEST_CASE("Sources schema: a lone laser, or no source at all, is written as a sources array") {
    json s = body();
    s["sources"] = json::array({laser_entry()});
    const auto doc = scrt::io::parse_document(wrap(s), true);
    const json w = scrt::io::write_document(doc);
    CHECK(w["scene"].contains("sources"));
    CHECK_FALSE(w["scene"].contains("sun"));
    CHECK(scrt::io::first_sun(doc) == nullptr);

    json e = body();
    e["sources"] = json::array();
    const auto empty = scrt::io::parse_document(wrap(e), true);
    CHECK(empty.sources.empty());
    CHECK(scrt::io::write_document(empty)["scene"]["sources"].empty());
}

TEST_CASE("Sources schema: both spellings at once is refused in strict AND lax mode") {
    json s = body();
    s["sun"] = legacy_sun();
    json sun = legacy_sun();
    sun["type"] = "sun";
    s["sources"] = json::array({sun});
    CHECK_THROWS_AS(scrt::io::parse_document(wrap(s), true), std::runtime_error);
    CHECK_THROWS_AS(scrt::io::parse_document(wrap(s), false), std::runtime_error);

    json a = body();
    a["aperture"] = legacy_aperture();
    a["sources"] = json::array({laser_entry()});
    CHECK_THROWS_AS(scrt::io::parse_document(wrap(a), false), std::runtime_error);
}

TEST_CASE("Sources schema: unknown source types and stray keys are rejected") {
    json s = body();
    s["sources"] = json::array({json{{"type", "flashlight"}}});
    CHECK_THROWS_AS(scrt::io::parse_document(wrap(s), false), std::runtime_error);

    json l = laser_entry();
    l["colour"] = "red";
    json t = body();
    t["sources"] = json::array({l});
    CHECK_THROWS_AS(scrt::io::parse_document(wrap(t), true), std::runtime_error);
    CHECK_NOTHROW(scrt::io::parse_document(wrap(t), false));  // lax: passthrough, as elsewhere

    json missing = body();
    missing.erase("sun");
    CHECK_THROWS_AS(scrt::io::parse_document(wrap(missing), false), std::runtime_error);
}

TEST_CASE("Sources schema: a sun's wavelength is written only when it is not 550") {
    json s = body();
    json sun = legacy_sun();
    sun["wavelength_nm"] = 1064.0;
    s["sun"] = sun;
    const auto doc = scrt::io::parse_document(wrap(s), true);
    CHECK(std::get<SunSourceDoc>(doc.sources[0]).wavelength_nm == 1064.0);

    const json w = scrt::io::write_document(doc);
    CHECK(w["scene"]["sun"]["wavelength_nm"] == 1064.0);  // legacy shape still carries it
    const auto back = scrt::io::parse_document(w, true);
    CHECK(std::get<SunSourceDoc>(back.sources[0]).wavelength_nm == 1064.0);

    // And it reaches the live sun.
    const auto ls = scrt::io::build_scene(back, std::filesystem::path(SCRT_SOURCE_DIR));
    REQUIRE(ls.scene->primary_sun() != nullptr);
    CHECK(ls.scene->primary_sun()->wavelength_nm() == 1064.0);
}

TEST_CASE("Sources schema: the shipped laser bench loads, holds a laser, and traces") {
    const auto path = std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / "laser_bench.json";
    REQUIRE(std::filesystem::exists(path));
    auto ls = scrt::io::load_scene(path);
    REQUIRE(ls.scene != nullptr);
    REQUIRE(ls.scene->sources().size() == 1);
    CHECK(ls.scene->primary_sun() == nullptr);
    CHECK(ls.scene->display_aperture() == nullptr);
    CHECK(std::string(ls.scene->sources()[0]->type_name()) == "laser");

    ls.cfg.n_primary_rays = 20000;
    ls.cfg.rng_seed       = 42;
    ls.cfg.num_threads    = 1;
    scrt::tracer::Tracer tracer(*ls.scene);
    const auto res = tracer.run(ls.cfg);
    CHECK(res.primary_rays_traced == 20000);
    double total = 0.0;
    for (const auto& face : ls.scene->receiver()->faces())
        total += face->accumulator().total_power_w();
    // The beam crosses one BK7 interface, so a few percent reflects away and the rest lands.
    CHECK(total > 0.9 * ls.scene->sources()[0]->total_power_w());
    CHECK(total < ls.scene->sources()[0]->total_power_w());
}

