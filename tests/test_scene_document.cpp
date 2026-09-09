#include <doctest/doctest.h>
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/surfaces/Surface.hpp"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

// Regression: parse_document re-derived the sun angles from `direction` without
// passing a fallback azimuth. Azimuth is degenerate at the pole, so a zenith sun
// returned the default 180 and any authored azimuth_deg was silently discarded -
// a user edit lost on save/reload.
TEST_CASE("SceneDocument: authored azimuth survives a zenith direction") {
    const auto j = nlohmann::json::parse(R"({
      "scene": {
        "sun": { "direction": [0,0,-1], "azimuth_deg": 90.0, "elevation_deg": 90.0,
                 "dni_wm2": 1000.0, "sunshape": {"type":"pillbox","half_angle_mrad":4.65} },
        "aperture": { "type":"disk", "center":[0,0,2], "normal":[0,0,1], "radius":1.0 },
        "materials": [ {"id":"m","type":"absorber"} ],
        "elements": [],
        "receiver": { "surface": {"type":"plane","half_width":0.05,"half_height":0.05},
                      "grid": {"nx":8,"ny":8} }
      }
    })");

    const auto doc = scrt::io::parse_document(j);
    CHECK(doc.sun.azimuth_deg == doctest::Approx(90.0));
    CHECK(doc.sun.elevation_deg == doctest::Approx(90.0));
    REQUIRE(doc.sun.direction.has_value());

    // And it must survive a full write -> reparse cycle.
    const auto round = scrt::io::parse_document(scrt::io::write_document(doc));
    CHECK(round.sun.azimuth_deg == doctest::Approx(90.0));
}

// The transform "scale" key must survive all the way through the real load path.
// Wave 2 replaced SceneLoader's legacy parse_transform (translation + rotation only)
// with parse_document; if load_scene ever stopped routing through it, a scaled object
// would load at the wrong size silently, since unknown keys are ignored.
TEST_CASE("SceneDocument: transform scale survives load_scene end to end") {
    const char* body = R"({
      "scene": {
        "sun": { "direction": [0,0,-1], "dni_wm2": 1000.0,
                 "sunshape": {"type":"pillbox","half_angle_mrad":4.65} },
        "aperture": { "type":"disk", "center":[0,0,2], "normal":[0,0,1], "radius":1.0 },
        "materials": [ {"id":"mir","type":"perfect_mirror"} ],
        "elements": [ { "name":"scaled_plate", "material":"mir",
                        "surface": {"type":"plane","half_width":1.0,"half_height":1.0},
                        "transform": {"translation":[0,0,0],"scale":[3.0,5.0,1.0]} } ],
        "receiver": { "surface": {"type":"plane","half_width":0.05,"half_height":0.05},
                      "grid": {"nx":8,"ny":8} }
      }
    })";
    const auto dir = std::filesystem::temp_directory_path() / "scrt_scale_rt";
    std::filesystem::create_directories(dir);
    const auto file = dir / "scaled.json";
    { std::ofstream out(file); out << body; }

    auto ls = scrt::io::load_scene(file);
    REQUIRE(ls.scene != nullptr);
    REQUIRE(ls.scene->surfaces().size() == 1u);

    // A unit-square plane scaled by (3,5) must span x in [-3,3], y in [-5,5].
    const auto b = ls.scene->surfaces()[0]->world_bounds();
    CHECK(b.max().x == doctest::Approx(3.0).epsilon(1e-9));
    CHECK(b.max().y == doctest::Approx(5.0).epsilon(1e-9));

    // And every surface must carry a non-zero stable id.
    CHECK(ls.scene->surfaces()[0]->id() != 0u);

    std::filesystem::remove_all(dir);
}
