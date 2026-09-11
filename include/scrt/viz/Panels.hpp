#pragma once
#include "scrt/core/Transform.hpp"
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
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
    /// Primary rays completed by the running trace (0 when idle).
    std::size_t trace_rays_done  = 0;
    /// Primary rays the running trace was asked for (0 when idle).
    std::size_t trace_rays_total = 0;
    /// Asks the running trace to stop; null when the Viewer has no worker.
    std::function<void()> cancel_trace;

    /// Adds a fully-formed element to the scene and returns its new stable id (0 on failure).
    /// Null until the Viewer owns a scene::SceneEditor; while unset the import panel builds the
    /// io::ElementDoc, shows it, and reports that it cannot yet be committed.
    std::function<std::uint64_t(io::ElementDoc)> add_element;

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
};

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
void draw_outliner_panel(PanelContext& ctx);
/// Draws the scene browser panel (load example scenes).
void draw_scene_browser_panel(PanelContext& ctx);
/// Draws the per-object transform editor panel.
void draw_transform_panel(PanelContext& ctx);
/// Draws the material parameter editor panel.
void draw_materials_panel(PanelContext& ctx);
/// Draws the sun controls panel (DNI, azimuth, elevation).
void draw_sun_panel(PanelContext& ctx);
/// Draws the trace controls and results panel.
void draw_trace_panel(PanelContext& ctx);
/// Draws the 3D model import panel (file, unit detection, submesh split, placement).
void draw_import_panel(PanelContext& ctx);
/// Draws the save / save-as / revert panel. Dispatched from draw_scene_browser_panel().
void draw_save_panel(PanelContext& ctx);
/// Draws the settings panel (theme, ground plane, Polyscope's own panels).
void draw_settings_panel(PanelContext& ctx);

} // namespace scrt::viz
