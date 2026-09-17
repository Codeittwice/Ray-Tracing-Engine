#pragma once
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/math/Vec.hpp"
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace scrt::tracer {

/// Accumulates optical flux on a 2D grid using surface UV coordinates from Hit.
class FluxAccumulator {
public:
    /// hw/hh: half-extents (m) matching the receiver Plane; nx/ny: grid resolution.
    FluxAccumulator(double half_width, double half_height, int nx, int ny);

    /// A finalised, incoherent accumulator holding a stored flux map [W/m2] - for showing a frame of
    /// a sweep with the same plotting code as a live trace. total_power_w() is the map's integral.
    static FluxAccumulator from_flux_map(double half_width, double half_height, int nx, int ny,
                                         const std::vector<double>& flux_wm2);

    /// Deposit ray power into the bin corresponding to hit.uv.
    void deposit(const core::Ray& r, const core::Hit& h) noexcept;

    /// Reset accumulated power and finalized flux to zero.
    void clear() noexcept;

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

    /// Switches the accumulator between power (incoherent, the default) and field (coherent) sums.
    ///
    /// COHERENT: each ray adds a complex field sqrt(P) exp(i 2 pi opl / lambda) - a vector along its
    /// Jones state if polarised, a scalar channel if not - to an ARM of its bin, keyed by (source,
    /// branch). An arm's field is normalised by the number of distinct primary rays in it, so one
    /// beam of any ray density reads exactly its incoherent power, and two arms of equal power read
    /// 0 to 4 times one arm. Arms of the same source then add as fields, their cross term weighted by
    /// exp(-|mean path difference| / coherence length) when that length is set; different sources
    /// never interfere. Only meaningful for DETERMINISTICALLY sampled sources (Tracer::run refuses
    /// anything else). power_sum_ keeps the incoherent total alongside, for comparison.
    void set_coherent(bool on);
    bool coherent() const { return coherent_; }

    /// Coherence length per source index [m]; 0 or a missing entry means fully coherent.
    void set_coherence_lengths(std::vector<double> lengths) { coherence_len_ = std::move(lengths); }
    const std::vector<double>& coherence_lengths() const { return coherence_len_; }

    /// Incoherent power sum over the grid [W], whatever the mode: in coherent mode the difference
    /// from total_power_w() is how much interference redistributed across the receiver's edges.
    double incoherent_power_w() const;

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

    /// One physical arm's field in one bin (coherent mode).
    struct Arm {
        std::uint32_t        source  = 0;
        std::uint32_t        branch  = 0;
        std::complex<double> ex, ey, ez;   ///< Polarised field, world components.
        std::complex<double> eu;           ///< Unpolarised rays, as a scalar field.
        double               primaries = 0.0;  ///< Distinct primary rays that reached this arm.
        double               contributions = 0.0;
        double               opl_sum = 0.0;
        std::uint32_t        last_id = 0;
        bool                 has_last = false;
    };
    bool                           coherent_ = false;
    std::vector<std::vector<Arm>>  arms_;          ///< Per bin, coherent mode only.
    std::vector<double>            coherence_len_;
    double                         coherent_power_ = 0.0;   ///< Set by finalize() in coherent mode.

    void deposit_coherent(const core::Ray& r, const core::Hit& h, int ix, int iy);
};

} // namespace scrt::tracer
