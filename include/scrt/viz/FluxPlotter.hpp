#pragma once
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <filesystem>

namespace scrt::viz {

/// Draws ImPlot flux analysis panels inside the active ImGui frame.
class FluxPlotter {
public:
    /// Draw heatmap + profile tabs. Call inside a valid ImGui/ImPlot frame.
    void draw(const tracer::FluxAccumulator& acc, const tracer::TraceResult& result);

private:
    void draw_heatmap_tab(const tracer::FluxAccumulator& acc);
    void draw_profile_tab(const tracer::FluxAccumulator& acc,
                          const tracer::TraceResult& result);
};

} // namespace scrt::viz
