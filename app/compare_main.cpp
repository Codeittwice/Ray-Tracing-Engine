#include "scrt/io/SceneLoader.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: scrt_compare scene1.json [scene2.json ...] [--rays N] [--out summary.csv]\n";
        return 1;
    }

    std::vector<std::filesystem::path> scene_paths;
    std::size_t                        n_rays_override = 0;
    std::filesystem::path              out_path        = "summary.csv";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--rays" && i + 1 < argc) {
            n_rays_override = std::stoull(argv[++i]);
        } else if (arg == "--out" && i + 1 < argc) {
            out_path = argv[++i];
        } else {
            scene_paths.emplace_back(arg);
        }
    }

    if (scene_paths.empty()) {
        std::cerr << "scrt_compare: no scene files specified.\n";
        return 1;
    }

    std::ofstream csv(out_path);
    if (!csv.is_open()) {
        std::cerr << "scrt_compare: cannot open '" << out_path.string() << "'\n";
        return 1;
    }
    csv << "scene,total_power_w,peak_flux_wm2,concentration_ratio,wall_time_s\n";
    csv.precision(6);

    int errors = 0;
    for (const auto& path : scene_paths) {
        try {
            scrt::io::LoadedScene ls = scrt::io::load_scene(path);
            ls.scene->build_acceleration_structure();

            if (n_rays_override > 0)
                ls.cfg.n_primary_rays = n_rays_override;

            auto* recv = ls.scene->receiver();
            if (!recv)
                throw std::runtime_error("scene has no receiver");

            const auto& ra = recv->accumulator();
            scrt::tracer::FluxAccumulator acc(ra.half_width(), ra.half_height(),
                                              ra.nx(), ra.ny());
            scrt::tracer::Tracer tracer(*ls.scene);
            auto result = tracer.run(ls.cfg, acc);

            double dni = ls.scene->sun() ? ls.scene->sun()->dni() : 1000.0;

            csv << path.string()              << ','
                << acc.total_power_w()        << ','
                << acc.peak_flux_wm2()        << ','
                << acc.concentration_ratio(dni) << ','
                << result.wall_time_s         << '\n';

            std::cout << "[OK] " << path.filename().string()
                      << "  power=" << acc.total_power_w()    << " W"
                      << "  peak="  << acc.peak_flux_wm2()    << " W/m²"
                      << "  CR="    << acc.concentration_ratio(dni) << 'x'
                      << "  t="     << result.wall_time_s     << " s\n";
        } catch (const std::exception& e) {
            std::cerr << "[ERR] " << path.string() << ": " << e.what() << '\n';
            ++errors;
        }
    }

    std::cout << "Summary written to " << out_path.string() << '\n';
    return errors ? 1 : 0;
}
