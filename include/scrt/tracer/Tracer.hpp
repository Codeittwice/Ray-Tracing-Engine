#pragma once
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/math/Vec.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace scrt::scene { class Scene; }

namespace scrt::tracer {

/// Configuration for a tracing run.
struct TraceConfig {
    std::size_t   n_primary_rays       = 1'000'000;
    int           max_bounces          = 16;
    double        power_cutoff_w       = 1e-9;
    std::uint64_t rng_seed             = 0;        ///< 0 → std::random_device
    bool          record_paths         = false;
    std::size_t   max_paths_to_record  = 2000;
};

/// Summary returned after a completed run.
struct TraceResult {
    std::size_t                           primary_rays_traced = 0;
    std::size_t                           total_hits          = 0;
    double                                wall_time_s         = 0.0;
    std::vector<std::vector<math::vec3>>  sampled_paths;
};

/// Single-threaded Monte Carlo ray tracer (parallelism added in Phase 5).
class Tracer {
public:
    explicit Tracer(const scene::Scene& s) : scene_(&s) {}

    TraceResult run(const TraceConfig& cfg, FluxAccumulator& acc) const;

private:
    void trace_one(core::Ray r, FluxAccumulator& acc, math::Rng& rng,
                   std::vector<math::vec3>* path_out, int max_bounces,
                   double power_cutoff, std::size_t& hit_count) const;

    const scene::Scene* scene_;
};

} // namespace scrt::tracer
