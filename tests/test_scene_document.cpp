#include <doctest/doctest.h>
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/io/ScenePaths.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/surfaces/Surface.hpp"
#include <exception>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <vector>

// SCRT_SOURCE_DIR is injected by CMake so tests can locate the committed scene corpus.
#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

namespace {

/// Non-recursive sweep of the four directories that hold committed scene files. Deliberately
/// excludes dist/solar-cooker-rt/examples/, which is a packaged copy, not source.
std::vector<std::filesystem::path> corpus_scenes() {
    const char* dirs[] = {
        "examples",
        "examples/panel_tests",
        "finalized_designs/scenes",
        "results/compact_study/scenes",
    };
    std::vector<std::filesystem::path> scenes;
    for (const char* dir : dirs) {
        const auto abs_dir = std::filesystem::path(SCRT_SOURCE_DIR) / dir;
        REQUIRE_MESSAGE(std::filesystem::is_directory(abs_dir), abs_dir.string());
        for (const auto& entry : std::filesystem::directory_iterator(abs_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
                scenes.push_back(entry.path());
        }
    }
    return scenes;
}

/// Reads a scene file exactly the way load_scene() does, comments tolerated.
nlohmann::json read_scene_json(const std::filesystem::path& path) {
    std::ifstream in(path);
    REQUIRE_MESSAGE(in.is_open(), path.string());
    return nlohmann::json::parse(in, nullptr, /*exceptions=*/true, /*ignore_comments=*/true);
}

/// Absolute path to a file inside the repository.
std::filesystem::path repo_file(const char* relative) {
    return std::filesystem::path(SCRT_SOURCE_DIR) / relative;
}

constexpr const char* kStlScene = "examples/stl_rectangular_3floors_606570deg_box.json";
constexpr const char* kStlMeshPath = "../assets/meshes/panels_only_rectangular_3floors_606570deg.stl";

} // namespace

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
    REQUIRE(scrt::io::first_sun(doc) != nullptr);
    CHECK(scrt::io::first_sun(doc)->azimuth_deg == doctest::Approx(90.0));
    CHECK(scrt::io::first_sun(doc)->elevation_deg == doctest::Approx(90.0));
    REQUIRE(scrt::io::first_sun(doc)->direction.has_value());

    // And it must survive a full write -> reparse cycle.
    const auto round = scrt::io::parse_document(scrt::io::write_document(doc));
    REQUIRE(scrt::io::first_sun(round) != nullptr);
    CHECK(scrt::io::first_sun(round)->azimuth_deg == doctest::Approx(90.0));
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

// Save-on-load ------------------------------------------------------------------

// io::load_scene() used to parse a SceneDocument, build the Scene from it and throw the document
// away, which made "open a file, edit it, save it" impossible without re-reading and re-parsing
// the original. LoadedScene::doc now carries it. This pins the guarantee that matters: the
// document that comes back is not a placeholder or a partial reconstruction, it is exactly what a
// direct parse_document() of the same file produces, and it can be written straight back out.
TEST_CASE("SceneDocument: load_scene keeps the document it built the scene from") {
    const auto path = repo_file("examples/parabolic_dish.json");
    REQUIRE_MESSAGE(std::filesystem::exists(path), path.string());

    const auto direct = scrt::io::parse_document(read_scene_json(path), /*strict=*/true);
    const auto ls     = scrt::io::load_scene(path);
    REQUIRE(ls.scene != nullptr);

    // The carried document describes the scene that was actually built.
    REQUIRE_FALSE(ls.doc.elements.empty());
    CHECK(ls.doc.elements.size() == ls.scene->surfaces().size());
    CHECK(ls.doc.elements.size() == direct.elements.size());
    CHECK(ls.doc.name == direct.name);

    // Writing it produces exactly what writing a directly-parsed document produces...
    const nlohmann::json from_loaded = scrt::io::write_document(ls.doc);
    const nlohmann::json from_direct = scrt::io::write_document(direct);
    CHECK(from_loaded == from_direct);

    // ...and the result is a real scene file: it re-parses in strict mode, is stable under a
    // second write, and loads back into an equivalent scene.
    const auto reparsed = scrt::io::parse_document(from_loaded, /*strict=*/true);
    CHECK(scrt::io::write_document(reparsed) == from_loaded);

    const auto dir = std::filesystem::temp_directory_path() / "scrt_doc_survives_load";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto saved = dir / "resaved.json";
    scrt::io::save_scene(ls.doc, saved);

    const auto reloaded = scrt::io::load_scene(saved);
    REQUIRE(reloaded.scene != nullptr);
    CHECK(reloaded.scene->surfaces().size() == ls.scene->surfaces().size());
    // The round trip is a fixed point: the reloaded document writes to the same JSON again.
    CHECK(scrt::io::write_document(reloaded.doc) == from_loaded);

    std::filesystem::remove_all(dir);
}

// T13 -------------------------------------------------------------------------

// Structural round-trip over the whole committed corpus: write(parse(file)) must equal
// write(parse(that)). The two halves are mutually reinforcing and that is the point - a key the
// writer FORGETS shows up as a JSON diff, and a key the writer INVENTS makes the second strict
// parse throw. Strict mode throughout, so an unrecognized key anywhere is a failure rather than a
// silent default. This is the only test that reads every shipped scene through the new
// document/writer pair; without it a writer regression would surface as corrupted user files.
TEST_CASE("T13: every corpus scene survives a strict parse/write/parse/write round trip") {
    const auto scenes = corpus_scenes();
    // Lower bound, not equality: the Python sweep scripts generate extra scenes into
    // results/compact_study/scenes/, and adding one must not fail this test.
    REQUIRE(scenes.size() >= 94u);

    std::size_t identical = 0;
    for (const auto& path : scenes) {
        const std::string name = path.string();
        try {
            const nlohmann::json first =
                scrt::io::write_document(scrt::io::parse_document(read_scene_json(path),
                                                                   /*strict=*/true));
            const nlohmann::json second =
                scrt::io::write_document(scrt::io::parse_document(first, /*strict=*/true));
            // Build the message eagerly: doctest's CHECK_MESSAGE binds its stream operator
            // tighter than '+', so inline concatenation will not compile.
            const std::string diff_msg = name + ": re-written document differs from the first write";
            CHECK_MESSAGE(first == second, diff_msg);
            if (first == second)
                ++identical;
        } catch (const std::exception& e) {
            const std::string threw_msg = name + " threw in strict mode: " + e.what();
            FAIL_CHECK(threw_msg);
        }
    }
    CHECK(identical == scenes.size());
}

// T16 -------------------------------------------------------------------------

// A mesh surface must round-trip as its stored path string plus scale_to_meters - never as a
// dump of imported vertex data, and never with the scale silently reset to 1.0. Baking geometry
// into the scene file would balloon it by megabytes and sever the link to the STL the user edits.
TEST_CASE("T16: a mesh surface round-trips as a path, not as baked geometry") {
    const auto doc = scrt::io::parse_document(read_scene_json(repo_file(kStlScene)),
                                               /*strict=*/true);
    const auto reparsed = scrt::io::parse_document(scrt::io::write_document(doc), /*strict=*/true);

    REQUIRE(reparsed.elements.size() == 1u);
    const auto* mesh = std::get_if<scrt::io::MeshDoc>(&reparsed.elements[0].surface);
    REQUIRE(mesh != nullptr);
    CHECK(mesh->path == kStlMeshPath);
    CHECK(mesh->scale_to_meters == doctest::Approx(0.001).epsilon(1e-15));

    // And in the emitted JSON itself: exactly type/path/scale_to_meters, path still a string.
    const auto j = scrt::io::write_document(reparsed);
    const auto& sj = j["scene"]["elements"][0]["surface"];
    CHECK(sj["type"].get<std::string>() == "mesh");
    REQUIRE(sj["path"].is_string());
    CHECK(sj["path"].get<std::string>() == kStlMeshPath);
    CHECK(sj["scale_to_meters"].get<double>() == doctest::Approx(0.001).epsilon(1e-15));
    CHECK(sj.size() == 3u);
}

// The same guarantee through the on-disk writer. Saving back into the document's own directory
// (old_base == new_base) must leave the mesh reference byte-for-byte untouched: rebasing a path
// that did not move would be a gratuitous diff in the user's file.
TEST_CASE("T16: save_scene into the same directory leaves the mesh path untouched") {
    const auto doc = scrt::io::parse_document(read_scene_json(repo_file(kStlScene)),
                                               /*strict=*/true);
    const auto dir = std::filesystem::temp_directory_path() / "scrt_t16_same_dir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto file = dir / "stl_scene.json";

    scrt::io::save_scene(doc, file);
    REQUIRE(std::filesystem::exists(file));

    const auto written = read_scene_json(file);
    const auto& sj = written["scene"]["elements"][0]["surface"];
    CHECK(sj["path"].get<std::string>() == kStlMeshPath);
    CHECK(sj["scale_to_meters"].get<double>() == doctest::Approx(0.001).epsilon(1e-15));

    std::filesystem::remove_all(dir);
}

// rebase_relative_path ---------------------------------------------------------

// The three contract cases from ScenePaths.hpp. Getting any of them wrong silently breaks every
// mesh reference in a Save-As, which only shows up as a load failure much later.
TEST_CASE("ScenePaths: rebase_relative_path honours its three documented cases") {
    const std::filesystem::path old_base = "C:/proj/scenes";
    const std::filesystem::path new_base = "C:/proj/out";

    // 1. An absolute stored path is not the scene directory's business - passed through.
    const std::string absolute = "C:/assets/meshes/dish.stl";
    CHECK(scrt::io::rebase_relative_path(absolute, old_base, new_base) == absolute);

    // 2. old_base == new_base is a no-op (this is what plain save_scene always does).
    const std::string relative = "../assets/meshes/dish.stl";
    CHECK(scrt::io::rebase_relative_path(relative, old_base, old_base) == relative);

    // 3. A real move: "../assets/..." from C:/proj/scenes resolves to C:/proj/assets/..., which
    //    from C:/proj/out is again "../assets/...". A sibling move keeps the same text, so use a
    //    destination one level deeper to prove the recomputation actually happens.
    const std::filesystem::path deeper = "C:/proj/out/nested";
    CHECK(scrt::io::rebase_relative_path(relative, old_base, deeper) ==
          "../../assets/meshes/dish.stl");

    // 4. An empty path stays empty rather than becoming ".".
    CHECK(scrt::io::rebase_relative_path("", old_base, deeper).empty());
}

// strict mode ------------------------------------------------------------------

// Strict mode exists so the editor can refuse to silently drop content it does not understand.
// A typo'd or future key must throw, not default - otherwise a save would quietly delete it.
TEST_CASE("SceneDocument: strict mode rejects an unrecognized key") {
    const char* body = R"({
      "scene": {
        "sun": { "direction": [0,0,-1], "dni_wm2": 1000.0,
                 "sunshape": {"type":"pillbox","half_angle_mrad":4.65} },
        "aperture": { "type":"disk", "center":[0,0,2], "normal":[0,0,1], "radius":1.0 },
        "materials": [ {"id":"m","type":"absorber"} ],
        "elements": [ { "name":"p", "material":"m", "reflectivity": 0.9,
                        "surface": {"type":"plane","half_width":1.0,"half_height":1.0} } ],
        "receiver": { "surface": {"type":"plane","half_width":0.05,"half_height":0.05},
                      "grid": {"nx":8,"ny":8} }
      }
    })";
    const auto j = nlohmann::json::parse(body);

    // Non-strict is the production load path and stays permissive.
    CHECK_NOTHROW(scrt::io::parse_document(j, /*strict=*/false));
    CHECK_THROWS_AS(scrt::io::parse_document(j, /*strict=*/true), std::runtime_error);

    // The same guard must fire on a material parameter the material type does not define.
    auto bad_material = j;
    bad_material["scene"]["materials"][0]["slope_error_mrad"] = 2.0;  // absorber has no params
    CHECK_THROWS_AS(scrt::io::parse_document(bad_material, /*strict=*/true), std::runtime_error);
}
