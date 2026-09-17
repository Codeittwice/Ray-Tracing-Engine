#pragma once
#include "scrt/core/Transform.hpp"
#include "scrt/io/SceneDocument.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace scrt::viz {

/// Process-wide bidirectional map between Polyscope structure names and stable Scene surface ids.
///
/// Polyscope itself keys structures by a globally unique string, so the mapping that makes
/// picking, the outliner and the transform editor agree is necessarily process-wide too.
/// Names are interned once at registration and never recomputed from a surface index, which
/// is what previously orphaned structures whenever a surface was reordered or removed.
class StructureRegistry {
public:
    /// The one registry, mirroring Polyscope's own process-wide structure table.
    static StructureRegistry& instance();

    /// Forgets every association; call before re-registering a scene.
    void clear();

    /// Interns a unique display name for id, appending " (2)", " (3)", ... on collision.
    std::string intern(std::uint64_t id, const std::string& base_name);

    /// Polyscope structure name for a surface id; empty string when unknown.
    const std::string& name_for(std::uint64_t id) const;

    /// Stable surface id for a Polyscope structure name; 0 when unknown.
    std::uint64_t id_for(const std::string& name) const;

    /// Drops a surface's name association and its baked transform.
    void erase(std::uint64_t id);

    /// Records the world transform baked into a surface's uploaded vertices.
    void set_baked(std::uint64_t id, const math::mat4& world);

    /// Inverse of the transform baked into a surface's vertices; identity when unknown.
    math::mat4 baked_inverse(std::uint64_t id) const;

    /// True when this surface id currently owns a registered structure name.
    bool contains(std::uint64_t id) const;

    /// Records (or, with nullopt, forgets) the drawn-only body of a surface's element.
    void set_body(std::uint64_t id, const std::optional<io::BodyDoc>& body);

    /// The body recorded for a surface id, or nullptr when it has none.
    const io::BodyDoc* body_for(std::uint64_t id) const;

private:
    std::unordered_map<std::string, std::uint64_t> name_to_id_;
    std::unordered_map<std::uint64_t, std::string> id_to_name_;
    std::unordered_map<std::uint64_t, math::mat4>  baked_inv_;
    std::unordered_map<std::uint64_t, io::BodyDoc> bodies_;
};

/// Polyscope structure name of a surface's drawn-only mount (substrate or cube).
std::string body_mount_structure_name(const std::string& surface_structure);

/// Polyscope structure name of a surface's drawn-only post.
std::string body_post_structure_name(const std::string& surface_structure);

/// Registers scene geometry and ray paths with Polyscope.
class RayRenderer {
public:
    /// Bind the renderer to a scene (non-const: placement edits write the surface transform).
    ///
    /// `doc`, when given, is where element bodies are read from on (re-)registration. A renderer
    /// without one (the transform panel's) keeps whatever bodies were recorded last.
    explicit RayRenderer(scene::Scene* scene, const io::SceneDocument* doc = nullptr)
        : scene_(scene), doc_(doc) {}

    /// Tessellate and register all optical surfaces as Polyscope meshes.
    void register_surfaces(int tess_segs = 32);

    /// Draws each laser as a housing ending at its origin, with a wavelength-coloured exit window
    /// and a post. Presentation only; re-run it when Show posts changes.
    void register_sources();

    /// Register the collection aperture as a translucent disk.
    void register_aperture();

    /// Register sampled ray paths as a Polyscope curve network.
    void register_paths(const tracer::TraceResult& result);

    /// Remove all previously registered geometry.
    void clear();

    /// Placement edit (translate/rotate/scale): writes the surface transform and mirrors the
    /// same matrix onto the Polyscope structure via setTransform. No tessellation, no upload.
    void set_surface_transform(std::uint64_t id, const core::Transform& world);

    /// Shape-parameter edit: re-tessellate and push vertex positions only (topology preserved).
    /// Falls back to a full re-registration when the vertex count changed.
    void update_surface_shape(std::uint64_t id, int tess_segs = 32);

    /// Topology edit (mesh swap): remove the structure and register the new tessellation.
    void reregister_surface(std::uint64_t id, int tess_segs = 32);

    /// Re-draws every surface's body (after a body setting such as Show posts changes).
    void sync_bodies();

    /// Remove one surface's structure and forget its registry entry.
    void remove_surface_structure(std::uint64_t id);

private:
    /// Reads the element's body from doc_ (when there is one) into the registry.
    void record_body(std::uint64_t id);

    /// Makes the drawn body of one surface match its current transform and recorded body.
    void sync_body(std::uint64_t id);

    /// Removes the mount and post structures belonging to one surface structure name.
    static void remove_body_structures(const std::string& surface_structure);

    scene::Scene*            scene_;
    const io::SceneDocument* doc_ = nullptr;
};

/// Re-applies the ray-path radius and opacity from ViewSettings to the live structure.
///
/// Separate from drawing them so the settings sliders can take effect immediately instead of
/// waiting for the next trace to re-register the curve network.
void apply_ray_appearance();

/// Makes the placement grid match the Show-grid setting and the move-snap step; call per frame.
void sync_grid(const scene::Scene* scene);

/// Makes the alignment-axis line match align_axis(); defaults the axis to the first laser. Per frame.
void sync_axis(const scene::Scene* scene);

/// Polyscope structure name of the placement grid.
const char* grid_structure_name();

/// Polyscope structure name of the collection aperture disk.
const char* aperture_structure_name();

/// Polyscope structure name of a receiver face's flux mesh (matches the Viewer's registration).
std::string receiver_flux_structure_name(bool multi_face, const std::string& face_name);

} // namespace scrt::viz
