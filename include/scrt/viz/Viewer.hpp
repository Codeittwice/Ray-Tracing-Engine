#pragma once
#include "scrt/core/Transform.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/scene/SceneEditor.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include "scrt/viz/Panels.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace scrt::viz {

/// Polyscope-based 3D viewer with ImGui controls and ImPlot flux analysis.
class Viewer {
public:
    Viewer() = default;
    /// Cancels and joins any running trace before the scene it reads is destroyed.
    ~Viewer();

    Viewer(const Viewer&)            = delete;  ///< Holds a worker thread; not copyable.
    Viewer& operator=(const Viewer&) = delete;  ///< Holds a worker thread; not copyable.

    /// Set the scene to visualise (non-const for live material/transform editing).
    void set_scene(scene::Scene* s);

    /// Override the initial trace configuration.
    void set_config(tracer::TraceConfig cfg) { cfg_ = cfg; }

    /// Set directory to scan for example scene files (.json) shown in the browser.
    void set_examples_dir(std::filesystem::path dir);

    /// Adopt a loaded scene and its document, so the viewer can mutate and save it.
    /// Prefer this over set_scene(): it is what gives the viewer a SceneEditor.
    void set_loaded_scene(io::LoadedScene ls, std::filesystem::path scene_file);

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

    // Document-backed editor; non-null whenever the viewer owns its scene.
    std::unique_ptr<scene::SceneEditor>      editor_;
    std::filesystem::path                    scene_dir_;
    /// Full path of the file the current document came from; empty for a synthesized scene.
    std::filesystem::path                    scene_path_;

    /// Show Polyscope's own Structures/Selection panels. Off by default: they are placed at
    /// fixed positions that collide with ours. Exposed so Settings can bring them back.
    bool                                     show_polyscope_panels_ = false;

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

    // ---- Async trace (Wave 5) --
    // The worker owns nothing the GUI writes while it runs: it reads the scene (which every
    // scene-mutating control is disabled for the duration) and writes only pending_*, which
    // the GUI reads solely after joining the thread.

    tracer::TraceControl                     trace_ctl_;      ///< Cancel flag + progress counters.
    std::jthread                             trace_thread_;   ///< Worker running Tracer::run.
    std::atomic<bool>                        trace_running_{false}; ///< True from launch to pickup.
    std::atomic<bool>                        trace_done_{false};    ///< Worker finished; result waiting.
    tracer::TraceResult                      pending_result_;       ///< Worker-written, GUI-read after join.
    std::unique_ptr<tracer::FluxAccumulator> pending_acc_;          ///< Worker-written, GUI-read after join.

    /// Launches a trace of `n_rays` on the worker thread; a no-op while one already runs.
    void start_trace(std::size_t n_rays);
    /// GUI-thread poll: joins a finished worker and publishes its result. Called once per frame.
    void poll_trace();
    /// Cancels any running trace and joins the worker; safe to call when idle.
    void cancel_and_join_trace();

    void register_scene();
    void update_receiver_flux();
    void draw_gui();

    void scan_examples_dir();
    void load_from_file(const std::filesystem::path& path);
    void load_scene_internal(io::LoadedScene ls);
    void init_surf_xforms();

    /// Builds the PanelContext used to dispatch to the GUI panel functions.
    PanelContext make_panel_context();
};

} // namespace scrt::viz
