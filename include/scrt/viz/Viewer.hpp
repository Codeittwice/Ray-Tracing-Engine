#pragma once
#include "scrt/core/Transform.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace scrt::viz {

/// Polyscope-based 3D viewer with ImGui controls and ImPlot flux analysis.
class Viewer {
public:
    Viewer() = default;
    ~Viewer() = default;

    /// Set the scene to visualise (non-const for live material/transform editing).
    void set_scene(scene::Scene* s);

    /// Override the initial trace configuration.
    void set_config(tracer::TraceConfig cfg) { cfg_ = cfg; }

    /// Set directory to scan for example scene files (.json) shown in the browser.
    void set_examples_dir(std::filesystem::path dir);

    /// Run the viewer main loop (blocks until window closed).
    void run();

private:
    scene::Scene*                            scene_        = nullptr;
    tracer::TraceConfig                      cfg_;
    tracer::TraceResult                      result_;
    std::unique_ptr<tracer::FluxAccumulator> acc_;
    bool                                     traced_       = false;
    bool                                     need_retrace_ = false;
    bool                                     need_rebuild_ = false;

    // Owned scene for scenes loaded via the browser
    std::unique_ptr<io::LoadedScene>         owned_scene_;

    // Scene browser state
    std::filesystem::path              examples_dir_;
    std::vector<std::filesystem::path> available_scenes_;
    std::vector<std::string>           scene_display_names_;
    int                                selected_scene_idx_ = -1;
    std::string                        load_error_;

    // Per-surface transform editor state
    struct SurfXformState {
        core::Transform base;           ///< Transform at load time (preserved)
        float           trans[3]   = {}; ///< GUI delta translation (m), world-space
        float           rot_deg[3] = {}; ///< GUI delta Euler XYZ rotation (degrees)
    };
    std::vector<SurfXformState> surf_xforms_;

    void run_trace(std::size_t n_rays);
    void register_scene();
    void update_receiver_flux();
    void draw_gui();

    void scan_examples_dir();
    void load_from_file(const std::filesystem::path& path);
    void load_scene_internal(io::LoadedScene ls);
    void init_surf_xforms();
    void apply_surf_xform(std::size_t idx);
    void draw_scene_browser();
    void draw_transform_editor();
};

} // namespace scrt::viz
