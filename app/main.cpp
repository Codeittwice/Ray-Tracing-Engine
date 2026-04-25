#include "scrt/io/ResultsExporter.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include "scrt/viz/Viewer.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: scrt_app scene.json [--headless] [--rays N] [--out dir/]\n";
        return 1;
    }

    std::filesystem::path scene_path = argv[1];
    bool                  headless   = false;
    std::size_t           n_rays_override = 0; // 0 = use scene default
    std::filesystem::path out_dir    = ".";

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--rays" && i + 1 < argc) {
            n_rays_override = std::stoull(argv[++i]);
        } else if (arg == "--out" && i + 1 < argc) {
            out_dir = argv[++i];
        }
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

        std::cout << "Total power    : " << acc.total_power_w()           << " W\n";
        std::cout << "Peak flux      : " << acc.peak_flux_wm2()           << " W/m²\n";
        std::cout << "Concentration  : " << acc.concentration_ratio(dni)  << "×\n";
        std::cout << "Wall time      : " << result.wall_time_s             << " s\n";
        std::cout << "Results written to " << out_dir.string()             << '\n';
        return 0;
    }

    // Interactive Polyscope viewer
    ls.cfg.record_paths        = true;
    ls.cfg.max_paths_to_record = 200;

    scrt::viz::Viewer viewer;
    viewer.set_scene(ls.scene.get());
    viewer.set_config(ls.cfg);
    viewer.run();

    return 0;
}
