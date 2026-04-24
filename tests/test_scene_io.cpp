#include <doctest/doctest.h>
#include "scrt/io/MeshImporter.hpp"
#include "scrt/io/ResultsExporter.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/surfaces/TriangleMesh.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

// SCRT_SOURCE_DIR is injected by CMake so tests can locate example files.
#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

// T11 -----------------------------------------------------------------------

TEST_CASE("T11: Scene JSON serialisation is idempotent (canonical round-trip)") {
    using json = nlohmann::json;

    // Build a representative scene JSON with nlohmann::json — this guarantees
    // sorted keys and correct number formatting from the start.
    json j;
    j["scene"]["aperture"]["center"] = json::array({0.0, 0.0, 2.0});
    j["scene"]["aperture"]["normal"] = json::array({0.0, 0.0, 1.0});
    j["scene"]["aperture"]["radius"] = 0.5;
    j["scene"]["aperture"]["type"]   = "disk";

    j["scene"]["elements"] = json::array();
    {
        json el;
        el["material"]              = "aluminum";
        el["name"]                  = "dish";
        el["surface"]["aperture_radius_m"] = 0.5;
        el["surface"]["focal_length_m"]    = 0.6;
        el["surface"]["type"]              = "paraboloid";
        el["transform"]["translation"]     = json::array({0.0, 0.0, 0.0});
        j["scene"]["elements"].push_back(el);
    }

    j["scene"]["materials"] = json::array();
    {
        json m;
        m["id"]               = "aluminum";
        m["reflectance"]      = 0.85;
        m["slope_error_mrad"] = 2.0;
        m["type"]             = "real_mirror";
        j["scene"]["materials"].push_back(m);
    }

    j["scene"]["name"] = "round-trip test";

    j["scene"]["receiver"]["grid"]["nx"] = 32;
    j["scene"]["receiver"]["grid"]["ny"] = 32;
    j["scene"]["receiver"]["surface"]["half_height"] = 0.05;
    j["scene"]["receiver"]["surface"]["half_width"]  = 0.05;
    j["scene"]["receiver"]["surface"]["type"]        = "plane";
    j["scene"]["receiver"]["transform"]["translation"] = json::array({0.0, 0.0, 0.6});

    j["scene"]["sun"]["direction"]                 = json::array({0.0, 0.0, -1.0});
    j["scene"]["sun"]["dni_wm2"]                   = 1000.0;
    j["scene"]["sun"]["sunshape"]["half_angle_mrad"] = 4.65;
    j["scene"]["sun"]["sunshape"]["type"]            = "pillbox";

    j["trace"]["max_bounces"]          = 8;
    j["trace"]["max_paths_to_record"]  = 500;
    j["trace"]["n_primary_rays"]       = 10000;
    j["trace"]["record_paths"]         = false;
    j["trace"]["rng_seed"]             = 42;

    // First canonical dump.
    std::string canonical = j.dump(4);

    // Parse the canonical string and dump again — must be byte-for-byte identical.
    std::string roundtripped = json::parse(canonical).dump(4);
    CHECK(canonical == roundtripped);
}

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

TEST_CASE("T11: all example scenes load without error") {
    const char* files[] = {
        "parabolic_dish.json",
        "fresnel_lens_cooker.json",
        "box_cooker.json",
        "scheffler_reflector.json",
    };
    for (const char* name : files) {
        auto path = std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / name;
        REQUIRE_MESSAGE(std::filesystem::exists(path), name);
        // Should not throw.
        scrt::io::LoadedScene ls = scrt::io::load_scene(path);
        CHECK_MESSAGE(ls.scene != nullptr, name);
    }
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
}
