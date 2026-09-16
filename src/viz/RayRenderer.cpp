#include "scrt/viz/RayRenderer.hpp"
#include "scrt/viz/Appearance.hpp"
#include "scrt/viz/Body.hpp"
#include "scrt/viz/Snap.hpp"
#include "scrt/viz/Align.hpp"
#include "scrt/sources/Laser.hpp"
#include "scrt/viz/ViewSettings.hpp"
#include "scrt/materials/Material.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/scene/Aperture.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "polyscope/curve_network.h"
#include "polyscope/polyscope.h"
#include "polyscope/surface_mesh.h"

namespace scrt::viz {

namespace {

void to_poly(const std::vector<math::vec3>& in,
             std::vector<std::array<double, 3>>& out) {
    out.clear();
    out.reserve(in.size());
    for (const auto& v : in)
        out.push_back({v.x, v.y, v.z});
}

void to_faces(const std::vector<std::uint32_t>& idx,
              std::vector<std::array<std::uint32_t, 3>>& out) {
    out.clear();
    out.reserve(idx.size() / 3);
    for (std::size_t k = 0; k + 2 < idx.size(); k += 3)
        out.push_back({idx[k], idx[k + 1], idx[k + 2]});
}

std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

/// Base (pre-uniquification) display name for a surface; stable under reordering.
std::string base_display_name(const surfaces::Surface& surf) {
    return surf.name().empty()
               ? "surface_" + std::to_string(surf.id())
               : surf.name();
}

/// Re-applies the surface's default Polyscope appearance after a (re-)registration.
///
/// Dispatched on the MATERIAL TYPE (viz::appearance_for). The previous rule matched the strings
/// "glass" and "pmma" in the surface's or the material's name, so a dielectric named
/// "lens_body" drew as an opaque grey slab and an absorber named "glass_pot" drew as glass.
void apply_default_style(polyscope::SurfaceMesh* mesh, const surfaces::Surface& surf) {
    const Appearance a = appearance_for(surf.material());
    mesh->setMaterial(a.matcap);
    mesh->setSurfaceColor({static_cast<float>(a.color.x), static_cast<float>(a.color.y),
                           static_cast<float>(a.color.z)});
    // Smooth shading matters for curved bodies: Polyscope defaults to flat, which makes a
    // tessellated lens or dish look faceted at any segment count this app actually uses.
    mesh->setSmoothShade(a.smooth);
    if (a.identical_backface)
        mesh->setBackFacePolicy(polyscope::BackFacePolicy::Identical);
    if (a.transparency < 1.0f) {
        mesh->setTransparency(a.transparency);
        // Polyscope composites transparency only in a transparency mode. The aperture disk
        // already turns this on for every solar scene; a bench scene may have no aperture, so
        // assert it here too rather than rely on that.
        if (polyscope::options::transparencyMode == polyscope::TransparencyMode::None)
            polyscope::options::transparencyMode = polyscope::TransparencyMode::Pretty;
    }
}

/// Tessellates surf and converts to Polyscope's vertex/face arrays; false when degenerate.
bool tessellate_for_polyscope(const surfaces::Surface& surf, int tess_segs,
                              std::vector<std::array<double, 3>>& pv,
                              std::vector<std::array<std::uint32_t, 3>>& pf) {
    std::vector<math::vec3>    verts;
    std::vector<std::uint32_t> indices;
    surf.tessellate(tess_segs, verts, indices);
    if (verts.empty() || indices.size() < 3) return false;
    to_poly(verts, pv);
    to_faces(indices, pf);
    return true;
}

const std::string kEmptyName;

} // namespace

// ---- StructureRegistry -------------------------------------------------------

StructureRegistry& StructureRegistry::instance() {
    static StructureRegistry reg;
    return reg;
}

void StructureRegistry::clear() {
    name_to_id_.clear();
    id_to_name_.clear();
    baked_inv_.clear();
    bodies_.clear();
}

std::string StructureRegistry::intern(std::uint64_t id, const std::string& base_name) {
    erase(id);
    const std::string base = base_name.empty() ? "surface_" + std::to_string(id) : base_name;
    std::string name = base;
    for (int suffix = 2; name_to_id_.find(name) != name_to_id_.end(); ++suffix)
        name = base + " (" + std::to_string(suffix) + ")";
    name_to_id_[name] = id;
    id_to_name_[id]   = name;
    return name;
}

const std::string& StructureRegistry::name_for(std::uint64_t id) const {
    auto it = id_to_name_.find(id);
    return it == id_to_name_.end() ? kEmptyName : it->second;
}

std::uint64_t StructureRegistry::id_for(const std::string& name) const {
    auto it = name_to_id_.find(name);
    return it == name_to_id_.end() ? 0u : it->second;
}

void StructureRegistry::erase(std::uint64_t id) {
    auto it = id_to_name_.find(id);
    if (it != id_to_name_.end()) {
        name_to_id_.erase(it->second);
        id_to_name_.erase(it);
    }
    baked_inv_.erase(id);
}

void StructureRegistry::set_baked(std::uint64_t id, const math::mat4& world) {
    baked_inv_[id] = glm::inverse(world);
}

math::mat4 StructureRegistry::baked_inverse(std::uint64_t id) const {
    auto it = baked_inv_.find(id);
    return it == baked_inv_.end() ? math::mat4(1.0) : it->second;
}

bool StructureRegistry::contains(std::uint64_t id) const {
    return id_to_name_.find(id) != id_to_name_.end();
}

// Bodies are deliberately NOT dropped by erase(): intern() calls erase() on every
// re-registration, and a renderer with no document (the transform panel's) could not put the
// body back. They go with remove_surface_structure() and clear().
void StructureRegistry::set_body(std::uint64_t id, const std::optional<io::BodyDoc>& body) {
    if (body) bodies_[id] = *body;
    else      bodies_.erase(id);
}

const io::BodyDoc* StructureRegistry::body_for(std::uint64_t id) const {
    auto it = bodies_.find(id);
    return it == bodies_.end() ? nullptr : &it->second;
}

// ---- structure naming helpers ------------------------------------------------

std::string body_mount_structure_name(const std::string& s) { return s + " [mount]"; }
std::string body_post_structure_name(const std::string& s) { return s + " [post]"; }

const char* aperture_structure_name() { return "aperture"; }

std::string receiver_flux_structure_name(bool multi_face, const std::string& face_name) {
    return multi_face ? "receiver_" + face_name + "_flux" : std::string("receiver_flux");
}

// ---- registration ------------------------------------------------------------

void RayRenderer::register_surfaces(int tess_segs) {
    if (!scene_) return;
    auto& reg = StructureRegistry::instance();
    reg.clear();

    for (const auto& surf : scene_->surfaces()) {
        std::vector<std::array<double, 3>>       pv;
        std::vector<std::array<std::uint32_t, 3>> pf;
        if (!tessellate_for_polyscope(*surf, tess_segs, pv, pf)) continue;

        const std::string name = reg.intern(surf->id(), base_display_name(*surf));
        reg.set_baked(surf->id(), surf->transform().matrix());

        auto* mesh = polyscope::registerSurfaceMesh(name, pv, pf);
        mesh->resetTransform();
        apply_default_style(mesh, *surf);
        record_body(surf->id());
        sync_body(surf->id());
    }
}

// ---- drawn-only bodies --------------------------------------------------------

void RayRenderer::record_body(std::uint64_t id) {
    if (!doc_) return;
    for (const auto& el : doc_->elements) {
        if (el.id != id) continue;
        StructureRegistry::instance().set_body(id, el.body);
        return;
    }
    StructureRegistry::instance().set_body(id, std::nullopt);
}

void RayRenderer::sync_body(std::uint64_t id) {
    if (!scene_) return;
    auto&              reg  = StructureRegistry::instance();
    const std::string& nm   = reg.name_for(id);
    const auto*        surf = scene_->surface_by_id(id);
    if (nm.empty() || !surf) return;
    const io::BodyDoc* body = reg.body_for(id);

    // Bodies are tessellated in WORLD space under the current transform every time, rather than
    // baked once and moved with setTransform like the surface. A post must stay vertical and
    // reach the floor however the part is turned or lifted, which no rigid transform of the
    // registered mesh can do. A body is a few dozen vertices, so this costs nothing per frame.
    for (const BodyPart part : {BodyPart::Mount, BodyPart::Post}) {
        const std::string sname = (part == BodyPart::Mount) ? body_mount_structure_name(nm)
                                                            : body_post_structure_name(nm);
        std::vector<math::vec3>    verts;
        std::vector<std::uint32_t> idx;
        // Posts can be hidden (Settings); the substrate or cube cannot, because hiding it would
        // hide what the part IS rather than what it stands on.
        const bool shown = (part != BodyPart::Post) || show_posts();
        const bool want  = shown && body && tessellate_body(*surf, *body, part, verts, idx);
        const bool have = polyscope::hasSurfaceMesh(sname);
        if (!want) {
            if (have) polyscope::removeSurfaceMesh(sname, false);
            continue;
        }
        std::vector<std::array<double, 3>>        pv;
        std::vector<std::array<std::uint32_t, 3>> pf;
        to_poly(verts, pv);
        if (have && polyscope::getSurfaceMesh(sname)->nVertices() == pv.size()) {
            polyscope::getSurfaceMesh(sname)->updateVertexPositions(pv);
            continue;
        }
        if (have) polyscope::removeSurfaceMesh(sname, false);
        to_faces(idx, pf);
        auto* mesh = polyscope::registerSurfaceMesh(sname, pv, pf);
        mesh->setBackFacePolicy(polyscope::BackFacePolicy::Identical);
        mesh->setMaterial("clay");
        if (part == BodyPart::Post) {
            mesh->setSurfaceColor({0.045f, 0.045f, 0.05f});        // black anodised post
        } else if (body->cube) {
            mesh->setSurfaceColor({0.70f, 0.85f, 0.95f});          // the cube's glass
            mesh->setTransparency(0.22f);
            if (polyscope::options::transparencyMode == polyscope::TransparencyMode::None)
                polyscope::options::transparencyMode = polyscope::TransparencyMode::Pretty;
        } else {
            mesh->setSurfaceColor({0.20f, 0.20f, 0.23f});          // black-anodised substrate
        }
    }
}

void RayRenderer::sync_bodies() {
    if (!scene_) return;
    for (const auto& s : scene_->surfaces()) sync_body(s->id());
}

void RayRenderer::remove_body_structures(const std::string& surface_structure) {
    for (const std::string& s : {body_mount_structure_name(surface_structure),
                                 body_post_structure_name(surface_structure)})
        if (polyscope::hasSurfaceMesh(s)) polyscope::removeSurfaceMesh(s, false);
}

void RayRenderer::register_aperture() {
    // Presentation only: the disk belongs to the sun, and a scene without one has nothing to
    // draw. polyscope::removeAllStructures() runs before every register_scene(), so returning
    // early cannot leave a stale disk on screen.
    const auto* ap_ptr = scene_->display_aperture();
    if (!ap_ptr) return;
    const auto& ap = *ap_ptr;
    math::vec3 u, v;
    ap.tangent_frame(u, v);

    constexpr int N = 64;
    std::vector<std::array<double, 3>>      pv;
    std::vector<std::array<std::uint32_t, 3>> pf;

    pv.push_back({ap.center.x, ap.center.y, ap.center.z}); // hub
    for (int i = 0; i <= N; ++i) {
        double phi = math::TWO_PI * i / N;
        math::vec3 p = ap.center + ap.radius * (std::cos(phi) * u + std::sin(phi) * v);
        pv.push_back({p.x, p.y, p.z});
    }
    for (std::uint32_t i = 1; i <= static_cast<std::uint32_t>(N); ++i)
        pf.push_back({0u, i, i < static_cast<std::uint32_t>(N) ? i + 1u : 1u});

    auto* mesh = polyscope::registerSurfaceMesh(aperture_structure_name(), pv, pf);
    mesh->setSurfaceColor({0.3f, 0.7f, 1.0f});
    mesh->setTransparency(0.4f);
}

void RayRenderer::register_paths(const tracer::TraceResult& result) {
    if (result.sampled_paths.empty()) return;

    std::vector<std::array<double, 3>>  nodes;
    std::vector<std::array<std::size_t, 2>> edges;

    // A path is a tree now, so its edges are given rather than implied by adjacency. The old
    // "join consecutive points" loop drew a split as a line that ran out along the reflected
    // branch and then jumped back to the split point - a picture of something that never
    // happened, and about to become the common case once beam splitters exist.
    for (const auto& path : result.sampled_paths) {
        if (path.edges.empty()) continue;
        const std::size_t base = nodes.size();
        for (const auto& p : path.nodes)
            nodes.push_back({p.x, p.y, p.z});
        for (const auto& e : path.edges)
            edges.push_back({base + e[0], base + e[1]});
    }

    if (nodes.empty()) return;

    auto* net = polyscope::registerCurveNetwork("ray_paths", nodes, edges);
    net->setColor({1.0f, 0.85f, 0.2f});
    apply_ray_appearance();
}

void apply_ray_appearance() {
    if (!polyscope::isInitialized()) return;
    if (!polyscope::hasCurveNetwork("ray_paths")) return;
    auto* net = polyscope::getCurveNetwork("ray_paths");

    // Absolute (isRelative = false). The default is relative to Polyscope's global lengthScale,
    // so any change in scene extent rescaled every ray - a scaled-up object turned the ray paths
    // into fat sausages that swallowed the model.
    net->setRadius(ray_radius_m(), false);

    const float a = ray_opacity();
    net->setTransparency(a);
    // Polyscope only composites transparency when the render engine is in a transparency mode;
    // the aperture already turns it on for every scene, so this is a no-op re-assertion rather
    // than a global change made on the rays' behalf.
    if (a < 1.0f && polyscope::options::transparencyMode == polyscope::TransparencyMode::None)
        polyscope::options::transparencyMode = polyscope::TransparencyMode::Pretty;
    polyscope::requestRedraw();
}

const char* grid_structure_name() { return "placement_grid"; }

namespace {

/// A curve network that is left OUT of Polyscope's scene extents.
///
/// Every registered structure is folded into state::boundingBox and lengthScale, which size the
/// ground plane and every "relative" length. The design note (4.4) says a CurveNetwork cannot opt
/// out; it can: Structure::hasExtents() is virtual and CurveNetwork does not override it. So the
/// grid and the alignment axis can reach past the scene without resizing anything.
class ExtentlessCurveNetwork : public polyscope::CurveNetwork {
public:
    using polyscope::CurveNetwork::CurveNetwork;
    bool hasExtents() override { return false; }
};

/// Registers (replacing) a decoration line set that does not count toward the scene extents.
polyscope::CurveNetwork* register_decoration_lines(
    const std::string& name, const std::vector<glm::vec3>& nodes,
    const std::vector<std::array<std::size_t, 2>>& edges) {
    auto* s = new ExtentlessCurveNetwork(name, nodes, edges);
    if (!polyscope::registerStructure(s)) {
        delete s;
        return nullptr;
    }
    return s;
}

/// Polyscope's current scene box, which (with the decorations excluded) is exactly what the
/// scene draws: surfaces, bodies, receiver, aperture and ray paths. False when empty.
bool scene_box(glm::vec3& lo, glm::vec3& hi) {
    polyscope::updateStructureExtents();
    lo = std::get<0>(polyscope::state::boundingBox);
    hi = std::get<1>(polyscope::state::boundingBox);
    return lo.x <= hi.x && lo.y <= hi.y && lo.z <= hi.z;
}

} // namespace

void sync_grid(const scene::Scene* scene) {
    if (!polyscope::isInitialized()) return;
    static float drawn_step = -1.0f;
    const bool   want = scene && show_grid();
    const bool   has  = polyscope::hasCurveNetwork(grid_structure_name());
    const float  step = snap_settings().translate_m;
    if (!want) {
        if (has) polyscope::removeCurveNetwork(grid_structure_name(), false);
        return;
    }
    if (has && step == drawn_step) return;

    glm::vec3 lo, hi;
    if (has) polyscope::removeCurveNetwork(grid_structure_name(), false);
    if (!scene_box(lo, hi)) return;

    // At z = 0 when the scene reaches the floor (bodies' posts do), and 30% of the larger
    // horizontal span wider than the scene on every side - asked for by the user, who found a grid
    // that stopped exactly at the outermost part too tight to place anything beside it. Possible
    // only because the grid no longer counts toward the extents.
    const double z      = std::clamp(0.0, static_cast<double>(lo.z), static_cast<double>(hi.z));
    const double span   = std::max(hi.x - lo.x, hi.y - lo.y);
    const double margin = std::max(0.3 * span, 5.0 * static_cast<double>(step));
    const double x0 = lo.x - margin, x1 = hi.x + margin, y0 = lo.y - margin, y1 = hi.y + margin;

    double     ux = 0.0, uy = 0.0;
    const auto xs = grid_positions(x0, x1, step, 241, ux);
    const auto ys = grid_positions(y0, y1, step, 241, uy);
    if (xs.empty() || ys.empty()) return;

    std::vector<glm::vec3>                  nodes;
    std::vector<std::array<std::size_t, 2>> edges;
    auto seg = [&](double ax, double ay, double bx, double by) {
        edges.push_back({nodes.size(), nodes.size() + 1});
        nodes.emplace_back(ax, ay, z);
        nodes.emplace_back(bx, by, z);
    };
    for (double x : xs) seg(x, y0, x, y1);
    for (double y : ys) seg(x0, y, x1, y);

    auto* net = register_decoration_lines(grid_structure_name(), nodes, edges);
    if (!net) return;
    net->setColor({0.46f, 0.48f, 0.52f});
    // Absolute radius. Measured: 0.02 x a 1 cm step is 0.2 mm, sub-pixel on a 1.3 m cooker, and the
    // grid drew as speckle; a floor of 0.05% of the scene diagonal keeps a line about a pixel wide.
    const double diag = glm::length(glm::dvec3(hi - lo));
    net->setRadius(static_cast<float>(std::max(0.02 * std::max(ux, uy), 0.0005 * diag)), false);
    drawn_step = step;
}

void sync_axis(const scene::Scene* scene) {
    if (!polyscope::isInitialized()) return;
    static const char* const kName = "alignment_axis";
    static bool       drawn  = false;
    static AlignAxis  drawn_as{};
    AlignAxis&        axis   = align_axis();
    const bool        has    = polyscope::hasCurveNetwork(kName);

    // A load removes every structure: the axis belonged to the old scene, so start again from the
    // new scene's first laser rather than carry a line that may run through nothing.
    if (drawn && !has) {
        axis.valid = false;
        drawn      = false;
    }
    if (!axis.valid && scene) {
        for (const auto& src : scene->sources()) {
            if (const auto* laser = dynamic_cast<const sources::Laser*>(src.get())) {
                axis.origin    = laser->origin();
                axis.direction = laser->direction();
                axis.valid     = true;
                break;
            }
        }
    }

    const bool want = scene && axis.valid && axis.show;
    if (!want) {
        if (has) polyscope::removeCurveNetwork(kName, false);
        drawn = false;
        return;
    }
    const bool same = has && drawn_as.origin == axis.origin && drawn_as.direction == axis.direction;
    if (same) return;

    glm::vec3 lo, hi;
    if (has) polyscope::removeCurveNetwork(kName, false);
    if (!scene_box(lo, hi)) return;
    // Long enough to cross the whole scene from wherever the axis starts: centred on the point of
    // the axis nearest the scene's centre, one and a half scene diagonals long.
    const math::vec3 d    = glm::normalize(axis.direction);
    const math::vec3 c    = closest_point_on_axis(math::vec3(0.5f * (lo + hi)), axis.origin, d);
    const double     half = 0.75 * glm::length(glm::dvec3(hi - lo)) + 0.05;
    std::vector<glm::vec3> nodes = {glm::vec3(c - half * d), glm::vec3(c + half * d)};
    std::vector<std::array<std::size_t, 2>> edges = {{0, 1}};
    auto* net = register_decoration_lines(kName, nodes, edges);
    if (!net) return;
    net->setColor({0.95f, 0.42f, 0.22f});
    net->setRadius(static_cast<float>(std::max(0.0006, 0.001 * glm::length(glm::dvec3(hi - lo)))),
                   false);
    drawn    = true;
    drawn_as = axis;
}

void RayRenderer::clear() {
    polyscope::removeAllStructures();
    StructureRegistry::instance().clear();
}

// ---- incremental update paths ------------------------------------------------

void RayRenderer::set_surface_transform(std::uint64_t id, const core::Transform& world) {
    if (!scene_) return;
    auto* surf = scene_->surface_by_id(id);
    if (!surf) return;

    // Single source of truth: the physics transform and the display transform are both
    // written here, from the same `world` matrix. The uploaded vertices already carry the
    // transform baked in at registration time, so the structure only needs the difference.
    surf->set_transform(world);
    scene_->mark_acceleration_dirty();

    auto& reg = StructureRegistry::instance();
    const std::string& nm = reg.name_for(id);
    if (nm.empty() || !polyscope::hasSurfaceMesh(nm)) return;

    const math::mat4 display = world.matrix() * reg.baked_inverse(id);
    polyscope::getSurfaceMesh(nm)->setTransform(glm::mat4(display));
    sync_body(id);
}

void RayRenderer::update_surface_shape(std::uint64_t id, int tess_segs) {
    if (!scene_) return;
    auto* surf = scene_->surface_by_id(id);
    if (!surf) return;

    auto& reg = StructureRegistry::instance();
    const std::string nm = reg.name_for(id);
    if (nm.empty() || !polyscope::hasSurfaceMesh(nm)) {
        reregister_surface(id, tess_segs);
        return;
    }

    std::vector<std::array<double, 3>>       pv;
    std::vector<std::array<std::uint32_t, 3>> pf;
    if (!tessellate_for_polyscope(*surf, tess_segs, pv, pf)) return;

    auto* mesh = polyscope::getSurfaceMesh(nm);
    if (mesh->nVertices() != pv.size()) {
        reregister_surface(id, tess_segs); // vertex count changed: topology, not shape
        return;
    }

    // Positions were tessellated under the surface's current transform, so re-bake and
    // drop the display transform to keep the two from accumulating.
    mesh->updateVertexPositions(pv);
    mesh->resetTransform();
    reg.set_baked(id, surf->transform().matrix());
    scene_->mark_acceleration_dirty();
    sync_body(id);
}

void RayRenderer::reregister_surface(std::uint64_t id, int tess_segs) {
    if (!scene_) return;
    auto* surf = scene_->surface_by_id(id);
    if (!surf) return;

    auto& reg = StructureRegistry::instance();
    const std::string old_name = reg.name_for(id);
    if (!old_name.empty()) {
        polyscope::removeSurfaceMesh(old_name, false);
        remove_body_structures(old_name); // the new name may differ, and would orphan these
    }

    std::vector<std::array<double, 3>>       pv;
    std::vector<std::array<std::uint32_t, 3>> pf;
    if (!tessellate_for_polyscope(*surf, tess_segs, pv, pf)) {
        reg.erase(id);
        return;
    }

    const std::string name = reg.intern(id, base_display_name(*surf));
    reg.set_baked(id, surf->transform().matrix());
    auto* mesh = polyscope::registerSurfaceMesh(name, pv, pf);
    mesh->resetTransform();
    apply_default_style(mesh, *surf);
    scene_->mark_acceleration_dirty();
    record_body(id);
    sync_body(id);
}

void RayRenderer::remove_surface_structure(std::uint64_t id) {
    auto& reg = StructureRegistry::instance();
    const std::string nm = reg.name_for(id);
    if (!nm.empty()) {
        polyscope::removeSurfaceMesh(nm, false);
        remove_body_structures(nm);
    }
    reg.erase(id);
    reg.set_body(id, std::nullopt);
}

} // namespace scrt::viz
