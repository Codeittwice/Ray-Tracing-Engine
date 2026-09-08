#include <doctest/doctest.h>
#include "scrt/io/SceneLoader.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

// SCRT_SOURCE_DIR is injected by CMake so tests can locate example files.
#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

namespace {

/// Load a scene, trace it deterministically, and sum total power over all receiver faces.
double trace_total_power_w(const std::filesystem::path& scene_path,
                           std::size_t n_primary_rays) {
    REQUIRE_MESSAGE(std::filesystem::exists(scene_path), scene_path.string());

    scrt::io::LoadedScene ls = scrt::io::load_scene(scene_path);
    REQUIRE_MESSAGE(ls.scene != nullptr, scene_path.string());
    REQUIRE_MESSAGE(ls.scene->receiver() != nullptr, scene_path.string());

    // Fixed trace configuration: single-threaded so the RNG seeding reduces to
    // exactly rng_seed and the result is bit-reproducible across machines.
    ls.cfg.rng_seed       = 42;
    ls.cfg.num_threads    = 1;
    ls.cfg.n_primary_rays = n_primary_rays;
    ls.cfg.record_paths   = false;

    // The no-accumulator overload clears and fills the receiver's own face
    // accumulators, so this one path covers single-face and box receivers alike.
    scrt::tracer::Tracer tracer(*ls.scene);
    tracer.run(ls.cfg);

    double total_w = 0.0;
    for (const auto& face : ls.scene->receiver()->faces())
        total_w += face->accumulator().total_power_w();
    return total_w;
}

/// Absolute path to a scene file inside the repository.
std::filesystem::path scene_file(const std::string& relative) {
    return std::filesystem::path(SCRT_SOURCE_DIR) / relative;
}

} // namespace

// Golden flux baseline --------------------------------------------------------
//
// The expected values below are RECORDED BASELINE VALUES, NOT ANALYTIC
// PREDICTIONS. They were measured by running the tracer at the exact
// configuration pinned in trace_total_power_w() (rng_seed = 42,
// num_threads = 1, record_paths = false, ray counts as given per case) and
// pasting the observed sums back in. They exist to lock down current physics
// behaviour: if one of these checks fails, the optical model changed and that
// is a regression to be explained, not a number to be re-baselined casually.

TEST_CASE("Golden flux: parabolic_dish.json total receiver power") {
    const double total_w = trace_total_power_w(scene_file("examples/parabolic_dish.json"), 20000);
    CHECK(total_w == doctest::Approx(663.88135955660175).epsilon(1e-9));
}

TEST_CASE("Golden flux: box_cooker.json total receiver power") {
    const double total_w = trace_total_power_w(scene_file("examples/box_cooker.json"), 20000);
    CHECK(total_w == doctest::Approx(191.79550384668289).epsilon(1e-9));
}

TEST_CASE("Golden flux: fresnel_lens_cooker.json total receiver power") {
    const double total_w =
        trace_total_power_w(scene_file("examples/fresnel_lens_cooker.json"), 20000);
    CHECK(total_w == doctest::Approx(38.867692665554998).epsilon(1e-9));
}

TEST_CASE("Golden flux: stl_rectangular_3floors_606570deg_box.json total receiver power") {
    const double total_w = trace_total_power_w(
        scene_file("examples/stl_rectangular_3floors_606570deg_box.json"), 20000);
    CHECK(total_w == doctest::Approx(874.83793179836209).epsilon(1e-9));
}

// Regression corpus -----------------------------------------------------------

TEST_CASE("Regression corpus: every bundled scene loads without error") {
    // Non-recursive sweep of the four directories that hold committed scenes.
    // Deliberately excludes dist/solar-cooker-rt/examples/, which is a packaged
    // copy and not part of the source corpus.
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

    // Guard against the sweep silently finding nothing (e.g. a bad SCRT_SOURCE_DIR).
    // Lower bound, not equality: the Python sweep scripts generate scenes directly
    // into results/compact_study/scenes/, so adding one must not fail this test.
    CHECK(scenes.size() >= 94u);

    for (const auto& path : scenes) {
        const std::string name = path.string();
        scrt::io::LoadedScene ls;
        bool threw = false;
        std::string what;
        try {
            ls = scrt::io::load_scene(path);
        } catch (const std::exception& e) {
            threw = true;
            what  = e.what();
        }
        // Build the message eagerly: doctest's CHECK_MESSAGE binds its stream
        // operator tighter than '+', so inline concatenation will not compile.
        const std::string threw_msg = name + " threw: " + what;
        const std::string null_msg  = name + " loaded a null scene";
        CHECK_MESSAGE(!threw, threw_msg);
        if (!threw)
            CHECK_MESSAGE(ls.scene != nullptr, null_msg);
    }
}
