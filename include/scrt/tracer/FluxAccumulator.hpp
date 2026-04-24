#pragma once
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/math/Vec.hpp"
#include <cstddef>
#include <vector>

namespace scrt::tracer {

/// Accumulates optical flux on a 2D grid using surface UV coordinates from Hit.
class FluxAccumulator {
public:
    /// hw/hh: half-extents (m) matching the receiver Plane; nx/ny: grid resolution.
    FluxAccumulator(double half_width, double half_height, int nx, int ny);

    /// Deposit ray power into the bin corresponding to hit.uv.
    void deposit(const core::Ray& r, const core::Hit& h) noexcept;

    /// Add another accumulator's power_sum into this one (for parallel merge).
    void merge_from(const FluxAccumulator& other) noexcept;

    /// Convert accumulated power to W/m² using total_primary_rays for normalization.
    void finalize(std::size_t total_primary_rays);

    const std::vector<double>& flux_map_wm2() const { return flux_; }

    double total_power_w()   const;
    double peak_flux_wm2()   const;

    /// Concentration ratio = peak_flux / (dni_wm2 * concentration_area_factor).
    double concentration_ratio(double dni_wm2) const;

    /// Diameter (m) of the circle centred at (0,0) enclosing fraction of total power.
    double encircled_diameter(double fraction) const;

    int    nx()             const { return nx_; }
    int    ny()             const { return ny_; }
    double half_width()     const { return hw_; }
    double half_height()    const { return hh_; }
    double bin_width_m()    const { return 2.0 * hw_ / nx_; }
    double bin_height_m()   const { return 2.0 * hh_ / ny_; }

private:
    double hw_, hh_;
    int    nx_, ny_;
    std::vector<double> power_sum_;   ///< Accumulated power per bin (W).
    std::vector<double> flux_;        ///< Filled by finalize(): W/m².
};

} // namespace scrt::tracer
