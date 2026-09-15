#include "scrt/tracer/EmissionPlan.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace scrt::tracer {

EmissionPlan build_emission_plan(std::span<const std::unique_ptr<sources::LightSource>> sources,
                                 std::size_t n_rays) {
    EmissionPlan plan;
    if (n_rays == 0) return plan;

    // ---- Keep the sources that actually emit ------------------------------------
    struct Kept {
        const sources::LightSource* src;
        double                      power_w;
        std::size_t                 count = 0;
        double                      frac  = 0.0;
    };
    std::vector<Kept> kept;
    for (const auto& s : sources) {
        if (!s) continue;
        const double p = s->total_power_w();
        if (std::isfinite(p) && p > 0.0)
            kept.push_back({s.get(), p});
    }
    if (kept.empty()) return plan;

    // ---- One emitting source: it gets every ray, and per_ray_w is total / N exactly -----
    // Written as its own branch so the arithmetic below (a sum, a division by that sum, a
    // multiplication by N) never touches the single-sun case that every golden value rests on.
    if (kept.size() == 1) {
        plan.entries.push_back({kept[0].src, 0, n_rays,
                                kept[0].power_w / static_cast<double>(n_rays)});
        return plan;
    }

    // ---- Proportional shares, truncated ------------------------------------------
    double total_w = 0.0;
    for (const auto& k : kept) total_w += k.power_w;

    std::size_t assigned = 0;
    for (auto& k : kept) {
        const double share = static_cast<double>(n_rays) * (k.power_w / total_w);
        k.count = static_cast<std::size_t>(std::floor(share));
        k.frac  = share - static_cast<double>(k.count);
        assigned += k.count;
    }

    // ---- Remainder by descending fractional part, ties by source index ----------
    std::vector<std::size_t> order(kept.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return kept[a].frac > kept[b].frac;
    });
    for (std::size_t i = 0; assigned < n_rays; ++i) {
        ++kept[order[i % order.size()]].count;
        ++assigned;
    }

    // ---- Every kept source gets at least one ray, taken from the largest allotment -----
    // Only possible while there are at least as many rays as kept sources; below that the
    // starved sources stay starved rather than the plan lying about the ray count.
    if (n_rays >= kept.size()) {
        for (auto& k : kept) {
            if (k.count > 0) continue;
            auto donor = std::max_element(kept.begin(), kept.end(),
                                          [](const Kept& a, const Kept& b) {
                                              return a.count < b.count;
                                          });
            if (donor->count <= 1) break;  // Nothing to spare; cannot happen when n >= size.
            --donor->count;
            k.count = 1;
        }
    }

    // ---- Lay the entries out over [0, n_rays) in source order -------------------
    std::size_t first = 0;
    for (const auto& k : kept) {
        if (k.count == 0) continue;
        plan.entries.push_back({k.src, first, k.count,
                                k.power_w / static_cast<double>(k.count)});
        first += k.count;
    }
    return plan;
}

} // namespace scrt::tracer
