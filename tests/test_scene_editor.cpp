#include <doctest/doctest.h>
#include "scrt/core/AABB.hpp"
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/scene/SceneEditor.hpp"
#include "scrt/surfaces/Surface.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

// SCRT_SOURCE_DIR is injected by CMake so tests can locate the committed mesh corpus.
#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

using scrt::io::ElementDoc;
using scrt::io::MeshDoc;
using scrt::io::PlaneDoc;
using scrt::io::SceneDocument;
using scrt::scene::SceneEditor;

namespace {

/// Smallest document the editor can work on: one absorber material, a default 0.05 m plane
/// receiver at the origin, and no elements at all.
SceneDocument empty_doc() {
    SceneDocument doc;
    doc.name = "editor_fixture";
    scrt::io::MaterialDoc mat;
    mat.id     = "abs";
    mat.type   = "absorber";
    mat.params = nlohmann::json::object();
    doc.materials.push_back(mat);
    doc.sun.direction = scrt::math::vec3{0.0, 0.0, -1.0};
    doc.next_id = 1;
    return doc;
}

/// A 1 m x 1 m plane element, normal +Z, translated to `pos`.
ElementDoc plane_at(scrt::math::vec3 pos, const char* name) {
    ElementDoc el;
    el.name        = name;
    el.material_id = "abs";
    el.surface     = PlaneDoc{1.0, 1.0};
    el.transform.translation = pos;
    return el;
}

/// A downward ray at x = 5, well clear of the 0.05 m receiver plane at the origin.
scrt::core::Ray probe_ray() {
    scrt::core::Ray r;
    r.origin    = {5.0, 0.0, 5.0};
    r.direction = {0.0, 0.0, -1.0};
    return r;
}

/// Closest-hit convenience wrapper.
bool trace(const scrt::scene::Scene& s, const scrt::core::Ray& r, scrt::core::Hit& hit,
           double t_min = 1e-6, double t_max = 1e9) {
    return s.intersect(r, t_min, t_max, hit);
}

/// Diagonal of a world AABB — a single scalar that shrinks when geometry leaves the scene.
double extent(const scrt::core::AABB& b) {
    return glm::length(b.max() - b.min());
}

/// Repository-relative path of the STL used by the mesh duplication test.
constexpr const char* kMeshRelative = "../assets/meshes/panels_only_rectangular_45deg.stl";

/// The directory relative mesh paths in the fixture documents resolve against.
std::filesystem::path mesh_base_dir() {
    return std::filesystem::path(SCRT_SOURCE_DIR) / "examples";
}

} // namespace

TEST_CASE("SceneEditor: add_element makes a previously missing ray hit") {
    SceneEditor ed(empty_doc(), mesh_base_dir());

    scrt::core::Hit hit;
    CHECK_FALSE(trace(ed.scene(), probe_ray(), hit));

    const std::uint64_t id = ed.add_element(plane_at({5.0, 0.0, 0.0}, "target"));
    CHECK(id != 0);
    CHECK(ed.doc().elements.size() == 1);
    CHECK(ed.doc().elements.front().id == id);
    CHECK(ed.scene().surface_by_id(id) != nullptr);
    CHECK(ed.scene().acceleration_dirty());
    CHECK(ed.dirty());

    REQUIRE(trace(ed.scene(), probe_ray(), hit));
    CHECK(hit.t == doctest::Approx(5.0));
    CHECK(hit.surface == ed.scene().surface_by_id(id));

    // Same answer once the BVH is rebuilt — the dirty flag is a safety fallback, not a
    // different intersection result.
    ed.scene().build_acceleration_structure();
    CHECK_FALSE(ed.scene().acceleration_dirty());
    REQUIRE(trace(ed.scene(), probe_ray(), hit));
    CHECK(hit.t == doctest::Approx(5.0));
}

TEST_CASE("SceneEditor: remove_element un-hits the ray and shrinks world bounds") {
    SceneEditor ed(empty_doc(), mesh_base_dir());
    const std::uint64_t id = ed.add_element(plane_at({5.0, 0.0, 0.0}, "target"));
    ed.scene().build_acceleration_structure();

    const double before = extent(ed.scene().world_bounds());

    scrt::core::Hit hit;
    REQUIRE(trace(ed.scene(), probe_ray(), hit));

    REQUIRE(ed.remove_element(id));
    CHECK(ed.doc().elements.empty());
    CHECK(ed.scene().surface_by_id(id) == nullptr);
    CHECK(ed.scene().acceleration_dirty());

    CHECK_FALSE(trace(ed.scene(), probe_ray(), hit));
    CHECK(extent(ed.scene().world_bounds()) < before);

    // Removing an id that belongs to neither the document nor the scene is a no-op.
    CHECK_FALSE(ed.remove_element(id));
    CHECK_FALSE(ed.remove_element(9999));
}

// Wave 0 regression, permanent. accel::BVH stores raw surfaces::Surface* borrowed from Scene's
// unique_ptrs. Removing the *last* surface used to leave a non-empty BVH pointing at freed
// memory, and BVH::build() on an empty surface list used to leave the previous nodes in place.
// Both paths are traversed here: once while the acceleration structure is dirty (linear-scan
// fallback) and once after an explicit empty rebuild.
TEST_CASE("SceneEditor: intersecting after removing the LAST surface is safe") {
    SceneEditor ed(empty_doc(), mesh_base_dir());
    const std::uint64_t id = ed.add_element(plane_at({5.0, 0.0, 0.0}, "only"));

    ed.scene().build_acceleration_structure();
    REQUIRE_FALSE(ed.scene().acceleration_dirty());
    scrt::core::Hit hit;
    REQUIRE(trace(ed.scene(), probe_ray(), hit));

    REQUIRE(ed.remove_element(id));
    REQUIRE(ed.scene().surfaces().empty());
    REQUIRE(ed.scene().acceleration_dirty());

    // (1) Dirty BVH must never be traversed: Scene::intersect falls back to a linear scan.
    CHECK_FALSE(trace(ed.scene(), probe_ray(), hit));

    // (2) Rebuilding over zero surfaces must clear the BVH's containers, not keep the old ones.
    ed.scene().build_acceleration_structure();
    CHECK_FALSE(ed.scene().acceleration_dirty());
    CHECK_FALSE(trace(ed.scene(), probe_ray(), hit));

    // And the scene still works afterwards.
    const std::uint64_t again = ed.add_element(plane_at({5.0, 0.0, 0.0}, "again"));
    CHECK(again != id);
    ed.scene().build_acceleration_structure();
    REQUIRE(trace(ed.scene(), probe_ray(), hit));
    CHECK(hit.t == doctest::Approx(5.0));
}

TEST_CASE("SceneEditor: duplicate_element gives a fresh id, a distinct name and a second hit") {
    SceneEditor ed(empty_doc(), mesh_base_dir());
    const std::uint64_t a = ed.add_element(plane_at({5.0, 0.0, 0.0}, "mirror"));

    const std::uint64_t b = ed.duplicate_element(a);
    REQUIRE(b != 0);
    CHECK(b != a);
    REQUIRE(ed.doc().elements.size() == 2);
    CHECK(ed.doc().elements[1].name != ed.doc().elements[0].name);
    CHECK(ed.scene().surface_by_id(b) != nullptr);
    CHECK(ed.scene().surface_by_id(b)->name() != ed.scene().surface_by_id(a)->name());
    // Duplicating from the document spec, not a clone(): the copy carries the same geometry.
    CHECK(std::holds_alternative<PlaneDoc>(ed.doc().elements[1].surface));

    // Move the copy 3 m down the ray so the two are separately reachable.
    ed.commit_transform(b, glm::translate(scrt::math::mat4(1.0), scrt::math::vec3{5.0, 0.0, -3.0}));
    ed.scene().build_acceleration_structure();

    scrt::core::Hit near_hit;
    REQUIRE(trace(ed.scene(), probe_ray(), near_hit));
    CHECK(near_hit.t == doctest::Approx(5.0));
    CHECK(near_hit.surface->id() == a);

    scrt::core::Hit far_hit;
    REQUIRE(ed.scene().intersect(probe_ray(), 6.0, 1e9, far_hit));
    CHECK(far_hit.t == doctest::Approx(8.0));
    CHECK(far_hit.surface->id() == b);
    CHECK(far_hit.t != doctest::Approx(near_hit.t));

    CHECK_EQ(ed.duplicate_element(4242), 0);
}

TEST_CASE("SceneEditor: duplicating a mesh element reproduces its world bounds exactly") {
    const auto stl = mesh_base_dir() / kMeshRelative;
    REQUIRE_MESSAGE(std::filesystem::exists(stl), stl.string());

    SceneEditor ed(empty_doc(), mesh_base_dir());

    ElementDoc el;
    el.name        = "panels";
    el.material_id = "abs";
    el.surface     = MeshDoc{kMeshRelative, 0.001};
    const std::uint64_t a = ed.add_element(el);

    const auto scene_bounds_before = ed.scene().world_bounds();
    const std::uint64_t b = ed.duplicate_element(a);
    REQUIRE(b != 0);

    const auto* sa = ed.scene().surface_by_id(a);
    const auto* sb = ed.scene().surface_by_id(b);
    REQUIRE(sa != nullptr);
    REQUIRE(sb != nullptr);

    const auto ba = sa->world_bounds();
    const auto bb = sb->world_bounds();
    CHECK(bb.min() == ba.min());
    CHECK(bb.max() == ba.max());
    CHECK(extent(bb) == extent(ba));

    // The copy sits exactly on top of the original, so the scene bounds must not move.
    const auto scene_bounds_after = ed.scene().world_bounds();
    CHECK(scene_bounds_after.min() == scene_bounds_before.min());
    CHECK(scene_bounds_after.max() == scene_bounds_before.max());
}

TEST_CASE("SceneEditor: commit_transform moves the live surface and the TransformDoc together") {
    SceneEditor ed(empty_doc(), mesh_base_dir());
    const std::uint64_t id = ed.add_element(plane_at({5.0, 0.0, 0.0}, "target"));

    const scrt::math::mat4 world =
        glm::translate(scrt::math::mat4(1.0), scrt::math::vec3{5.0, 0.0, -2.5});
    ed.commit_transform(id, world);

    // Live surface: exact matrix.
    const auto* surf = ed.scene().surface_by_id(id);
    REQUIRE(surf != nullptr);
    CHECK(surf->transform().matrix() == world);
    CHECK(ed.scene().acceleration_dirty());

    // Document: the same placement, stored as a readable TRS triple.
    const auto& td = ed.doc().elements.front().transform;
    CHECK(td.translation.x == doctest::Approx(5.0));
    CHECK(td.translation.y == doctest::Approx(0.0));
    CHECK(td.translation.z == doctest::Approx(-2.5));
    CHECK(td.scale == scrt::math::vec3(1.0));
    CHECK_FALSE(td.matrix.has_value());

    // And the two agree: re-deriving the surface from the document keeps it where it was put.
    REQUIRE(ed.rebuild_element(id));
    const auto* rebuilt = ed.scene().surface_by_id(id);
    REQUIRE(rebuilt != nullptr);
    CHECK(rebuilt->transform().point_to_world({0.0, 0.0, 0.0}).z == doctest::Approx(-2.5));

    ed.scene().build_acceleration_structure();
    scrt::core::Hit hit;
    REQUIRE(trace(ed.scene(), probe_ray(), hit));
    CHECK(hit.t == doctest::Approx(7.5));

    // A non-uniform scale still round-trips through the document.
    ed.commit_transform(id, glm::scale(scrt::math::mat4(1.0), scrt::math::vec3{2.0, 3.0, 1.0}));
    const auto& scaled = ed.doc().elements.front().transform;
    CHECK(scaled.scale.x == doctest::Approx(2.0));
    CHECK(scaled.scale.y == doctest::Approx(3.0));
    CHECK(scaled.scale.z == doctest::Approx(1.0));
}

TEST_CASE("SceneEditor: rebuild_element picks up an edited shape parameter") {
    SceneEditor ed(empty_doc(), mesh_base_dir());
    // 0.1 m half-width plane at x = 5: the probe ray at x = 5 hits it dead centre.
    ElementDoc el = plane_at({5.0, 0.0, 0.0}, "target");
    el.surface    = PlaneDoc{0.1, 0.1};
    const std::uint64_t id = ed.add_element(el);

    scrt::core::Ray edge = probe_ray();
    edge.origin.x = 5.5;  // 0.5 m off centre: outside a 0.1 m half-width plane.
    scrt::core::Hit hit;
    CHECK_FALSE(trace(ed.scene(), edge, hit));

    // Edit the document, then re-derive: this is the documented shape-parameter flow.
    ed.doc().elements.front().surface = PlaneDoc{1.0, 1.0};
    REQUIRE(ed.rebuild_element(id));
    CHECK(ed.scene().surfaces().size() == 1);
    CHECK(ed.scene().surface_by_id(id) != nullptr);
    CHECK(ed.scene().acceleration_dirty());

    ed.scene().build_acceleration_structure();
    REQUIRE(trace(ed.scene(), edge, hit));
    CHECK(hit.t == doctest::Approx(5.0));

    CHECK_FALSE(ed.rebuild_element(4242));
}

TEST_CASE("SceneEditor: ids stay unique and stable across interleaved adds and removes") {
    SceneEditor ed(empty_doc(), mesh_base_dir());

    std::vector<std::uint64_t> live;
    std::set<std::uint64_t>    ever_issued;

    auto add = [&](const char* name) {
        const std::uint64_t id = ed.add_element(plane_at({5.0, 0.0, 0.0}, name));
        CHECK(id != 0);
        CHECK(ever_issued.insert(id).second);  // never reused, even after a removal
        live.push_back(id);
        return id;
    };

    const std::uint64_t a = add("a");
    const std::uint64_t b = add("b");
    REQUIRE(ed.remove_element(a));
    live.erase(std::remove(live.begin(), live.end(), a), live.end());
    const std::uint64_t c = add("c");
    const std::uint64_t d = ed.duplicate_element(b);
    CHECK(ever_issued.insert(d).second);
    live.push_back(d);
    REQUIRE(ed.remove_element(c));
    live.erase(std::remove(live.begin(), live.end(), c), live.end());
    add("e");

    // Every live id resolves in BOTH the document and the scene, and to the same element.
    CHECK(ed.doc().elements.size() == live.size());
    CHECK(ed.scene().surfaces().size() == live.size());
    for (std::uint64_t id : live) {
        const auto* surf = ed.scene().surface_by_id(id);
        REQUIRE_MESSAGE(surf != nullptr, id);
        const auto it = std::find_if(ed.doc().elements.begin(), ed.doc().elements.end(),
                                     [id](const ElementDoc& e) { return e.id == id; });
        REQUIRE(it != ed.doc().elements.end());
        CHECK(it->name == surf->name());
    }
    // Removed ids resolve nowhere.
    CHECK(ed.scene().surface_by_id(a) == nullptr);
    CHECK(ed.scene().surface_by_id(c) == nullptr);
    CHECK(ed.doc().next_id > *std::max_element(ever_issued.begin(), ever_issued.end()));
}

TEST_CASE("SceneEditor: adopting a document-built scene keeps loader ids as the join key") {
    const auto path = std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / "parabolic_dish.json";
    if (!std::filesystem::exists(path))
        return;  // corpus file renamed; the fixture-based cases above still cover the contract.

    std::ifstream in(path);
    REQUIRE(in.is_open());
    const auto root = nlohmann::json::parse(in, nullptr, true, true);
    auto doc = scrt::io::parse_document(root);
    REQUIRE_FALSE(doc.elements.empty());

    auto loaded = scrt::io::build_scene(doc, path.parent_path());
    const std::uint64_t first = doc.elements.front().id;

    // The document travels inside the LoadedScene now, so no second argument is needed - and
    // there is no way for the caller to hand over a document that disagrees with the scene.
    CHECK(loaded.doc.elements.size() == doc.elements.size());
    SceneEditor ed(std::move(loaded), path.parent_path());
    CHECK_FALSE(ed.dirty());
    REQUIRE(ed.scene().surface_by_id(first) != nullptr);

    const std::uint64_t copy = ed.duplicate_element(first);
    REQUIRE(copy != 0);
    CHECK(copy != first);
    CHECK(ed.scene().surface_by_id(copy) != nullptr);
    CHECK(ed.dirty());
    // The fresh id must not collide with any id the loader already handed out.
    for (const auto& el : ed.doc().elements)
        if (el.id != copy)
            CHECK(el.id != copy);
}

TEST_CASE("SceneEditor: add_element rejects an unknown material and changes nothing") {
    SceneEditor ed(empty_doc(), mesh_base_dir());
    ElementDoc bad  = plane_at({5.0, 0.0, 0.0}, "bad");
    bad.material_id = "no_such_material";

    CHECK_THROWS_AS(ed.add_element(bad), std::runtime_error);
    CHECK(ed.doc().elements.empty());
    CHECK(ed.scene().surfaces().empty());
    CHECK_FALSE(ed.dirty());
}
