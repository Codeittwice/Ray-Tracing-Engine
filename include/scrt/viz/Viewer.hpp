#pragma once
#include "scrt/scene/Scene.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <memory>

namespace scrt::viz {

/// Polyscope-based 3D viewer with ImGui controls and ImPlot flux analysis.
class Viewer {
public:
    Viewer() = default;
    ~Viewer() = default;

    /// Set the scene to visualise (non-const for live material editing).
    void set_scene(scene::Scene* s);

    /// Override the initial trace configuration.
    void set_config(tracer::TraceConfig cfg) { cfg_ = cfg; }

    /// Run the viewer main loop (blocks until window closed).
    void run();

private:
    scene::Scene*                            scene_ = nullptr;
    tracer::TraceConfig                      cfg_;
    tracer::TraceResult                      result_;
    std::unique_ptr<tracer::FluxAccumulator> acc_;
    bool                                     traced_      = false;
    bool                                     need_retrace_ = false;

    void run_trace(std::size_t n_rays);
    void register_scene();
    void update_receiver_flux();
    void draw_gui();
};

} // namespace scrt::viz
