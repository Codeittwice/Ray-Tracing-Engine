#include <doctest/doctest.h>
#include "scrt/core/Transform.hpp"
#include "scrt/io/MeshImporter.hpp"
#include "scrt/io/ResultsExporter.hpp"
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/io/ScenePaths.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/surfaces/TriangleMesh.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <exception>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

// SCRT_SOURCE_DIR is injected by CMake so tests can locate example files.
#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

namespace {

/// Absolute path to a file inside the repository.
std::filesystem::path repo_file(const char* relative) {
    return std::filesystem::path(SCRT_SOURCE_DIR) / relative;
}

/// Reads a scene file exactly the way load_scene() does, comments tolerated.
nlohmann::json read_scene_json(const std::filesystem::path& path) {
    std::ifstream in(path);
    REQUIRE_MESSAGE(in.is_open(), path.string());
    return nlohmann::json::parse(in, nullptr, /*exceptions=*/true, /*ignore_comments=*/true);
}

/// Traces a loaded scene at the pinned deterministic configuration and sums total power over
/// every receiver face. Single-threaded so the RNG seeding reduces to exactly rng_seed and the
/// result is bit-reproducible, which is what makes an exact round-trip comparison meaningful.
double trace_total_power_w(scrt::io::LoadedScene& ls) {
    REQUIRE(ls.scene != nullptr);
    REQUIRE(ls.scene->receiver() != nullptr);
    ls.cfg.rng_seed       = 42;
    ls.cfg.num_threads    = 1;
    ls.cfg.n_primary_rays = 20000;
    ls.cfg.record_paths   = false;

    scrt::tracer::Tracer tracer(*ls.scene);
    tracer.run(ls.cfg);

    double total_w = 0.0;
    for (const auto& face : ls.scene->receiver()->faces())
        total_w += face->accumulator().total_power_w();
    return total_w;
}

/// Parses `src` into a SceneDocument, Save-As's it to `dst`, and loads the result.
///
/// save_scene_as (not save_scene) is the correct entry point here: mesh paths are stored relative
/// to the scene file's own directory, so writing into a temp directory moves the file away from
/// "../assets/meshes/...". save_scene's frozen signature has nowhere to put the original
/// directory and therefore cannot repair the reference; save_scene_as is told `old_base` and
/// rebases. Using it for non-mesh scenes too keeps this helper uniform - for them the rebase is
/// a no-op because there is no relative path to rewrite.
scrt::io::LoadedScene round_trip_scene(const std::filesystem::path& src,
                                       const std::filesystem::path& dst) {
    const auto doc = scrt::io::parse_document(read_scene_json(src));
    scrt::io::save_scene_as(doc, dst, src.parent_path());
    REQUIRE_MESSAGE(std::filesystem::exists(dst), dst.string());
    return scrt::io::load_scene(dst);
}

} // namespace

// T11 -----------------------------------------------------------------------
//
// The former "T11: Scene JSON serialisation is idempotent (canonical round-trip)" case was
// DELETED, not moved. It built a JSON object with nlohmann, dumped it, re-parsed it and dumped
// it again, then checked the two strings matched - i.e. it tested nlohmann::json against itself
// and never touched scrt code at all. It could not have failed for any product reason. Real
// round-trip coverage now lives in T13 (structural, whole corpus, in test_scene_document.cpp)
// and T14 (physics, below).

TEST_CASE("T11: parabolic_dish.json loads and produces positive receiver flux") {
    auto path = std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / "parabolic_dish.json";
    REQUIRE(std::filesystem::exists(path));

    scrt::io::LoadedScene ls = scrt::io::load_scene(path);
    REQUIRE(ls.scene != nullptr);
    REQUIRE(ls.scene->receiver() != nullptr);

    ls.cfg.n_primary_rays = 10000;
    ls.cfg.rng_seed       = 42;

    scrt::tracer::Tracer tracer(*ls.scene);
    auto& acc = ls.scene->receiver()->accumulator();
    tracer.run(ls.cfg, acc);

    CHECK(acc.total_power_w() > 0.0);
}

TEST_CASE("T11: fresnel_lens_cooker traces without crash") {
    auto path = std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / "fresnel_lens_cooker.json";
    REQUIRE(std::filesystem::exists(path));

    scrt::io::LoadedScene ls = scrt::io::load_scene(path);
    REQUIRE(ls.scene != nullptr);

    ls.cfg.n_primary_rays = 500;
    ls.cfg.rng_seed       = 42;
    ls.cfg.num_threads    = 1;  // serial for determinism

    const auto& ra = ls.scene->receiver()->accumulator();
    scrt::tracer::FluxAccumulator acc(ra.half_width(), ra.half_height(), ra.nx(), ra.ny());
    scrt::tracer::Tracer tracer(*ls.scene);
    auto result = tracer.run(ls.cfg, acc);

    CHECK(result.primary_rays_traced == 500);
    CHECK(acc.total_power_w() >= 0.0);
}

// "T11: all example scenes load without error" was superseded by the runtime
// std::filesystem sweep in tests/test_regression_corpus.cpp, which covers the
// whole committed scene corpus rather than four hardcoded filenames.

TEST_CASE("T11: box receiver scene creates proportional receiver faces") {
    auto path = std::filesystem::path(SCRT_SOURCE_DIR) / "examples" /
                "stl_rectangular_3floors_606570deg_box.json";
    REQUIRE(std::filesystem::exists(path));

    scrt::io::LoadedScene ls = scrt::io::load_scene(path);
    auto* recv = ls.scene->receiver();
    REQUIRE(recv != nullptr);
    REQUIRE(recv->faces().size() == 6);

    CHECK(recv->faces()[0]->name() == "glass_top");
    CHECK(recv->faces()[0]->accumulator().nx() == 64);
    CHECK(recv->faces()[0]->accumulator().ny() == 64);
    CHECK(recv->faces()[1]->name() == "bottom");
    CHECK(recv->faces()[1]->accumulator().nx() == 64);
    CHECK(recv->faces()[1]->accumulator().ny() == 64);
    CHECK(recv->faces()[2]->name() == "north_wall");
    CHECK(recv->faces()[2]->accumulator().nx() == 64);
    CHECK(recv->faces()[2]->accumulator().ny() == 32);
    CHECK(recv->faces()[4]->name() == "east_wall");
    CHECK(recv->faces()[4]->accumulator().nx() == 32);
    CHECK(recv->faces()[4]->accumulator().ny() == 64);
}

TEST_CASE("T11: box receiver can add an internal battery receiver face") {
    nlohmann::json j;
    j["scene"]["name"] = "box_with_battery";
    j["scene"]["materials"] = nlohmann::json::array();
    j["scene"]["sun"]["direction"] = {0.0, 0.0, -1.0};
    j["scene"]["sun"]["dni_wm2"] = 1000.0;
    j["scene"]["sun"]["sunshape"]["type"] = "pillbox";
    j["scene"]["sun"]["sunshape"]["half_angle_mrad"] = 4.65;
    j["scene"]["aperture"]["type"] = "disk";
    j["scene"]["aperture"]["center"] = {0.0, 0.0, 1.0};
    j["scene"]["aperture"]["normal"] = {0.0, 0.0, 1.0};
    j["scene"]["aperture"]["radius"] = 0.5;
    j["scene"]["receiver"]["type"] = "box";
    j["scene"]["receiver"]["surface"]["type"] = "plane";
    j["scene"]["receiver"]["surface"]["half_width"] = 0.15;
    j["scene"]["receiver"]["surface"]["half_height"] = 0.15;
    j["scene"]["receiver"]["depth"] = 0.15;
    j["scene"]["receiver"]["battery"]["top_depth_m"] = 0.075;
    j["scene"]["receiver"]["battery"]["height_m"] = 0.05;
    j["scene"]["receiver"]["battery"]["half_width"] = 0.075;
    j["scene"]["receiver"]["battery"]["half_height"] = 0.05;
    j["scene"]["receiver"]["battery"]["nx"] = 32;
    j["scene"]["receiver"]["battery"]["ny"] = 24;

    const auto path = std::filesystem::temp_directory_path() / "scrt_box_with_battery.json";
    {
        std::ofstream f(path);
        REQUIRE(f.is_open());
        f << j.dump(4) << '\n';
    }

    scrt::io::LoadedScene ls = scrt::io::load_scene(path);
    auto* recv = ls.scene->receiver();
    REQUIRE(recv != nullptr);
    REQUIRE(recv->faces().size() == 11);
    CHECK(recv->faces()[6]->name() == "battery_top");
    CHECK(recv->faces()[6]->accumulator().half_width() == doctest::Approx(0.075));
    CHECK(recv->faces()[6]->accumulator().half_height() == doctest::Approx(0.05));
    CHECK(recv->faces()[6]->accumulator().nx() == 32);
    CHECK(recv->faces()[6]->accumulator().ny() == 24);
    CHECK(recv->faces()[7]->name() == "battery_north_wall");
    CHECK(recv->faces()[8]->name() == "battery_south_wall");
    CHECK(recv->faces()[9]->name() == "battery_east_wall");
    CHECK(recv->faces()[10]->name() == "battery_west_wall");
    CHECK(recv->faces()[7]->accumulator().half_height() == doctest::Approx(0.025));
    CHECK(recv->faces()[9]->accumulator().half_width() == doctest::Approx(0.025));

    std::filesystem::remove(path);
}

TEST_CASE("T11: export_flux_csv writes a readable file") {
    // Build minimal scene, trace, export.
    auto path = std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / "parabolic_dish.json";
    REQUIRE(std::filesystem::exists(path));

    scrt::io::LoadedScene ls = scrt::io::load_scene(path);
    ls.cfg.n_primary_rays = 5000;
    ls.cfg.rng_seed       = 1;

    scrt::tracer::Tracer tracer(*ls.scene);
    auto& acc = ls.scene->receiver()->accumulator();
    auto  res = tracer.run(ls.cfg, acc);

    const auto csv_path = std::filesystem::temp_directory_path() / "scrt_test_flux.csv";
    scrt::io::export_flux_csv(acc, csv_path);
    CHECK(std::filesystem::exists(csv_path));
    CHECK(std::filesystem::file_size(csv_path) > 0);
    std::filesystem::remove(csv_path);

    const auto npy_path = std::filesystem::temp_directory_path() / "scrt_test_flux.npy";
    scrt::io::export_flux_npy(acc, npy_path);
    CHECK(std::filesystem::exists(npy_path));
    CHECK(std::filesystem::file_size(npy_path) > 0);
    std::filesystem::remove(npy_path);

    const auto json_path = std::filesystem::temp_directory_path() / "scrt_test_summary.json";
    scrt::io::export_summary_json(acc, res, 1000.0, json_path);
    CHECK(std::filesystem::exists(json_path));
    std::filesystem::remove(json_path);
}

// T13/T14/T15 ---------------------------------------------------------------

// Physics round-trip. T13 proves the JSON is structurally stable, but structure can survive
// while MEANING drifts - a mirror rotated into a different Euler convention, a scale dropped, a
// mesh silently re-imported at 1000x. Only re-tracing catches that. Load -> save -> reload ->
// trace both at a pinned seed and ray count; the two total receiver powers must agree to within
// double round-off, because the two scene graphs should be identical, not merely similar.
TEST_CASE("T14: a save/reload round trip reproduces receiver power exactly") {
    const char* scenes[] = {
        "examples/parabolic_dish.json",                        // analytic paraboloid, plane receiver
        "examples/box_cooker.json",                            // flat-plate reflectors, plane receiver
        "examples/stl_rectangular_3floors_606570deg_box.json",  // STL mesh + 6-face box receiver
        "finalized_designs/scenes/two_level_final_double_pmma.json",  // meshes + battery + Buie sun
    };

    const auto tmp = std::filesystem::temp_directory_path() / "scrt_t14_roundtrip";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);

    for (const char* relative : scenes) {
        const auto src = repo_file(relative);
        REQUIRE_MESSAGE(std::filesystem::exists(src), src.string());

        scrt::io::LoadedScene original = scrt::io::load_scene(src);
        const double before = trace_total_power_w(original);

        scrt::io::LoadedScene reloaded = round_trip_scene(src, tmp / src.filename());
        const double after = trace_total_power_w(reloaded);

        // Build messages eagerly: doctest's CHECK_MESSAGE binds its stream operator tighter
        // than '+', so inline concatenation will not compile.
        std::ostringstream os;
        os.precision(17);
        os << relative << ": " << before << " W before, " << after << " W after";
        const std::string msg = os.str();

        // A zero-power scene would make the equality check vacuous.
        CHECK_MESSAGE(before > 0.0, msg);
        CHECK_MESSAGE(after == doctest::Approx(before).epsilon(1e-12), msg);
    }

    std::filesystem::remove_all(tmp);
}

// The documented limitation behind T14's choice of save_scene_as, pinned so it cannot regress
// unnoticed in either direction. Plain save_scene(doc, path) is defined as old_base ==
// new_base == path.parent_path(), so a Save-As into a different directory writes the mesh
// reference through unchanged and it no longer resolves. If save_scene ever grows the ability
// to rebase, this test fails and the Wave 3 editor can stop threading old_base around.
TEST_CASE("T14: plain save_scene cannot relocate a mesh scene to another directory") {
    const auto src = repo_file("examples/stl_rectangular_3floors_606570deg_box.json");
    REQUIRE(std::filesystem::exists(src));

    const auto tmp = std::filesystem::temp_directory_path() / "scrt_t14_save_scene_only";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    const auto dst = tmp / src.filename();

    const auto doc = scrt::io::parse_document(read_scene_json(src));
    scrt::io::save_scene(doc, dst);
    REQUIRE(std::filesystem::exists(dst));

    // The stored path is written through verbatim...
    const auto written = read_scene_json(dst);
    CHECK(written["scene"]["elements"][0]["surface"]["path"].get<std::string>() ==
          "../assets/meshes/panels_only_rectangular_3floors_606570deg.stl");
    // ...and therefore no longer resolves from the new directory.
    CHECK_THROWS_AS(scrt::io::load_scene(dst), std::runtime_error);

    std::filesystem::remove_all(tmp);
}

// A box receiver is assembled procedurally by build_scene() from half-extents, depth, grid and
// an optional battery inset - none of the faces exist as JSON. So a dropped or mis-typed key
// does not produce a load error, it produces a receiver with the wrong number of faces and a
// flux total that looks plausible. Assert the names, not just the count, in both the plain
// 6-face form and the 11-face battery form.
TEST_CASE("T15: box receiver faces survive a save/reload round trip") {
    const auto tmp = std::filesystem::temp_directory_path() / "scrt_t15_box_faces";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);

    SUBCASE("6-face box, no battery") {
        const auto src = repo_file("examples/stl_rectangular_3floors_606570deg_box.json");
        REQUIRE(std::filesystem::exists(src));
        scrt::io::LoadedScene ls = round_trip_scene(src, tmp / src.filename());

        const auto* recv = ls.scene->receiver();
        REQUIRE(recv != nullptr);
        REQUIRE(recv->faces().size() == 6u);
        CHECK(recv->faces()[0]->name() == "glass_top");
        CHECK(recv->faces()[1]->name() == "bottom");
        CHECK(recv->faces()[2]->name() == "north_wall");
        CHECK(recv->faces()[3]->name() == "south_wall");
        CHECK(recv->faces()[4]->name() == "east_wall");
        CHECK(recv->faces()[5]->name() == "west_wall");
    }

    SUBCASE("11-face box with a battery inset") {
        const auto src = repo_file("finalized_designs/scenes/two_level_final_double_pmma.json");
        REQUIRE(std::filesystem::exists(src));
        scrt::io::LoadedScene ls = round_trip_scene(src, tmp / src.filename());

        const auto* recv = ls.scene->receiver();
        REQUIRE(recv != nullptr);
        REQUIRE(recv->faces().size() == 11u);
        CHECK(recv->faces()[0]->name() == "glass_top");
        CHECK(recv->faces()[1]->name() == "bottom");
        CHECK(recv->faces()[2]->name() == "north_wall");
        CHECK(recv->faces()[3]->name() == "south_wall");
        CHECK(recv->faces()[4]->name() == "east_wall");
        CHECK(recv->faces()[5]->name() == "west_wall");
        CHECK(recv->faces()[6]->name() == "battery_top");
        CHECK(recv->faces()[7]->name() == "battery_north_wall");
        CHECK(recv->faces()[8]->name() == "battery_south_wall");
        CHECK(recv->faces()[9]->name() == "battery_east_wall");
        CHECK(recv->faces()[10]->name() == "battery_west_wall");

        // The battery geometry itself must come back with the authored dimensions, not the
        // half-of-the-box fallbacks parse_document uses when the keys are absent.
        CHECK(recv->faces()[6]->accumulator().half_width() == doctest::Approx(0.075));
        CHECK(recv->faces()[6]->accumulator().half_height() == doctest::Approx(0.075));
        CHECK(recv->faces()[6]->accumulator().nx() == 64);
        CHECK(recv->faces()[6]->accumulator().ny() == 64);
    }

    std::filesystem::remove_all(tmp);
}

// T12 -----------------------------------------------------------------------

TEST_CASE("T12: TriangleMesh import with scale_to_meters, world_bounds correct") {
    // Write a minimal unit-cube OBJ to a temp file.
    // Vertices span [0,1]^3 → with scale=0.001, world span = 0.001 m.
    const auto obj_path = std::filesystem::temp_directory_path() / "scrt_test_cube.obj";
    {
        std::ofstream f(obj_path);
        REQUIRE(f.is_open());
        // 8 vertices of a unit cube
        f << "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\n";
        f << "v 0 0 1\nv 1 0 1\nv 0 1 1\nv 1 1 1\n";
        // 12 triangles (2 per face, 6 faces)
        f << "f 1 2 4\nf 1 4 3\n"; // bottom
        f << "f 5 7 8\nf 5 8 6\n"; // top
        f << "f 1 5 6\nf 1 6 2\n"; // front
        f << "f 3 4 8\nf 3 8 7\n"; // back
        f << "f 1 3 7\nf 1 7 5\n"; // left
        f << "f 2 6 8\nf 2 8 4\n"; // right
    }

    constexpr double kScale = 0.001;
    scrt::io::ImportedMesh imp = scrt::io::import_mesh(obj_path, kScale);
    std::filesystem::remove(obj_path);

    // Assimp may split vertices when generating per-face normals, so vertex count ≥ 8.
    REQUIRE(imp.vertices.size() >= 8u);
    REQUIRE(imp.indices.size()  % 3 == 0);
    REQUIRE(imp.indices.size()  >= 36u); // at least 12 triangles

    scrt::surfaces::TriangleMesh mesh(std::move(imp.vertices), std::move(imp.indices));

    scrt::core::AABB bounds = mesh.world_bounds();
    double span_x = static_cast<double>(bounds.max().x - bounds.min().x);
    double span_y = static_cast<double>(bounds.max().y - bounds.min().y);
    double span_z = static_cast<double>(bounds.max().z - bounds.min().z);

    CHECK(span_x == doctest::Approx(kScale).epsilon(1e-9));
    CHECK(span_y == doctest::Approx(kScale).epsilon(1e-9));
    CHECK(span_z == doctest::Approx(kScale).epsilon(1e-9));

    mesh.set_transform(scrt::core::Transform::from_translation({1.0, -2.0, 0.5}));
    scrt::core::AABB moved = mesh.world_bounds();
    CHECK(moved.min().x == doctest::Approx(1.0).epsilon(1e-9));
    CHECK(moved.min().y == doctest::Approx(-2.0).epsilon(1e-9));
    CHECK(moved.min().z == doctest::Approx(0.5).epsilon(1e-9));
    CHECK(moved.max().x == doctest::Approx(1.0 + kScale).epsilon(1e-9));
    CHECK(moved.max().y == doctest::Approx(-2.0 + kScale).epsilon(1e-9));
    CHECK(moved.max().z == doctest::Approx(0.5 + kScale).epsilon(1e-9));
}
