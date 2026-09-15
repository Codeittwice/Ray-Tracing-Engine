#pragma once
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/surfaces/Surface.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace scrt::scene {

/// Mutating front end that keeps an io::SceneDocument and the live scene::Scene it was built
/// from in step; every structural scene edit in the app goes through here.
///
/// ---------------------------------------------------------------------------------------
/// SYNC DISCIPLINE — read this before adding a member
/// ---------------------------------------------------------------------------------------
/// 1. THE DOCUMENT IS AUTHORITATIVE FOR STRUCTURE. Which elements exist, and each element's
///    name, material binding, shape parameters and mesh path, are owned by io::SceneDocument.
///    The live surfaces::Surface objects are a *derived* view: add_element(),
///    duplicate_element() and rebuild_element() always construct the surface *from* the
///    document, never the other way round, and never copy state back out of a surface.
///    duplicate_element() therefore re-derives from the ElementDoc rather than needing a
///    clone() on all eight surface classes.
///
/// 2. TRANSFORMS ARE THE ONE DELIBERATE EXCEPTION. commit_transform() is the ONLY member
///    permitted to write the io::TransformDoc and surfaces::Surface::set_transform() in the
///    same breath. Re-deriving a surface on every gizmo drag frame (re-importing a mesh, say)
///    is out of the question, so the transform is allowed to live in two places at once.
///    Confining that to a single function means there is exactly one place the invariant can
///    break, and exactly one function to audit.
///
/// 3. BVH LIFECYCLE IS CORRECTNESS, NOT PERFORMANCE. accel::BVH stores raw
///    surfaces::Surface* borrowed from Scene's std::unique_ptrs, so any removed or re-derived
///    surface leaves the BVH holding a dangling pointer. Every mutator here therefore ends by
///    calling Scene::mark_acceleration_dirty(); Scene::intersect() falls back to a linear scan
///    while dirty, so a stale BVH is never traversed. Call
///    Scene::build_acceleration_structure() again before tracing for speed, never for safety.
///
/// 4. IDS ARE THE JOIN KEY. io::ElementDoc::id and surfaces::Surface::id() carry the same
///    value for the same element. Ids are allocated here (not by Scene::add_surface, whose own
///    stamp is immediately overwritten), are never reused within a session, and are checked
///    against both the document and the live scene before being handed out.
///
/// Where this can still break, in decreasing order of likelihood:
///   - Anything that calls Scene::add_surface()/remove_surface() directly on scene() bypasses
///     the document and desynchronises structure. scene() is exposed for traversal, rendering
///     and tracing, not for structural edits.
///   - Anything that calls Surface::set_transform() directly leaves the document stale, so the
///     next rebuild_element() or save silently reverts the move. Route it through
///     commit_transform().
///   - Editing an ElementDoc through doc() changes structure without re-deriving its surface.
///     That is the intended flow for shape-parameter edits, but it MUST be followed by
///     rebuild_element(id); until then the live surface lags the document.
class SceneEditor {
public:
    /// Adopts a LoadedScene wholesale — scene, trace config and the io::SceneDocument the scene
    /// was built from — so document and scene are in sync by construction. This is the entry
    /// point for "the user opened a file": io::load_scene() returns exactly this. Throws
    /// std::runtime_error when `loaded.scene` is null.
    SceneEditor(io::LoadedScene loaded, std::filesystem::path base_dir);

    /// Builds the live scene from `doc` (io::build_scene) and takes ownership of both. The entry
    /// point for a document that was synthesized rather than loaded (a new empty scene, an
    /// AI-generated one). Throws std::runtime_error when the scene cannot be built.
    SceneEditor(io::SceneDocument doc, std::filesystem::path base_dir);

    /// Appends `d` to the document with a freshly allocated id and derives its live surface;
    /// returns the new id. Throws std::runtime_error if the surface cannot be built (unknown
    /// material id, unreadable mesh), leaving document and scene untouched.
    std::uint64_t add_element(io::ElementDoc d);

    /// Removes the element and its live surface; returns false when `id` matches neither.
    bool remove_element(std::uint64_t id);

    /// Copies the element's document spec under a fresh id and distinct name and derives a
    /// second surface from it; returns the new id, or 0 when `id` is unknown.
    std::uint64_t duplicate_element(std::uint64_t id);

    /// Re-derives the live surface from its (possibly edited) io::ElementDoc; call after any
    /// shape-parameter, material or mesh-path change. Returns false when `id` is unknown.
    bool rebuild_element(std::uint64_t id);

    /// Writes a world matrix to BOTH the live surface and the io::TransformDoc; see rule 2.
    void commit_transform(std::uint64_t id, const math::mat4& world);

    /// Writes one material parameter to BOTH the live materials::Material and its
    /// io::MaterialDoc. Returns false when `material_id` names no material or `key` is not a
    /// parameter of that material's type.
    ///
    /// The second case of rule 2, for the same reason as the first: a slider dragged at 60 Hz
    /// cannot re-derive a material and re-point every surface that borrows it, so the value is
    /// allowed to live in two places — confined to one function, so there is one place the
    /// invariant can break and one function to audit.
    ///
    /// Before this existed the materials panel wrote the live object only, so changing a
    /// reflectance and saving wrote the ORIGINAL value back out.
    bool commit_material_param(const std::string& material_id, const std::string& key,
                               double value);

    /// Writes the sun's direction and DNI to BOTH the live sources::SunSource and the
    /// io::SunDoc, keeping the doc's azimuth/elevation consistent with its direction.
    /// A no-op when the scene has no sun. Rule 2 again, same reason: continuous sliders.
    void commit_sun(math::vec3 direction, double dni_wm2);

    /// Mirrors trace settings into the document, so a save records what the user actually ran
    /// rather than what the file was loaded with. The document is authoritative here with no
    /// live counterpart to keep in step — the Viewer holds its own tracer::TraceConfig copy.
    void commit_trace_config(const tracer::TraceConfig& cfg);

    /// The live scene: traverse, render and trace it — do not add or remove surfaces on it.
    Scene& scene() { return *scene_; }
    /// The live scene: traverse, render and trace it — do not add or remove surfaces on it.
    const Scene& scene() const { return *scene_; }

    /// The authoritative document; edits to element shape parameters need rebuild_element().
    io::SceneDocument& doc() { return doc_; }
    /// The authoritative document.
    const io::SceneDocument& doc() const { return doc_; }

    /// Trace configuration that came with the loaded scene, carried for the Wave 5 save UI.
    const tracer::TraceConfig& trace_config() const { return cfg_; }

    /// Directory that relative mesh paths in the document resolve against.
    const std::filesystem::path& base_dir() const { return base_dir_; }

    /// True when the document has been mutated since construction or the last mark_saved().
    bool dirty() const { return dirty_; }

    /// Clears the dirty flag; call after a successful io::save_scene().
    void mark_saved() { dirty_ = false; }

private:
    /// Advances doc().next_id past every id already used by an element or a live surface.
    void seed_id_allocator();

    /// Builds the live surface for `d` and adds it to the scene under `d.id`; throws on failure.
    void instantiate(const io::ElementDoc& d);

    /// Finds the document element with the given id, or nullptr.
    io::ElementDoc* find_doc_element(std::uint64_t id);

    /// Resolves a material id against the live scene's materials (matched by name), or nullptr.
    const materials::Material* find_material(const std::string& material_id);

    /// True when `id` is already taken by a document element or a live surface.
    bool id_in_use(std::uint64_t id) const;

    /// Allocates the next free id and advances the document's allocator.
    std::uint64_t allocate_id();

    /// Derives a name not already used by any document element or live surface.
    std::string unique_copy_name(const std::string& base) const;

    io::SceneDocument             doc_;
    std::unique_ptr<Scene>        scene_;
    tracer::TraceConfig           cfg_;
    std::filesystem::path         base_dir_;
    bool                          dirty_ = false;
};

} // namespace scrt::scene
