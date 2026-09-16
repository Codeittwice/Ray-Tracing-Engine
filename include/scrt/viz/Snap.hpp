#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace scrt::viz {

/// Snap-to-step settings shared by the gizmo, the numeric placement fields and a library drop.
///
/// One object, so the three input paths that place things cannot snap to different steps.
struct SnapSettings {
    bool   enabled     = false; ///< Master switch; off means every path is continuous.
    float  translate_m = 0.01f; ///< Move step in metres.
    float  rotate_deg  = 15.0f; ///< Turn step in degrees.
    float  scale       = 0.1f;  ///< Size step, as a scale factor.
};

/// The one process-wide snap setting (tool state, like the gizmo's own; never saved).
inline SnapSettings& snap_settings() {
    static SnapSettings s;
    return s;
}

/// Rounds `v` to the nearest multiple of `step`; a non-positive step leaves `v` untouched.
inline double snap_to(double v, double step) {
    if (!(step > 0.0) || !std::isfinite(v)) return v;
    return std::round(v / step) * step;
}

/// Grid-line positions: multiples of `step` inside [lo, hi], never outside it.
///
/// The step is doubled until there are at most `max_lines`, and the step actually used is
/// written to `used`. Never outside [lo, hi] matters: Polyscope folds every structure into the
/// scene extents, so a grid line a hair beyond the scene would resize the ground plane and every
/// relative length (see the grid note in docs/plans/v2_waves_4_to_8_design.md, 4.4).
inline std::vector<double> grid_positions(double lo, double hi, double step, int max_lines,
                                          double& used) {
    std::vector<double> out;
    used = step;
    if (!(step > 0.0) || !(hi >= lo) || !std::isfinite(lo) || !std::isfinite(hi) || max_lines < 1)
        return out;
    while ((hi - lo) / used + 1.0 > max_lines) used *= 2.0;
    for (double k = std::ceil(lo / used - 1e-9); k * used <= hi + 1e-9 * used; k += 1.0)
        out.push_back(std::clamp(k * used, lo, hi));
    return out;
}

/// Rounds a scale factor to `step`, never below one step (a scale of 0 is not invertible).
inline double snap_scale(double v, double step) {
    if (!(step > 0.0)) return v;
    return std::max(step, snap_to(v, step));
}

} // namespace scrt::viz
