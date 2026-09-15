#pragma once
#include "scrt/sources/LightSource.hpp"
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace scrt::tracer {

/// How one run's primary rays are divided among the scene's sources, and what each is worth.
///
/// Built once per run, before the parallel region. Entries partition the GLOBAL ray index
/// space [0, N) - the same space Tracer::run already derives each slot's `ray_start` from -
/// so slot decomposition and per-slot seeding are untouched, and a slot may straddle two
/// sources. Every ray a source is allotted carries total_power_w() / count watts on top of
/// the relative weight the source returned.
struct EmissionPlan {
    struct Entry {
        const sources::LightSource* source = nullptr;
        std::size_t                 first  = 0;   ///< First global ray index of this run.
        std::size_t                 count  = 0;   ///< Rays allotted; never 0 for a kept entry.
        double                      per_ray_w = 0.0;  ///< total_power_w() / count [W].
    };

    std::vector<Entry> entries;  ///< Contiguous, ascending in `first`, covering [0, total).

    bool        empty() const { return entries.empty(); }
    std::size_t total_rays() const {
        return entries.empty() ? 0 : entries.back().first + entries.back().count;
    }

    /// The entry that owns global ray index `i`. Precondition: i < total_rays().
    ///
    /// A linear scan: one compare for every scene that exists today, and measurable only for
    /// a hypothetical fifty-source scene at ten million rays.
    const Entry& entry_for(std::size_t i) const {
        std::size_t k = 0;
        while (k + 1 < entries.size() && i >= entries[k + 1].first) ++k;
        return entries[k];
    }
};

/// Allot `n_rays` among `sources` in proportion to total_power_w().
///
/// Deterministic, no RNG: truncate each share, then hand the remainder out one ray at a time
/// by descending fractional part, ties broken by source index. A source emitting <= 0 W (or
/// a non-finite power) is dropped and never reaches a division. Every kept source gets at
/// least one ray, so a milliwatt pilot beam beside a kilowatt dish is still visible in the
/// path render - a policy choice, not a physical one; drop it if a per-source ray budget lands
/// in the GUI. With exactly one emitting source it gets all N rays, so per_ray_w is
/// total_power_w() / N: the same double the single-sun tracer computed.
EmissionPlan build_emission_plan(std::span<const std::unique_ptr<sources::LightSource>> sources,
                                 std::size_t n_rays);

} // namespace scrt::tracer
