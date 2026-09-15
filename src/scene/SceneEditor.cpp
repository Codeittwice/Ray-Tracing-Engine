#include "scrt/scene/SceneEditor.hpp"
#include "scrt/core/AABB.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/materials/Dielectric.hpp"
#include "scrt/materials/Material.hpp"
#include "scrt/materials/RealMirror.hpp"
#include "scrt/sources/SunSource.hpp"
#include "scrt/math/Constants.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

// NOTE ON SCOPE: the class contract, including the document/scene sync discipline every
// function here obeys, is documented on SceneEditor in SceneEditor.hpp. This file implements
// it and nothing else.

namespace scrt::scene {

// ---- Construction --------------------------------------------------------

// io::LoadedScene now carries the io::SceneDocument its scene was built from, so adopting one
// wholesale is the in-sync-by-construction case; there is nothing left to reconcile. The three
// moves read from three distinct subobjects of `loaded`, so their unspecified evaluation order
// is immaterial (mem-initializers still run in member declaration order regardless).
SceneEditor::SceneEditor(io::LoadedScene loaded, std::filesystem::path base_dir)
    : doc_(std::move(loaded.doc)),
      scene_(std::move(loaded.scene)),
      cfg_(loaded.cfg),
      base_dir_(std::move(base_dir)) {
    if (!scene_)
        throw std::runtime_error("SceneEditor: constructed from a LoadedScene with no scene");
    seed_id_allocator();
}

// HAZARD, do not "optimize" this: the two arguments below must both be plain copies. The
// arguments of a delegating mem-initializer are evaluated in UNSPECIFIED order, so writing
// `io::build_scene(doc, base_dir), std::move(base_dir)` (or `std::move(doc)` anywhere here) would
// let the move run first and hand build_scene a gutted object. Copying a path and letting
// build_scene copy the document is the whole cost of being obviously correct.
SceneEditor::SceneEditor(io::SceneDocument doc, std::filesystem::path base_dir)
    : SceneEditor(io::build_scene(doc, base_dir), base_dir) {}

void SceneEditor::seed_id_allocator() {
    // The document's id allocator must clear every id already in play, otherwise the first
    // add_element() would hand out an id an existing element already owns and the join key
    // between doc_ and scene_ would stop being unique.
    for (const auto& el : doc_.elements)
        doc_.next_id = std::max(doc_.next_id, el.id + 1);
    for (const auto& s : scene_->surfaces())
        doc_.next_id = std::max(doc_.next_id, s->id() + 1);
    if (doc_.next_id == 0)
        doc_.next_id = 1;
}

// ---- Private helpers -----------------------------------------------------

io::ElementDoc* SceneEditor::find_doc_element(std::uint64_t id) {
    for (auto& el : doc_.elements)
        if (el.id == id)
            return &el;
    return nullptr;
}

const materials::Material* SceneEditor::find_material(const std::string& material_id) {
    if (material_id.empty())
        return nullptr;
    for (const auto& m : scene_->mutable_materials())
        if (m && m->name() == material_id)
            return m.get();
    return nullptr;
}

bool SceneEditor::id_in_use(std::uint64_t id) const {
    for (const auto& el : doc_.elements)
        if (el.id == id)
            return true;
    for (const auto& s : scene_->surfaces())
        if (s->id() == id)
            return true;
    return false;
}

std::uint64_t SceneEditor::allocate_id() {
    std::uint64_t candidate = std::max<std::uint64_t>(doc_.next_id, 1);
    while (id_in_use(candidate))
        ++candidate;
    doc_.next_id = candidate + 1;
    return candidate;
}

std::string SceneEditor::unique_copy_name(const std::string& base) const {
    const std::string stem = base.empty() ? std::string("element") : base;
    auto taken = [this](const std::string& n) {
        for (const auto& el : doc_.elements)
            if (el.name == n)
                return true;
        for (const auto& s : scene_->surfaces())
            if (s->name() == n)
                return true;
        return false;
    };
    std::string candidate = stem + " copy";
    for (int n = 2; taken(candidate); ++n)
        candidate = stem + " copy " + std::to_string(n);
    return candidate;
}

void SceneEditor::instantiate(const io::ElementDoc& d) {
    // Everything that can fail happens before the scene is touched, so a throw leaves both the
    // document and the scene exactly as they were.
    const materials::Material* mat = find_material(d.material_id);
    if (!mat)
        throw std::runtime_error("SceneEditor: element references unknown material id '" +
                                 d.material_id + "'");

    auto surf = io::build_surface(d.surface, base_dir_);

    if (!io::is_default_transform(d.transform))
        surf->set_transform(d.transform.to_transform());
    if (!d.name.empty())
        surf->set_name(d.name);
    surf->set_material(mat);

    // io::ElementDoc::visible has no runtime counterpart on surfaces::Surface, so the surface is
    // added regardless: an editor-only visibility flag must never change the optics.
    auto* raw = surf.get();
    scene_->add_surface(std::move(surf));  // stamps its own sequential id...
    raw->set_id(d.id);                      // ...which the document's id overrides (rule 4).
    scene_->mark_acceleration_dirty();
}

// ---- Structural mutation -------------------------------------------------

std::uint64_t SceneEditor::add_element(io::ElementDoc d) {
    d.id = allocate_id();
    instantiate(d);  // throws before mutating anything if the surface cannot be built
    const std::uint64_t id = d.id;
    doc_.elements.push_back(std::move(d));
    dirty_ = true;
    return id;
}

bool SceneEditor::remove_element(std::uint64_t id) {
    const bool erased_surface = scene_->remove_surface(id);

    bool erased_doc = false;
    auto it = std::find_if(doc_.elements.begin(), doc_.elements.end(),
                           [id](const io::ElementDoc& el) { return el.id == id; });
    if (it != doc_.elements.end()) {
        doc_.elements.erase(it);
        erased_doc = true;
    }

    if (!erased_surface && !erased_doc)
        return false;

    // Scene::remove_surface() already dirties the acceleration structure, but say it here too:
    // the BVH holds a raw Surface* into the unique_ptr just destroyed, and this class — not
    // Scene — is the one that promises never to leave a traversable stale BVH behind.
    scene_->mark_acceleration_dirty();
    dirty_ = true;
    return true;
}

std::uint64_t SceneEditor::duplicate_element(std::uint64_t id) {
    const io::ElementDoc* src = find_doc_element(id);
    if (!src)
        return 0;

    // Copy by value first: add_element() push_backs into doc_.elements, which would invalidate
    // a reference into that vector. Re-deriving from the spec is also why no surfaces::Surface
    // subclass needs a clone().
    io::ElementDoc copy = *src;
    copy.name = unique_copy_name(copy.name);
    return add_element(std::move(copy));
}

bool SceneEditor::rebuild_element(std::uint64_t id) {
    const io::ElementDoc* el = find_doc_element(id);
    if (!el)
        return false;

    // Copy the spec before removing anything: instantiate() may throw (a mesh path edited to a
    // file that does not exist), and the old surface must survive that.
    const io::ElementDoc spec = *el;
    scene_->remove_surface(id);
    // NOTE: the re-derived surface is appended, so it moves to the end of Scene::surfaces().
    // Nothing depends on that order today — the outliner walks doc_.elements, and the BVH is
    // rebuilt from scratch — but a consumer that assumes scene order matches document order
    // must sort by id.
    instantiate(spec);
    dirty_ = true;
    return true;
}

void SceneEditor::commit_transform(std::uint64_t id, const math::mat4& world) {
    // Rule 2: the ONE function allowed to write the live surface and the document together.
    if (auto* s = scene_->surface_by_id(id)) {
        s->set_transform(core::Transform::from_matrix(world));
        // A moved surface invalidates the BVH's cached world bounds just as surely as a
        // removed one invalidates its pointers.
        scene_->mark_acceleration_dirty();
    }

    if (auto* el = find_doc_element(id)) {
        math::vec3 t{0.0}, euler_rad{0.0}, scale{1.0};
        bool stored_as_trs = false;
        if (core::Transform::from_matrix(world).decompose_trs(t, euler_rad, scale)) {
            // Prefer the readable TRS triple so saved files stay diffable, but only when
            // recomposing it reproduces `world`: decompose_trs() succeeds for any unsheared
            // linear part, and T*R*S then reproduces the input to rounding. If it does not
            // (mirrored or otherwise pathological linear part), fall through to the matrix so
            // the document never disagrees with the surface by more than float rounding.
            const math::mat4 recomposed =
                core::Transform::from_trs(t, euler_rad, scale).matrix();
            double worst = 0.0;
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    worst = std::max(worst, std::abs(recomposed[c][r] - world[c][r]));
            stored_as_trs = worst <= 1e-9;
        }

        if (stored_as_trs) {
            el->transform.translation        = t;
            el->transform.rotation_euler_deg = euler_rad * math::RAD2DEG;
            el->transform.scale              = scale;
            el->transform.matrix.reset();
        } else {
            // io::TransformDoc::to_transform() gives `matrix` precedence over the TRS fields,
            // so this branch round-trips `world` exactly. The TRS fields are still filled with
            // the translation column (which is exact regardless of shear) so a UI reading them
            // does not show stale numbers from a previous placement.
            el->transform.translation        = math::vec3(world[3]);
            el->transform.rotation_euler_deg = math::vec3{0.0};
            el->transform.scale              = math::vec3{1.0};
            el->transform.matrix             = world;
        }
        dirty_ = true;
    }
}

// ---- commit_material_param ----------------------------------------------

bool SceneEditor::commit_material_param(const std::string& material_id, const std::string& key,
                                        double value) {
    // Live side first: dispatch on the concrete type, because only it knows which setter a key
    // names. An unknown key is reported rather than written, so a typo cannot land in the
    // document as a parameter the loader will later reject in strict mode.
    materials::Material* live = nullptr;
    for (const auto& m : scene_->mutable_materials())
        if (m && m->name() == material_id) { live = m.get(); break; }
    if (!live) return false;

    bool applied = false;
    if (auto* rm = dynamic_cast<materials::RealMirror*>(live)) {
        if (key == "reflectance")            { rm->set_reflectance(value);      applied = true; }
        else if (key == "slope_error_mrad")  { rm->set_slope_error_mrad(value); applied = true; }
    } else if (auto* di = dynamic_cast<materials::Dielectric*>(live)) {
        if (key == "n")                      { di->set_n(value);          applied = true; }
        else if (key == "absorption_per_m")  { di->set_absorption(value);  applied = true; }
    }
    if (!applied) return false;

    // Document side. MaterialDoc::params is deliberately type-erased json, so writing the key
    // verbatim is both the storage and the round-trip: SceneWriter merges params in as-is.
    for (auto& md : doc_.materials) {
        if (md.id != material_id) continue;
        md.params[key] = value;
        dirty_ = true;
        return true;
    }

    // The live scene has a material the document does not. io::build_scene injects anonymous
    // absorbers for the receiver that have no MaterialDoc at all, and they are reachable from
    // the materials panel; changing one is a live-only edit with nothing to record.
    return false;
}

// ---- commit_sun ---------------------------------------------------------

void SceneEditor::commit_sun(math::vec3 direction, double dni_wm2) {
    auto* sun     = scene_->primary_sun();
    auto* sun_doc = io::first_sun(doc_);
    // The document is authoritative for structure, so a live sun without a document sun (or
    // the reverse) is a sync bug; refuse to widen it by writing one side only.
    if (!sun || !sun_doc) return;

    sun->set_sun_direction(direction);
    sun->set_dni(dni_wm2);

    // Both halves of the document's sun, kept consistent with each other. SunSourceDoc::
    // direction wins over the angles on load, but SceneWriter emits whichever of the two it
    // has, so a fresh direction beside stale angles would write a file that contradicts itself.
    sun_doc->direction     = direction;
    const auto angles      = sources::SunSource::angles_from_direction(direction);
    sun_doc->azimuth_deg   = angles.azimuth_deg;
    sun_doc->elevation_deg = angles.elevation_deg;
    sun_doc->dni_wm2       = dni_wm2;
    dirty_                 = true;
}

// ---- add_material / remove_material -------------------------------------

bool SceneEditor::add_material(io::MaterialDoc md) {
    if (md.id.empty())
        return false;
    for (const auto& m : doc_.materials)
        if (m.id == md.id)
            return false;
    if (find_material(md.id))
        return false;
    auto mat = io::build_material(md);  // throws on an unknown type, before any mutation
    scene_->add_material(std::move(mat));
    doc_.materials.push_back(std::move(md));
    dirty_ = true;
    return true;
}

bool SceneEditor::remove_material(const std::string& id) {
    auto it = std::find_if(doc_.materials.begin(), doc_.materials.end(),
                           [&](const io::MaterialDoc& m) { return m.id == id; });
    if (it == doc_.materials.end())
        return false;
    for (const auto& el : doc_.elements)
        if (el.material_id == id)
            return false;  // still bound: a surface borrows the raw pointer
    doc_.materials.erase(it);
    scene_->remove_material(id);
    dirty_ = true;
    return true;
}

// ---- add_source / remove_source ---------------------------------------------

std::size_t SceneEditor::add_source(io::SourceDoc sd) {
    auto src = io::build_source(sd, *scene_);  // throws before any mutation
    const std::size_t idx = scene_->add_source(std::move(src));
    doc_.sources.push_back(std::move(sd));
    dirty_ = true;
    return idx;
}

bool SceneEditor::remove_source(std::size_t index) {
    if (index >= doc_.sources.size() || index >= scene_->sources().size())
        return false;
    doc_.sources.erase(doc_.sources.begin() + static_cast<std::ptrdiff_t>(index));
    scene_->remove_source(index);
    dirty_ = true;
    return true;
}

// ---- commit_trace_config ------------------------------------------------

void SceneEditor::commit_trace_config(const tracer::TraceConfig& cfg) {
    doc_.trace = cfg;
    dirty_     = true;
}

} // namespace scrt::scene
