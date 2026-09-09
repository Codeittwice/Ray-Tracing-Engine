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

    /// Adds a fully-formed element to the scene and returns its new stable id (0 on failure).
    /// Null until the Viewer owns a scene::SceneEditor; while unset the import panel builds the
    /// io::ElementDoc, shows it, and reports that it cannot yet be committed.
    std::function<std::uint64_t(io::ElementDoc)> add_element;
};

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

} // namespace scrt::viz
