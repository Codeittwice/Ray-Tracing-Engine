#pragma once
#include "scrt/core/Transform.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include "scrt/viz/Panels.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
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

    // Per-object (surface) transform editor state, keyed by stable Scene id.
    std::unordered_map<std::uint64_t, ObjectEditState> edits_;

    /// Stable id of the surface selected in the outliner; 0 when nothing (or a
    /// non-surface row) is selected. Owned here so selection survives across frames.
    std::uint64_t selected_id_ = 0;

    void run_trace(std::size_t n_rays);
    void register_scene();
    void update_receiver_flux();
    void draw_gui();

    void scan_examples_dir();
    void load_from_file(const std::filesystem::path& path);
    void load_scene_internal(io::LoadedScene ls);
    void init_surf_xforms();
    void apply_object_xform(std::uint64_t id);

    /// Builds the PanelContext used to dispatch to the GUI panel functions.
    PanelContext make_panel_context();
};

} // namespace scrt::viz
