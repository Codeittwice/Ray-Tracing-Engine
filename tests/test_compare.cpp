#include <doctest/doctest.h>
#include "scrt/io/SceneLoader.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

namespace {

struct CompareRow {
    std::string scene;
    double      total_power_w        = 0.0;
    double      peak_flux_wm2        = 0.0;
    double      concentration_ratio  = 0.0;
    double      wall_time_s          = 0.0;
};

/// Run the compare pipeline on a scene and append a row to the CSV stream.
CompareRow run_scene(const std::filesystem::path& path, std::size_t n_rays,
                     std::ostream& csv_out) {
    scrt::io::LoadedScene ls = scrt::io::load_scene(path);
    ls.scene->build_acceleration_structure();
    ls.cfg.n_primary_rays = n_rays;
    ls.cfg.rng_seed       = 42;

    auto* recv = ls.scene->receiver();
    REQUIRE(recv != nullptr);
    const auto& ra = recv->accumulator();
    scrt::tracer::FluxAccumulator acc(ra.half_width(), ra.half_height(), ra.nx(), ra.ny());

    scrt::tracer::Tracer tracer(*ls.scene);
    auto result = tracer.run(ls.cfg, acc);

    double dni = ls.scene->sun() ? ls.scene->sun()->dni() : 1000.0;

    CompareRow row;
    row.scene               = path.string();
    row.total_power_w       = acc.total_power_w();
    row.peak_flux_wm2       = acc.peak_flux_wm2();
    row.concentration_ratio = acc.concentration_ratio(dni);
    row.wall_time_s         = result.wall_time_s;

    csv_out << row.scene               << ','
            << row.total_power_w       << ','
            << row.peak_flux_wm2       << ','
            << row.concentration_ratio << ','
            << row.wall_time_s         << '\n';
    return row;
}

} // namespace

// T_MC1 -------------------------------------------------------------------

TEST_CASE("T_MC1: compare pipeline writes CSV with header and one row per scene") {
    const char* names[] = {"parabolic_dish.json", "parabolic_trough.json"};

    const auto csv_path = std::filesystem::temp_directory_path() / "scrt_compare_test.csv";
    std::ofstream csv_out(csv_path);
    REQUIRE(csv_out.is_open());
    csv_out << "scene,total_power_w,peak_flux_wm2,concentration_ratio,wall_time_s\n";

    for (const char* name : names) {
        auto path = std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / name;
        REQUIRE(std::filesystem::exists(path));
        run_scene(path, 2000, csv_out);
    }
    csv_out.close();

    // Read back and validate structure
    std::ifstream csv_in(csv_path);
    REQUIRE(csv_in.is_open());

    std::string header;
    std::getline(csv_in, header);
    CHECK(header.find("scene")          != std::string::npos);
    CHECK(header.find("total_power_w")  != std::string::npos);
    CHECK(header.find("wall_time_s")    != std::string::npos);

    std::vector<std::string> rows;
    std::string line;
    while (std::getline(csv_in, line))
        if (!line.empty()) rows.push_back(line);
    csv_in.close();
    std::filesystem::remove(csv_path);

    // Two data rows
    REQUIRE(rows.size() == 2);

    // Correct scene names in first column
    CHECK(rows[0].find("parabolic_dish")   != std::string::npos);
    CHECK(rows[1].find("parabolic_trough") != std::string::npos);

    // Each row has exactly 4 commas (5 fields)
    for (const auto& r : rows) {
        int commas = 0;
        for (char c : r)
            if (c == ',') ++commas;
        CHECK(commas == 4);
    }
}

// T_MC2 -------------------------------------------------------------------

TEST_CASE("T_MC2: compare pipeline wall_time_s > 0 and primary_rays_traced matches config") {
    auto path = std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / "parabolic_dish.json";
    REQUIRE(std::filesystem::exists(path));

    scrt::io::LoadedScene ls = scrt::io::load_scene(path);
    ls.scene->build_acceleration_structure();
    ls.cfg.n_primary_rays = 500;
    ls.cfg.rng_seed       = 7;

    auto* recv = ls.scene->receiver();
    REQUIRE(recv != nullptr);
    const auto& ra = recv->accumulator();
    scrt::tracer::FluxAccumulator acc(ra.half_width(), ra.half_height(), ra.nx(), ra.ny());

    scrt::tracer::Tracer tracer(*ls.scene);
    auto result = tracer.run(ls.cfg, acc);

    CHECK(result.wall_time_s         > 0.0);
    CHECK(result.primary_rays_traced == 500);
    CHECK(acc.total_power_w()        >= 0.0);
    CHECK(acc.peak_flux_wm2()        >= 0.0);
}

TEST_CASE("T_MC2: concentration_ratio is peak_flux / dni (positive for working scene)") {
    auto path = std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / "parabolic_dish.json";
    REQUIRE(std::filesystem::exists(path));

    scrt::io::LoadedScene ls = scrt::io::load_scene(path);
    ls.scene->build_acceleration_structure();
    ls.cfg.n_primary_rays = 5000;
    ls.cfg.rng_seed       = 42;

    auto* recv = ls.scene->receiver();
    REQUIRE(recv != nullptr);
    const auto& ra = recv->accumulator();
    scrt::tracer::FluxAccumulator acc(ra.half_width(), ra.half_height(), ra.nx(), ra.ny());
    scrt::tracer::Tracer tracer(*ls.scene);
    tracer.run(ls.cfg, acc);

    double dni = ls.scene->sun()->dni();
    CHECK(acc.concentration_ratio(dni) > 1.0);  // parabolic dish must concentrate
}
