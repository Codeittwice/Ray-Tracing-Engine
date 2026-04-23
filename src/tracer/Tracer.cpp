#include "scrt/tracer/Tracer.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/materials/Material.hpp"
#include "scrt/scene/Scene.hpp"
#include <chrono>
#include <limits>

namespace scrt::tracer {

void Tracer::trace_one(core::Ray r, FluxAccumulator& acc, math::Rng& rng,
                       std::vector<math::vec3>* path, int max_bounces,
                       double power_cutoff, std::size_t& hit_count) const {
    if (path)
        path->push_back(r.origin);

    for (int b = 0; b < max_bounces; ++b) {
        core::Hit h;
        if (!scene_->intersect(r, scrt::math::EPSILON_T,
                               std::numeric_limits<double>::max(), h))
            return;

        if (path)
            path->push_back(h.position);

        ++hit_count;

        auto inter = h.surface->material()->interact(r, h, rng);
        switch (inter.kind) {
            case materials::InteractionKind::Absorbed:
                acc.deposit(r, h);
                return;
            case materials::InteractionKind::Reflected:
                r = inter.reflected;
                break;
            case materials::InteractionKind::Refracted:
                r = inter.transmitted;
                break;
            case materials::InteractionKind::Split:
                trace_one(inter.reflected, acc, rng, path, max_bounces,
                          power_cutoff, hit_count);
                r = inter.transmitted;
                break;
        }
        if (r.power < power_cutoff)
            return;
    }
}

TraceResult Tracer::run(const TraceConfig& cfg, FluxAccumulator& acc) const {
    auto t0 = std::chrono::steady_clock::now();

    math::Rng rng(cfg.rng_seed != 0 ? cfg.rng_seed
                                    : static_cast<std::uint64_t>(
                                          std::chrono::steady_clock::now()
                                              .time_since_epoch()
                                              .count()));

    const auto& sun = *scene_->sun();
    const auto& ap  = scene_->aperture();

    // Power per primary ray: P_total = DNI * aperture_area; shared across N rays
    double ray_power = sun.dni() * ap.area() / static_cast<double>(cfg.n_primary_rays);

    TraceResult result;
    result.primary_rays_traced = cfg.n_primary_rays;

    for (std::size_t i = 0; i < cfg.n_primary_rays; ++i) {
        std::vector<math::vec3>* path_ptr = nullptr;
        std::vector<math::vec3> path_buf;
        if (cfg.record_paths && result.sampled_paths.size() < cfg.max_paths_to_record) {
            path_ptr = &path_buf;
        }

        core::Ray r = sun.sample_ray(ap, rng);
        r.power     = ray_power;
        r.id        = static_cast<std::uint32_t>(i);

        trace_one(r, acc, rng, path_ptr, cfg.max_bounces, cfg.power_cutoff_w,
                  result.total_hits);

        if (path_ptr && !path_buf.empty())
            result.sampled_paths.push_back(std::move(path_buf));
    }

    acc.finalize(cfg.n_primary_rays);

    auto t1 = std::chrono::steady_clock::now();
    result.wall_time_s =
        std::chrono::duration<double>(t1 - t0).count();

    return result;
}

} // namespace scrt::tracer
