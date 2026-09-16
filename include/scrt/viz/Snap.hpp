#pragma once

#include <algorithm>
#include <cmath>

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

/// Rounds a scale factor to `step`, never below one step (a scale of 0 is not invertible).
inline double snap_scale(double v, double step) {
    if (!(step > 0.0)) return v;
    return std::max(step, snap_to(v, step));
}

} // namespace scrt::viz
