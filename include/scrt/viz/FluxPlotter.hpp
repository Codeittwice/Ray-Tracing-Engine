#pragma once
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <filesystem>

namespace scrt::viz {

/// Draws ImPlot flux analysis panels inside the active ImGui frame.
class FluxPlotter {
public:
    /// Draw heatmap + profile tabs. Call inside a valid ImGui/ImPlot frame.
    ///
    /// `dni_wm2` is the direct normal irradiance of the sun that produced `acc`. It divides
    /// the peak flux to give the concentration ratio and is written into an exported JSON
    /// summary, so it must be the scene's real DNI: this used to be hardcoded to 1000 W/m^2
    /// in both places, which made the reported concentration wrong by DNI/1000 as soon as
    /// the Sun panel's slider moved. Pass a non-positive value (the default) to use the DNI
    /// last published by scene_dni().
    void draw(const tracer::FluxAccumulator& acc, const tracer::TraceResult& result,
              double dni_wm2 = 0.0);

    /// Record the DNI of the scene currently on screen [W/m^2]; ignored if not positive.
    ///
    /// The flux window is opened by the Viewer shell, which does not pass the scene down.
    /// draw_sun_panel() calls this every frame with the live sun's DNI, and the flux window
    /// is drawn later in that same frame, so scene_dni() is the value the user is looking
    /// at. GUI thread only. Prefer the explicit `dni_wm2` argument wherever the call site
    /// can actually see the scene; this exists so the window cannot silently fall back to a
    /// fabricated 1000 W/m^2 when it cannot.
    static void set_scene_dni(double dni_wm2);

    /// DNI last published by set_scene_dni(), or 1000 W/m^2 if none ever was [W/m^2].
    static double scene_dni();

    /// True once set_scene_dni() has supplied a real scene DNI; false while scene_dni() is
    /// still the 1000 W/m^2 placeholder, which the window labels as an assumption.
    static bool scene_dni_known();

private:
    void draw_heatmap_tab(const tracer::FluxAccumulator& acc, double dni_wm2);
    void draw_profile_tab(const tracer::FluxAccumulator& acc,
                          const tracer::TraceResult& result);
};

} // namespace scrt::viz
