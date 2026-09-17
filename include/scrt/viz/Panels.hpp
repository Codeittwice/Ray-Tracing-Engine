#pragma once
#include "imgui.h"
#include "scrt/core/Transform.hpp"
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include "scrt/viz/Sweep.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace scrt::scene { class SceneEditor; }

namespace scrt::viz {

/// Per-object GUI edit state, keyed by the surface's stable Scene id.
///
/// The three float triples are the single source of truth for a placement edit: both the
/// numeric fields and the 3D gizmo read and write exactly these, and the world matrix is a
/// pure function of them, so the two input paths cannot drift apart. The edit is applied as
///   M = translate(pivot) * T(trans) * R(rot_deg) * S(scale) * translate(-pivot) * base
/// so rotation and scaling happen about the object, not about the world origin.
struct ObjectEditState {
    core::Transform base;            ///< Transform at load time (preserved).
    float           trans[3]   = {}; ///< GUI delta translation (m), world axes.
    float           rot_deg[3] = {}; ///< GUI delta Euler XYZ rotation (degrees) about the pivot.
    float           scale[3]   = {1.f, 1.f, 1.f}; ///< GUI delta scale factors about the pivot.

    /// World-space rotation/scale pivot: the object's bounds centroid in its *base* pose.
    /// Deliberately independent of the current edit, so it never chases the object.
    math::vec3      pivot{};
    /// False until pivot has been computed for this object; the panel fills it in once.
    bool            pivot_valid = false;
};

/// Mutable state shared between the Viewer shell and its GUI panels.
struct PanelContext {
    scene::Scene*                            scene = nullptr;
    tracer::TraceConfig*                     cfg   = nullptr;
    const tracer::TraceResult*               result = nullptr;
    const tracer::FluxAccumulator*           acc   = nullptr;
    bool                                     traced = false;
    bool*                                    need_retrace = nullptr;
    bool*                                    need_rebuild = nullptr;

    // Scene browser
    const std::vector<std::filesystem::path>* available_scenes = nullptr;
    const std::vector<std::string>*           scene_display_names = nullptr;
    int*                                      selected_scene_idx = nullptr;
    std::string*                              load_error = nullptr;

    // Object edit state, keyed by stable surface id (never by index).
    std::unordered_map<std::uint64_t, ObjectEditState>* edits = nullptr;

    /// Points at the Viewer-owned stable id of the selected surface (0 = none, or a
    /// non-surface row such as Sun is selected). A pointer, not a value, so the outliner's
    /// selection survives the per-frame rebuild of this context.
    std::uint64_t* selected_id = nullptr;

    /// Loads the scene at the given path into the Viewer (throws on failure).
    std::function<void(const std::filesystem::path&)> load_scene;
    /// Runs a trace with the given ray count.
    std::function<void(std::size_t)> run_trace;
    /// Re-applies GUI edit state for the surface with the given stable id.

    // Model import (Wave 4)

    /// Directory the loaded scene file lives in; imported mesh paths are stored relative to it.
    /// Null until the Viewer keeps the scene's path, in which case the import panel falls back
    /// to the current working directory and says so.
    const std::filesystem::path* scene_dir = nullptr;

    // Async trace (Wave 5)

    /// True while a trace runs on the worker thread. The Viewer already greys out every
    /// scene-mutating panel for the duration; a panel only needs this to phrase itself.
    bool        trace_running    = false;
    /// Why the last trace failed, empty when it did not; shown in red by the trace toolbar.
    const std::string* trace_error = nullptr;
    /// Primary rays completed by the running trace (0 when idle).
    std::size_t trace_rays_done  = 0;
    /// Primary rays the running trace was asked for (0 when idle).
    std::size_t trace_rays_total = 0;
    /// Asks the running trace to stop; null when the Viewer has no worker.
    std::function<void()> cancel_trace;

    // Stage 8: live preview and sweeps
    /// Re-trace a small preview automatically after every edit (GUI thread, between frames).
    bool*       live_update = nullptr;
    /// Rays the live preview currently uses (it adapts to hold ~30 fps) and its last trace time.
    std::size_t live_rays   = 0;
    double      live_ms     = 0.0;
    /// The sweep engine (read-only for panels); null when the Viewer has none.
    const SweepRunner* sweep = nullptr;
    /// True while a sweep runs or its player is open: the scene is being driven, so edits are locked.
    bool        sweep_busy  = false;
    int*        sweep_frame = nullptr;
    bool*       sweep_playing = nullptr;
    std::function<void(const SweepSpec&)> start_sweep;
    /// Cancels a running sweep or closes the player, restoring the part's pose either way.
    std::function<void()> stop_sweep;
    std::function<void(int)> show_sweep_frame;

    /// Queues a fully-formed element to be added to the scene; returns whether the request was
    /// accepted. Null until the Viewer owns a scene::SceneEditor; while unset the import panel
    /// builds the io::ElementDoc, shows it, and reports that it cannot yet be committed.
    ///
    /// QUEUED, not immediate, and so it cannot return the new element's id: the element does not
    /// exist yet when this returns. The Viewer applies every queued mutation at the end of the
    /// frame, because Scene::surfaces() hands out a span over the vector these mutations resize,
    /// and panels iterate that span while they draw.
    std::function<bool(io::ElementDoc)> add_element;

    /// Queues an element for removal, by stable id. Same deferral as add_element.
    std::function<void(std::uint64_t)> remove_element;

    /// Queues a material to be added to the document and the scene; returns whether the request
    /// was accepted (false when the id is empty or already taken). Queued for the same reason as
    /// add_element: Scene::materials() hands out a span the materials panel iterates while it
    /// draws, and add_material resizes the vector behind it.
    std::function<bool(io::MaterialDoc)> add_material;

    /// Queues a light source to be added. Same deferral. A sun that asks for an auto-fitted
    /// aperture gets it when the queue is applied, so it sees elements added in the same frame.
    std::function<bool(io::SourceDoc)> add_source;

    /// Queues an element to be copied under a fresh id and name. Same deferral as add_element.
    std::function<void(std::uint64_t)> duplicate_element;

    /// Commits a world-space placement through SceneEditor so the document stays authoritative.
    /// Null when the viewer has no editor; callers must fall back to display-only.
    std::function<void(std::uint64_t, const math::mat4&)> commit_transform;

    // Save (Wave 5)

    /// The document-backed editor, for doc(), dirty() and mark_saved(). Null until the Viewer
    /// owns one, in which case the save panel disables itself and says why. The save panel also
    /// uses this pointer's *identity* to detect that a different scene has been loaded, so the
    /// Viewer must keep handing out the address of its live scene::SceneEditor, never a copy.
    scene::SceneEditor* editor = nullptr;

    /// Full path of the scene file the document was last read from, or empty when the document
    /// was synthesized rather than loaded. Save writes here; Revert reloads from here. Only read
    /// on the frame the editor changes identity, so it need not be updated every frame.
    const std::filesystem::path* scene_path = nullptr;

    /// Tells the Viewer the document now lives at `path` (after a successful Save As), so a
    /// window title or status line can follow. MUST NOT reload or otherwise replace the editor:
    /// the in-memory document's relative mesh paths still resolve against the *original* base
    /// directory, and the save panel keeps tracking that separately. Optional.
    std::function<void(const std::filesystem::path&)> set_scene_path;

    /// Whether the Settings window is open. Owned by the Viewer so the gear icon in the top bar
    /// and the window's own close button write the same flag.
    bool* settings_open = nullptr;

    /// Asks the Viewer to open the Settings window (from the top bar's gear). Optional.
    std::function<void()> open_settings;

    /// Whether the import/export overlay is open, owned by the Viewer so the floating button and
    /// the overlay's own close button write the same flag.
    bool* io_overlay_open = nullptr;

    /// Whether the scene-assistant overlay is open. Same arrangement as io_overlay_open.
    bool* ai_overlay_open = nullptr;

    /// Asks the left column to select a tab by its label ("Scene", "Design", "Simulate") on the
    /// next frame. The import/export overlay uses it to drop the user where the model it just
    /// picked is waiting to be placed. Optional.
    std::function<void(const char*)> focus_tab;
};

/// What the outliner currently has selected, for panels that only need to describe it.
///
/// A copy taken per frame rather than a pointer into the outliner's own state: the selection is
/// resolved while the outliner draws, and the selection panel draws in a different window later
/// in the same frame.
struct SelectionInfo {
    bool          any = false;     ///< False when nothing is selected.
    std::string   kind;            ///< "Reflector", "Receiver face", "Sun", "Material", ...
    std::string   label;           ///< Display name of the selected row.
    std::string   structure;       ///< Polyscope structure name; empty for rows without geometry.
    std::uint64_t surface_id = 0;  ///< Stable Scene surface id; 0 for non-surface rows.
    const char*   icon = "";       ///< Icon literal for the row's kind.
    std::string   detail;          ///< One line of type-specific detail; may be empty.
};

/// Reconciles the selection with the 3D view: adopts a click in the viewport, drops a selection
/// whose object is gone, and re-paints the highlight after a structure was re-registered.
///
/// Call once per frame from the Viewer, BEFORE any panel draws - not from the outliner. The
/// outliner only draws while its tab is open, and a click in the viewport has to land whichever
/// tab the user is on.
void update_selection_from_view(PanelContext& ctx);

/// The outliner's current selection. Valid from the moment draw_outliner_panel() has run this
/// frame; before that it still holds last frame's, which is what an early-drawn panel wants.
SelectionInfo current_selection();

/// Outcome of a save attempt: whether the file is really on disk, plus a message for the user.
struct SaveOutcome {
    bool        ok = false;  ///< True only when a non-empty file exists at the destination.
    std::string message;     ///< Human-readable success or failure detail; never empty.
};

/// Writes `doc` to `path` through io::save_scene_as, rebasing relative mesh paths from
/// `doc_base` (the directory the in-memory document's paths resolve against), then verifies a
/// non-empty file landed. Never throws. Never use io::save_scene() here: its signature cannot
/// carry `doc_base`, so it writes mesh paths that no longer resolve from the new directory.
SaveOutcome save_document_to(const io::SceneDocument& doc, const std::filesystem::path& path,
                             const std::filesystem::path& doc_base);

/// Tells the save panel which file the scene now being loaded came from; call on success only.
void note_scene_loaded(const std::filesystem::path& path);

/// Draws the object outliner tree (reflectors, receiver faces, sun, aperture, materials).
///
/// `boxed` wraps the tree in a collapsing header; pass false where the tree is already the whole
/// point of its container, as it is in the left column's Scene tab.
void draw_outliner_panel(PanelContext& ctx, bool boxed = true);
/// Draws the scene browser panel (load example scenes).
void draw_scene_browser_panel(PanelContext& ctx);
/// Runs the 3D manipulator gizmo and draws the transform editor.
///
/// MUST be called exactly once per frame, whether or not anything is selected: ImGuizmo has to be
/// primed with a fresh frame and draw list before anything can manipulate, and skipping it leaves
/// it drawing into the previous frame's list.
void draw_transform_panel(PanelContext& ctx);
/// Draws the material parameter editor panel.
void draw_materials_panel(PanelContext& ctx);

/// Draws the component, material and source library: ready-made things to drop into the scene.
void draw_library_panel(PanelContext& ctx);

/// Accepts a library component dragged into the 3D view, dropping it where the cursor is.
///
/// Draws NOTHING and creates no window unless a drag is actually in flight. The 3D view is
/// Polyscope's render surface, not an ImGui window, so a drop target over it has to be an
/// overlay - and an overlay that existed permanently would sit between the user and Polyscope's
/// camera handling, swallowing rotate, pan, zoom and the edge-panning the user asked be kept.
/// Call once per frame, after the panels, with the layout's viewport rect.
void draw_viewport_drop_target(PanelContext& ctx, const ImVec2& vmin, const ImVec2& vmax);
/// Draws the sun controls panel (DNI, azimuth, elevation).
void draw_sun_panel(PanelContext& ctx);
/// Draws the trace controls and results panel.
void draw_trace_panel(PanelContext& ctx);

/// Draws the Preview / Full Trace toolbar floating at the top of the 3D viewport rect.
void draw_trace_toolbar(PanelContext& ctx, const ImVec2& vmin, const ImVec2& vmax);

/// Sweep setup for the selected part (move or turn over a range); drawn in the Simulate tab.
void draw_sweep_setup(PanelContext& ctx);

/// The sweep progress bar while it runs, then the player: scrub, play, and power vs position.
void draw_sweep_window(PanelContext& ctx, const ImVec2& vmin, const ImVec2& vmax);
/// Draws the 3D model import panel (file, unit detection, submesh split, placement).
void draw_import_panel(PanelContext& ctx);

/// Hands the import panel a file to inspect, as if the user had typed it and pressed Inspect.
///
/// The import/export overlay owns the file dialog but not the placement UI, so it picks the
/// file and passes it here; the Design tab then already has it loaded when the user arrives.
void set_import_file(const std::filesystem::path& path);
/// Draws the save / save-as / revert panel. Dispatched from draw_scene_browser_panel().
void draw_save_panel(PanelContext& ctx);
/// Draws the import/export overlay: a modal opened from the floating button over the viewport.
void draw_io_overlay(PanelContext& ctx);

/// Draws the scene-assistant overlay: describe a cooker, attach references, have one designed.
///
/// Call every frame whether or not it is open: it also collects the reply of a request that is
/// still in flight, and the user is free to close the overlay while one runs.
void draw_ai_overlay(PanelContext& ctx);

/// Draws the Settings window (theme, ground plane, Polyscope's own panels).
///
/// A free-floating, closable window rather than another entry in the left column: it is opened
/// from the top bar's gear, used rarely, and has nothing to do with the scene being edited.
void draw_settings_window(PanelContext& ctx);

/// Draws the selection read-out, and the transform editor beneath it when a surface is selected.
/// Returns the height the panel would like, so the layout can give the flux plot the rest.
float draw_selection_panel(PanelContext& ctx);
/// Draws the main menu bar. Returns its height so the layout can sit beneath it.
float draw_top_bar(PanelContext& ctx);
/// Draws the floating assistant / import-export buttons over the 3D viewport.
void draw_viewport_buttons(PanelContext& ctx, ImVec2 viewport_min, ImVec2 viewport_max);

} // namespace scrt::viz
