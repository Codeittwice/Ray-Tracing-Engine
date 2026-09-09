#include <doctest/doctest.h>
#include "scrt/io/MeshImporter.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// SCRT_SOURCE_DIR is injected by CMake so tests can locate example files.
#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

namespace {

/// Directory holding the repository's real (millimetre-authored) STL assets.
std::filesystem::path meshes_dir() {
    return std::filesystem::path(SCRT_SOURCE_DIR) / "assets" / "meshes";
}

/// Every .stl under assets/meshes, sorted so the reported order is stable.
std::vector<std::filesystem::path> asset_stls() {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(meshes_dir(), ec))
        if (e.path().extension() == ".stl") out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

/// Writes a two-submesh OBJ (two unit cubes, one at the origin and one offset) and returns it.
/// Two `o` groups make Assimp emit two aiMeshes, which is what the split path needs.
std::filesystem::path write_two_part_obj() {
    const auto path = std::filesystem::temp_directory_path() / "scrt_two_part.obj";
    std::ofstream f(path);
    REQUIRE(f.is_open());
    auto cube = [&f](const char* name, int base, double dx) {
        f << "o " << name << "\n";
        for (int i = 0; i < 8; ++i)
            f << "v " << ((i & 1) ? 1.0 : 0.0) + dx << " " << ((i & 2) ? 1.0 : 0.0) << " "
              << ((i & 4) ? 1.0 : 0.0) << "\n";
        // 12 triangles over the 8 vertices (winding is irrelevant to bounds/counts).
        const int tri[12][3] = {{0, 1, 3}, {0, 3, 2}, {4, 6, 7}, {4, 7, 5},
                                {0, 4, 5}, {0, 5, 1}, {2, 3, 7}, {2, 7, 6},
                                {0, 2, 6}, {0, 6, 4}, {1, 5, 7}, {1, 7, 3}};
        for (const auto& t : tri)
            f << "f " << base + t[0] << " " << base + t[1] << " " << base + t[2] << "\n";
    };
    cube("part_a", 1, 0.0);
    cube("part_b", 9, 10.0);
    return path;
}

} // namespace

// ---- Unit detection ---------------------------------------------------------------------

TEST_CASE("Mesh unit heuristic: thresholds and scale factors") {
    using scrt::io::MeshUnit;

    CHECK(scrt::io::guess_mesh_unit(1000.0) == MeshUnit::Millimeters);
    CHECK(scrt::io::guess_mesh_unit(100.001) == MeshUnit::Millimeters);
    CHECK(scrt::io::guess_mesh_unit(100.0) == MeshUnit::Centimeters);  // strict >
    CHECK(scrt::io::guess_mesh_unit(3.001) == MeshUnit::Centimeters);
    CHECK(scrt::io::guess_mesh_unit(3.0) == MeshUnit::Meters);         // strict >
    CHECK(scrt::io::guess_mesh_unit(0.62) == MeshUnit::Meters);
    CHECK(scrt::io::guess_mesh_unit(0.0) == MeshUnit::Meters);

    CHECK(scrt::io::unit_scale_to_meters(MeshUnit::Millimeters) == doctest::Approx(1e-3));
    CHECK(scrt::io::unit_scale_to_meters(MeshUnit::Centimeters) == doctest::Approx(1e-2));
    CHECK(scrt::io::unit_scale_to_meters(MeshUnit::Meters) == doctest::Approx(1.0));
}

TEST_CASE("Mesh unit heuristic: every shipped STL is detected as millimetres") {
    const auto files = asset_stls();
    REQUIRE(files.size() > 0u);

    for (const auto& p : files) {
        scrt::io::MeshFileInfo info = scrt::io::inspect_mesh_file(p);
        const auto ext = info.bounds.max() - info.bounds.min();
        const double s = scrt::io::unit_scale_to_meters(info.guessed_unit);

        MESSAGE(p.filename().string() << ": diagonal " << info.raw_diagonal << " raw, "
                << info.parts.size() << " submesh(es), " << info.triangle_count
                << " tris, raw extent " << ext.x << " x " << ext.y << " x " << ext.z
                << " -> guessed " << std::string(scrt::io::mesh_unit_label(info.guessed_unit))
                << " -> " << ext.x * s << " x " << ext.y * s << " x " << ext.z * s << " m");

        // Every scene in this repo imports these with scale_to_meters 0.001.
        CHECK_MESSAGE(info.guessed_unit == scrt::io::MeshUnit::Millimeters,
                      p.filename().string());
        CHECK(info.triangle_count > 0u);
        CHECK(info.raw_diagonal > 0.0);
    }
}

// ---- Inspection -------------------------------------------------------------------------

TEST_CASE("inspect_mesh_file reports submeshes, counts and raw bounds") {
    const auto path = write_two_part_obj();
    scrt::io::MeshFileInfo info = scrt::io::inspect_mesh_file(path);

    CHECK(info.parts.size() == 2u);
    CHECK(info.triangle_count == 24u);
    CHECK(info.parts[0].triangle_count == 12u);
    CHECK(info.parts[1].triangle_count == 12u);
    CHECK(info.parts[0].index == 0u);
    CHECK(info.parts[1].index == 1u);

    // Merged bounds span both cubes: x in [0, 11], y and z in [0, 1].
    CHECK(info.bounds.min().x == doctest::Approx(0.0));
    CHECK(info.bounds.max().x == doctest::Approx(11.0));
    CHECK(info.bounds.max().y == doctest::Approx(1.0));
    // Per-part bounds are the individual cubes.
    CHECK(info.parts[1].bounds.min().x == doctest::Approx(10.0));

    // Diagonal ~11.05 -> above 3, below 100 -> centimetres.
    CHECK(info.guessed_unit == scrt::io::MeshUnit::Centimeters);

    std::filesystem::remove(path);
}

TEST_CASE("inspect_mesh_file throws on an unreadable file") {
    CHECK_THROWS_AS(scrt::io::inspect_mesh_file("no_such_mesh_file.stl"), std::runtime_error);
}

// ---- Submesh import ---------------------------------------------------------------------

TEST_CASE("import_submesh yields one object per submesh; merged is the concatenation") {
    const auto path = write_two_part_obj();

    scrt::io::ImportedMesh merged = scrt::io::import_mesh(path, 1.0);
    scrt::io::ImportedMesh a      = scrt::io::import_submesh(path, 0, 1.0);
    scrt::io::ImportedMesh b      = scrt::io::import_submesh(path, 1, 1.0);

    CHECK(a.indices.size() == 36u);
    CHECK(b.indices.size() == 36u);
    CHECK(merged.indices.size() == a.indices.size() + b.indices.size());
    CHECK(merged.vertices.size() == a.vertices.size() + b.vertices.size());

    // Part 0 is the cube at the origin, part 1 the cube offset by +10 in x.
    auto max_x = [](const scrt::io::ImportedMesh& m) {
        double v = -1e30;
        for (const auto& p : m.vertices) v = std::max(v, p.x);
        return v;
    };
    CHECK(max_x(a) == doctest::Approx(1.0));
    CHECK(max_x(b) == doctest::Approx(11.0));

    CHECK_THROWS_AS(scrt::io::import_submesh(path, 2, 1.0), std::runtime_error);

    std::filesystem::remove(path);
}

TEST_CASE("import_submesh on a single-mesh STL equals the merged import") {
    const auto files = asset_stls();
    REQUIRE(files.size() > 0u);
    const auto& path = files.front();

    scrt::io::MeshFileInfo info = scrt::io::inspect_mesh_file(path);
    REQUIRE(info.parts.size() == 1u);  // STL is a single triangle soup

    scrt::io::ImportedMesh merged = scrt::io::import_mesh(path, 1e-3);
    scrt::io::ImportedMesh only   = scrt::io::import_submesh(path, 0, 1e-3);

    CHECK(only.vertices.size() == merged.vertices.size());
    CHECK(only.indices.size() == merged.indices.size());
    for (std::size_t i = 0; i < merged.vertices.size(); ++i)
        CHECK(only.vertices[i].x == doctest::Approx(merged.vertices[i].x));
}

TEST_CASE("Merged and per-submesh imports occupy distinct cache slots") {
    const auto path = write_two_part_obj();
    scrt::io::clear_mesh_cache();

    const auto& m1 = scrt::io::import_mesh_cached(path, 1.0);
    const auto& s1 = scrt::io::import_submesh_cached(path, 0, 1.0);
    CHECK(scrt::io::mesh_cache_stats().misses == 2u);
    CHECK(m1.indices.size() != s1.indices.size());  // different slots, different content

    const auto& m2 = scrt::io::import_mesh_cached(path, 1.0);
    const auto& s2 = scrt::io::import_submesh_cached(path, 0, 1.0);
    CHECK(scrt::io::mesh_cache_stats().hits == 2u);
    CHECK(&m1 == &m2);
    CHECK(&s1 == &s2);

    scrt::io::clear_mesh_cache();
    std::filesystem::remove(path);
}

// ---- Placement and path handling --------------------------------------------------------

TEST_CASE("mesh_centering_translation puts the scaled mesh centre on the target") {
    // Raw bounds [0,1000]^3 in millimetres -> scaled centre at (0.5, 0.5, 0.5) m.
    const scrt::core::AABB raw{scrt::math::vec3(0.0), scrt::math::vec3(1000.0)};
    const scrt::math::vec3 target{2.0, -1.0, 0.25};

    const scrt::math::vec3 t = scrt::io::mesh_centering_translation(raw, 1e-3, target);
    CHECK(t.x == doctest::Approx(1.5));
    CHECK(t.y == doctest::Approx(-1.5));
    CHECK(t.z == doctest::Approx(-0.25));

    // Applying it lands the scaled centroid exactly on the target.
    const scrt::math::vec3 placed = raw.centroid() * 1e-3 + t;
    CHECK(placed.x == doctest::Approx(target.x));
    CHECK(placed.y == doctest::Approx(target.y));
    CHECK(placed.z == doctest::Approx(target.z));
}

TEST_CASE("scene_relative_mesh_path keeps a scene-relative path") {
    // The shape every shipped scene uses: scene in examples/, mesh in assets/meshes/.
    const std::filesystem::path root  = std::filesystem::path(SCRT_SOURCE_DIR);
    const std::filesystem::path scene = root / "examples";
    const std::filesystem::path mesh  = root / "assets" / "meshes" / "x.stl";

    const std::string rel = scrt::io::scene_relative_mesh_path(mesh, scene);
    CHECK(rel == "../assets/meshes/x.stl");

    // Resolving it against the scene directory reproduces the original file.
    CHECK(std::filesystem::weakly_canonical(scene / rel) ==
          std::filesystem::weakly_canonical(mesh));

    // A mesh inside the scene directory stays a plain relative name (no leading "./").
    CHECK(scrt::io::scene_relative_mesh_path(scene / "sub" / "y.stl", scene) == "sub/y.stl");
}
