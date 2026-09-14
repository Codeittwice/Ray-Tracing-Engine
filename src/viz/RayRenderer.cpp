#include "scrt/viz/RayRenderer.hpp"
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

bool is_transparent_lid_surface(const surfaces::Surface& surf) {
    std::string name = lower_copy(surf.name());
    const auto* material = surf.material();
    if (material)
        name += " " + lower_copy(material->name());
    return name.find("pmma") != std::string::npos
           || name.find("glass") != std::string::npos;
}

/// Base (pre-uniquification) display name for a surface; stable under reordering.
std::string base_display_name(const surfaces::Surface& surf) {
    return surf.name().empty()
               ? "surface_" + std::to_string(surf.id())
               : surf.name();
}

/// Re-applies the surface's default Polyscope appearance after a (re-)registration.
void apply_default_style(polyscope::SurfaceMesh* mesh, const surfaces::Surface& surf) {
    if (is_transparent_lid_surface(surf)) {
        mesh->setSurfaceColor({0.75f, 0.9f, 1.0f});
        mesh->setTransparency(0.2f);
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

// ---- structure naming helpers ------------------------------------------------

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
    }
}

void RayRenderer::register_aperture() {
    const auto& ap = scene_->aperture();
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

    for (const auto& path : result.sampled_paths) {
        if (path.size() < 2) continue;
        std::size_t base = nodes.size();
        for (const auto& p : path)
            nodes.push_back({p.x, p.y, p.z});
        for (std::size_t i = 0; i + 1 < path.size(); ++i)
            edges.push_back({base + i, base + i + 1});
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
}

void RayRenderer::reregister_surface(std::uint64_t id, int tess_segs) {
    if (!scene_) return;
    auto* surf = scene_->surface_by_id(id);
    if (!surf) return;

    auto& reg = StructureRegistry::instance();
    const std::string old_name = reg.name_for(id);
    if (!old_name.empty())
        polyscope::removeSurfaceMesh(old_name, false);

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
}

void RayRenderer::remove_surface_structure(std::uint64_t id) {
    auto& reg = StructureRegistry::instance();
    const std::string nm = reg.name_for(id);
    if (!nm.empty())
        polyscope::removeSurfaceMesh(nm, false);
    reg.erase(id);
}

} // namespace scrt::viz
