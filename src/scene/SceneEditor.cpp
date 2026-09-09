#include "scrt/scene/SceneEditor.hpp"
#include "scrt/core/AABB.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/io/MeshImporter.hpp"
#include "scrt/materials/Material.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/surfaces/CylindricalParaboloid.hpp"
#include "scrt/surfaces/FresnelZoneLens.hpp"
#include "scrt/surfaces/GeneralQuadric.hpp"
#include "scrt/surfaces/Paraboloid.hpp"
#include "scrt/surfaces/Plane.hpp"
#include "scrt/surfaces/Sphere.hpp"
#include "scrt/surfaces/TriangleMesh.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

// NOTE ON SCOPE: the class contract, including the document/scene sync discipline every
// function here obeys, is documented on SceneEditor in SceneEditor.hpp. This file implements
// it and nothing else.

namespace scrt::scene {

namespace {

/// True when every field of t is still its struct default, i.e. the element has no transform.
///
/// Byte-for-byte copy of the private helper of the same name in SceneLoader.cpp (which is in an
/// anonymous namespace and therefore unreachable). It exists for the same reason there: an
/// element with no transform must keep the surface's default-constructed core::Transform rather
/// than going through core::Transform::from_trs(), so a document-derived surface is bitwise
/// identical to a loader-derived one.
bool is_default_transform(const io::TransformDoc& t) {
    return t.translation == math::vec3{0.0} &&
           t.rotation_euler_deg == math::vec3{0.0} &&
           t.scale == math::vec3{1.0} &&
           !t.matrix.has_value();
}

/// Constructs the surface geometry for one element, dispatching on SurfaceDoc's active
/// alternative; mesh paths resolve against base_dir.
///
/// Deliberate duplicate of build_surface() in src/io/SceneLoader.cpp, which is file-local
/// (anonymous namespace) and so cannot be called from here. Kept branch-for-branch identical,
/// including the shared io::import_mesh() entry point, so the editor and the loader can never
/// disagree about what an ElementDoc means. If a future wave promotes the loader's copy to a
/// public io:: helper, delete this one and call it.
std::unique_ptr<surfaces::Surface> build_surface(const io::SurfaceDoc& sd,
                                                  const std::filesystem::path& base_dir) {
    return std::visit(
        [&](const auto& s) -> std::unique_ptr<surfaces::Surface> {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, io::PlaneDoc>) {
                return std::make_unique<surfaces::Plane>(s.half_width, s.half_height);
            } else if constexpr (std::is_same_v<T, io::SphereDoc>) {
                return std::make_unique<surfaces::Sphere>(s.radius);
            } else if constexpr (std::is_same_v<T, io::ParaboloidDoc>) {
                return std::make_unique<surfaces::Paraboloid>(s.focal_length_m,
                                                               s.aperture_radius_m);
            } else if constexpr (std::is_same_v<T, io::CylParaboloidDoc>) {
                return std::make_unique<surfaces::CylindricalParaboloid>(
                    s.focal_length_m, s.aperture_half_width_m, s.aperture_half_length_m);
            } else if constexpr (std::is_same_v<T, io::QuadricDoc>) {
                surfaces::QuadricCoeffs c;
                c.A = s.A; c.B = s.B; c.C = s.C; c.D = s.D; c.E = s.E;
                c.F = s.F; c.G = s.G; c.H = s.H; c.I = s.I; c.J = s.J;
                return std::make_unique<surfaces::GeneralQuadric>(
                    c, core::AABB{s.box_min, s.box_max});
            } else if constexpr (std::is_same_v<T, io::FresnelZoneLensDoc>) {
                return std::make_unique<surfaces::FresnelZoneLens>(
                    s.focal_length_m, s.inner_radius_m, s.pitch_m, s.n_zones, s.n_lens);
            } else if constexpr (std::is_same_v<T, io::MeshDoc>) {
                // io::import_mesh() is the process-wide cached importer (src/io/MeshImporter.cpp),
                // so duplicating a mesh element costs one vector copy, not a second Assimp parse.
                auto imp = io::import_mesh(base_dir / s.path, s.scale_to_meters);
                return std::make_unique<surfaces::TriangleMesh>(std::move(imp.vertices),
                                                                 std::move(imp.indices));
            }
        },
        sd);
}

} // namespace

// ---- Construction --------------------------------------------------------

SceneEditor::SceneEditor(io::LoadedScene loaded, std::filesystem::path base_dir)
    : SceneEditor(std::move(loaded), io::SceneDocument{}, std::move(base_dir)) {}

// Deliberately NOT delegating to the (LoadedScene, SceneDocument, path) constructor: the
// arguments of a delegating mem-initializer are evaluated in unspecified order, so
// `io::build_scene(doc, ...)` could run after `std::move(doc)` had already gutted it.
SceneEditor::SceneEditor(io::SceneDocument doc, std::filesystem::path base_dir)
    : doc_(std::move(doc)), base_dir_(std::move(base_dir)) {
    io::LoadedScene loaded = io::build_scene(doc_, base_dir_);
    scene_ = std::move(loaded.scene);
    cfg_   = loaded.cfg;
    if (!scene_)
        throw std::runtime_error("SceneEditor: io::build_scene returned no scene");
    seed_id_allocator();
}

SceneEditor::SceneEditor(io::LoadedScene loaded, io::SceneDocument doc,
                         std::filesystem::path base_dir)
    : doc_(std::move(doc)),
      scene_(std::move(loaded.scene)),
      cfg_(loaded.cfg),
      base_dir_(std::move(base_dir)) {
    if (!scene_)
        throw std::runtime_error("SceneEditor: constructed from a LoadedScene with no scene");
    seed_id_allocator();
}

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

    auto surf = build_surface(d.surface, base_dir_);

    if (!is_default_transform(d.transform))
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

} // namespace scrt::scene
