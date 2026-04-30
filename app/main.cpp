#include "scrt/io/ResultsExporter.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include "scrt/viz/Viewer.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

// Returns the directory that contains this executable.
static std::filesystem::path exe_dir(const char* argv0) {
    return std::filesystem::absolute(argv0).parent_path();
}

// If no scene argument was given, scan <exe_dir>/examples/ and return the
// first .json found (alphabetical order).  Returns empty path if none found.
static std::filesystem::path find_default_scene(const std::filesystem::path& examples) {
    if (!std::filesystem::exists(examples)) return {};
    std::vector<std::filesystem::path> hits;
    for (const auto& e : std::filesystem::directory_iterator(examples))
        if (e.path().extension() == ".json")
            hits.push_back(e.path());
    if (hits.empty()) return {};
    std::sort(hits.begin(), hits.end());
    return hits.front();
}

int main(int argc, char* argv[]) {
    const auto base_dir      = exe_dir(argv[0]);
    const auto default_exdir = base_dir / "examples";

    std::filesystem::path scene_path;
    bool                  headless        = false;
    std::size_t           n_rays_override = 0;
    std::filesystem::path out_dir         = ".";
    std::filesystem::path examples_dir    = default_exdir;

    if (argc >= 2 && argv[1][0] != '-') {
        scene_path = argv[1];
    }

    for (int i = (scene_path.empty() ? 1 : 2); i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--rays" && i + 1 < argc) {
            n_rays_override = std::stoull(argv[++i]);
        } else if (arg == "--out" && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (arg == "--examples-dir" && i + 1 < argc) {
            examples_dir = argv[++i];
        }
    }

    // No scene specified — auto-discover from examples/ next to the exe.
    if (scene_path.empty()) {
        scene_path = find_default_scene(examples_dir);
        if (scene_path.empty()) {
            std::cerr << "Usage: scrt_app scene.json [--headless] [--rays N] [--out dir/]\n"
                      << "       Or place .json scenes in an 'examples' folder next to the executable\n"
                      << "       and run scrt_app with no arguments.\n";
            return 1;
        }
        std::cout << "Loading default scene: " << scene_path.filename().string() << "\n";
    }

    scrt::io::LoadedScene ls;
    try {
        ls = scrt::io::load_scene(scene_path);
    } catch (const std::exception& e) {
        std::cerr << "Error loading scene: " << e.what() << '\n';
        return 1;
    }

    ls.scene->build_acceleration_structure();

    if (n_rays_override > 0)
        ls.cfg.n_primary_rays = n_rays_override;

    if (headless) {
        auto* recv = ls.scene->receiver();
        if (!recv) {
            std::cerr << "Scene has no receiver — nothing to trace.\n";
            return 1;
        }
        const auto& ra = recv->accumulator();
        scrt::tracer::FluxAccumulator acc(ra.half_width(), ra.half_height(), ra.nx(), ra.ny());

        scrt::tracer::Tracer tracer(*ls.scene);
        auto result = tracer.run(ls.cfg, acc);

        std::filesystem::create_directories(out_dir);
        double dni = ls.scene->sun() ? ls.scene->sun()->dni() : 1000.0;
        scrt::io::export_flux_csv(acc, (out_dir / "flux.csv").string());
        scrt::io::export_summary_json(acc, result, dni, (out_dir / "summary.json").string());

        std::cout << "Total power    : " << acc.total_power_w()          << " W\n";
        std::cout << "Peak flux      : " << acc.peak_flux_wm2()          << " W/m2\n";
        std::cout << "Concentration  : " << acc.concentration_ratio(dni) << "x\n";
        std::cout << "Wall time      : " << result.wall_time_s            << " s\n";
        std::cout << "Results written to " << out_dir.string()            << '\n';
        return 0;
    }

    // Interactive Polyscope viewer
    ls.cfg.record_paths        = true;
    ls.cfg.max_paths_to_record = 200;

    scrt::viz::Viewer viewer;
    viewer.set_scene(ls.scene.get());
    viewer.set_config(ls.cfg);
    viewer.set_examples_dir(examples_dir);
    viewer.run();

    return 0;
}
