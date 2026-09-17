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
#include <map>
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

// ---- light sources ------------------------------------------------------------

namespace {

/// Approximate sRGB of a visible wavelength (380-780 nm); a neutral grey outside that band.
/// The usual piecewise ramp with intensity rolled off at both ends of the visible range.
glm::vec3 wavelength_rgb(double nm) {
    double r = 0.0, g = 0.0, b = 0.0;
    if (nm >= 380 && nm < 440)      { r = -(nm - 440) / 60; b = 1; }
    else if (nm >= 440 && nm < 490) { g = (nm - 440) / 50; b = 1; }
    else if (nm >= 490 && nm < 510) { g = 1; b = -(nm - 510) / 20; }
    else if (nm >= 510 && nm < 580) { r = (nm - 510) / 70; g = 1; }
    else if (nm >= 580 && nm < 645) { r = 1; g = -(nm - 645) / 65; }
    else if (nm >= 645 && nm <= 780){ r = 1; }
    else return {0.55f, 0.55f, 0.58f};   // infrared or ultraviolet: no honest colour
    double k = 1.0;
    if (nm < 420) k = 0.3 + 0.7 * (nm - 380) / 40;
    else if (nm > 700) k = 0.3 + 0.7 * (780 - nm) / 80;
    return {static_cast<float>(r * k), static_cast<float>(g * k), static_cast<float>(b * k)};
}

/// Closed prism between two equal rings (side quads and fan caps), appended to pv/pf.
void add_ring_prism(const std::vector<math::vec3>& bot, const std::vector<math::vec3>& top,
                    std::vector<std::array<double, 3>>&        pv,
                    std::vector<std::array<std::uint32_t, 3>>& pf) {
    const auto n  = static_cast<std::uint32_t>(bot.size());
    const auto b0 = static_cast<std::uint32_t>(pv.size());
    for (const auto& p : bot) pv.push_back({p.x, p.y, p.z});
    for (const auto& p : top) pv.push_back({p.x, p.y, p.z});
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t j = (i + 1) % n;
        pf.push_back({b0 + i, b0 + j, b0 + n + j});
        pf.push_back({b0 + i, b0 + n + j, b0 + n + i});
    }
    for (std::uint32_t i = 1; i + 1 < n; ++i) {
        pf.push_back({b0, b0 + i + 1, b0 + i});
        pf.push_back({b0 + n, b0 + n + i, b0 + n + i + 1});
    }
}

std::vector<math::vec3> circle(math::vec3 c, math::vec3 u, math::vec3 v, double r, int n) {
    std::vector<math::vec3> out;
    for (int k = 0; k < n; ++k) {
        const double a = math::TWO_PI * k / n;
        out.push_back(c + r * (std::cos(a) * u + std::sin(a) * v));
    }
    return out;
}

const std::string kLaserPrefix = "laser_";

} // namespace

void RayRenderer::register_sources() {
    // Remove what a previous call drew: this runs on load and again when Show posts changes.
    for (int i = 1; i <= 64; ++i) {
        const std::string base = kLaserPrefix + std::to_string(i);
        for (const std::string& s : {base + " [housing]", base + " [window]", base + " [post]"})
            if (polyscope::hasSurfaceMesh(s)) polyscope::removeSurfaceMesh(s, false);
    }
    if (!scene_) return;

    int index = 0;
    for (const auto& src : scene_->sources()) {
        const auto* laser = dynamic_cast<const sources::Laser*>(src.get());
        if (!laser) continue;
        if (++index > 64) break;
        const std::string base = kLaserPrefix + std::to_string(index);

        // The user asked for a laser to be an object rather than light appearing from nowhere.
        // Presentation only: nothing traces the housing, and it ends exactly at the source origin
        // so the beam visibly leaves its front face.
        const math::vec3 d = glm::normalize(laser->direction());
        math::vec3       u, v;
        scene::orthonormal_frame(d, u, v);
        const math::vec3 front = laser->origin();
        const double     len   = 0.12;
        const double     rad   = 0.0125;
        const math::vec3 back  = front - len * d;

        std::vector<std::array<double, 3>>        pv;
        std::vector<std::array<std::uint32_t, 3>> pf;
        add_ring_prism(circle(back, u, v, rad, 32), circle(front, u, v, rad, 32), pv, pf);
        auto* housing = polyscope::registerSurfaceMesh(base + " [housing]", pv, pf);
        housing->setMaterial("clay");
        housing->setSurfaceColor({0.13f, 0.13f, 0.15f});
        housing->setSmoothShade(true);
        housing->setBackFacePolicy(polyscope::BackFacePolicy::Identical);

        // The exit window: the beam's own diameter (at least 2 mm so it is visible), just proud of
        // the front face, in the colour of the wavelength.
        pv.clear();
        pf.clear();
        const double wr = std::clamp(0.5 * laser->beam_diameter_m(), 0.001, rad * 0.9);
        add_ring_prism(circle(front, u, v, wr, 24), circle(front + 0.0008 * d, u, v, wr, 24), pv, pf);
        auto* window = polyscope::registerSurfaceMesh(base + " [window]", pv, pf);
        window->setMaterial("flat");
        window->setSurfaceColor(wavelength_rgb(laser->wavelength_nm()));

        // A post under the housing's middle, like the bench parts, when it is lifted off the floor.
        const math::vec3 mid = front - 0.5 * len * d;
        const double     top = mid.z - rad * 0.5;
        if (show_posts() && top > 1e-6) {
            pv.clear();
            pf.clear();
            const math::vec3 X{1, 0, 0}, Y{0, 1, 0};
            add_ring_prism(circle({mid.x, mid.y, 0.0}, X, Y, kPostRadius, 16),
                           circle({mid.x, mid.y, top}, X, Y, kPostRadius, 16), pv, pf);
            auto* post = polyscope::registerSurfaceMesh(base + " [post]", pv, pf);
            post->setMaterial("clay");
            post->setSurfaceColor({0.045f, 0.045f, 0.05f});
            post->setBackFacePolicy(polyscope::BackFacePolicy::Identical);
        }
    }
}

namespace {

/// Every recorded edge, in the order register_paths laid them into the "ray_paths" network, so a pick
/// index maps straight back to what the light was doing there. GUI thread only.
std::vector<tracer::RayEdgeInfo> g_ray_edges;
/// Node index -> first edge touching it, so a click that lands on a node still names a ray.
std::vector<long>                g_node_first_edge;
long                             g_picked_edge = -1;

const char* const kRayPolarisationName = "ray_polarisation";

// Defined with the grid, further down this file (same unnamed namespace).
polyscope::CurveNetwork* register_decoration_lines(const std::string& name,
                                                   const std::vector<glm::vec3>& nodes,
                                                   const std::vector<std::array<std::size_t, 2>>& edges);

} // namespace

glm::vec3 light_colour(double wavelength_nm) { return wavelength_rgb(wavelength_nm); }

const tracer::RayEdgeInfo* ray_edge(long index) {
    return (index >= 0 && static_cast<std::size_t>(index) < g_ray_edges.size())
               ? &g_ray_edges[static_cast<std::size_t>(index)]
               : nullptr;
}

long ray_edge_for_pick(std::size_t local_index) {
    // Polyscope numbers a curve network's pickable elements nodes first, then edges.
    const std::size_t n_nodes = g_node_first_edge.size();
    if (local_index < n_nodes) return g_node_first_edge[local_index];
    const std::size_t e = local_index - n_nodes;
    return e < g_ray_edges.size() ? static_cast<long>(e) : -1;
}

void set_picked_ray_edge(long index) { g_picked_edge = index; }
long picked_ray_edge() { return ray_edge(g_picked_edge) ? g_picked_edge : -1; }

void RayRenderer::register_paths(const tracer::TraceResult& result) {
    g_ray_edges.clear();
    g_node_first_edge.clear();
    g_picked_edge = -1;
    if (polyscope::hasCurveNetwork(kRayPolarisationName))
        polyscope::removeCurveNetwork(kRayPolarisationName, false);
    if (result.sampled_paths.empty()) return;

    std::vector<std::array<double, 3>>      nodes;
    std::vector<std::array<std::size_t, 2>> edges;
    std::vector<glm::vec3>                  colours;

    // Polarisation ticks: the axes of each polarised edge's polarisation ellipse, drawn across the
    // ray at its midpoint. Linear light is one line, circular an equal cross, elliptical a long and a
    // short line. Unpolarised light gets none - it has no axis to draw.
    //
    // ONE mark per beam segment, not one per ray. Looked at on QA 16: a 3 mm beam of ~200 rays drew 200
    // marks 3.6 mm long at the same place, all buried inside the solid tube the rays make, and nothing
    // could be seen. Edges are grouped by where their midpoint lies (5 mm cells) and which way they run;
    // each group draws the first member's state at the group's mean midpoint, long enough to reach
    // clear of the bundle.
    std::vector<glm::vec3>                  tick_nodes;
    std::vector<std::array<std::size_t, 2>> tick_edges;
    const double tick_min_half = std::max(0.0015, 6.0 * static_cast<double>(ray_radius_m()));
    struct TickGroup {
        math::vec3          major{0.0}, minor{0.0};
        math::vec3          sum{0.0};
        std::vector<math::vec3> mids;
    };
    std::map<std::array<long long, 6>, TickGroup> tick_groups;

    // A path is a tree now, so its edges are given rather than implied by adjacency. The old
    // "join consecutive points" loop drew a split as a line that ran out along the reflected
    // branch and then jumped back to the split point - a picture of something that never
    // happened, and about to become the common case once beam splitters exist.
    for (const auto& path : result.sampled_paths) {
        if (path.edges.empty()) continue;
        const std::size_t base = nodes.size();
        for (const auto& pt : path.nodes) {
            nodes.push_back({pt.x, pt.y, pt.z});
            g_node_first_edge.push_back(-1);
        }
        for (std::size_t k = 0; k < path.edges.size(); ++k) {
            const auto& e = path.edges[k];
            const long  flat = static_cast<long>(edges.size());
            edges.push_back({base + e[0], base + e[1]});
            for (std::size_t endpoint : {base + e[0], base + e[1]})
                if (g_node_first_edge[endpoint] < 0) g_node_first_edge[endpoint] = flat;

            tracer::RayEdgeInfo info;
            if (k < path.edge_info.size()) info = path.edge_info[k];
            else if (k < path.edge_power_w.size()) info.power_w = path.edge_power_w[k];
            g_ray_edges.push_back(info);

            // Laser light in its wavelength's colour; sunlight (broadband) in the old ray yellow.
            colours.push_back(path.monochromatic ? wavelength_rgb(info.wavelength_nm)
                                                 : glm::vec3{1.0f, 0.85f, 0.2f});

            if (info.polarised) {
                const math::vec3 a = path.nodes[e[0]], b = path.nodes[e[1]];
                const math::vec3 mid = 0.5 * (a + b);
                const math::vec3 s   = info.s_axis;
                const math::vec3 p   = glm::cross(info.direction, s);
                // Field as a complex world vector, then the ellipse axes: rotate the phase by
                // theta0 = -arg(F.F)/2 so the real part is the major axis and the imaginary the minor.
                const std::complex<double> fx = info.Es * s.x + info.Ep * p.x;
                const std::complex<double> fy = info.Es * s.y + info.Ep * p.y;
                const std::complex<double> fz = info.Es * s.z + info.Ep * p.z;
                const std::complex<double> ff = fx * fx + fy * fy + fz * fz;
                const std::complex<double> rot = std::exp(std::complex<double>(0.0, -0.5 * std::arg(ff)));
                const math::vec3 major{(rot * fx).real(), (rot * fy).real(), (rot * fz).real()};
                const math::vec3 minor{(rot * fx).imag(), (rot * fy).imag(), (rot * fz).imag()};
                const math::vec3 dir = glm::normalize(b - a);
                const std::array<long long, 6> key{
                    std::llround(mid.x / 0.005), std::llround(mid.y / 0.005), std::llround(mid.z / 0.005),
                    std::llround(dir.x * 20.0), std::llround(dir.y * 20.0), std::llround(dir.z * 20.0)};
                auto& g = tick_groups[key];
                if (g.mids.empty()) {
                    g.major = major;
                    g.minor = minor;
                }
                g.sum += mid;
                g.mids.push_back(mid);
            }
        }
    }

    for (const auto& [key, g] : tick_groups) {
        const math::vec3 centre = g.sum / static_cast<double>(g.mids.size());
        double spread = 0.0;
        for (const auto& m : g.mids) spread = std::max(spread, glm::length(m - centre));
        const double half = std::max(tick_min_half, 1.6 * spread + tick_min_half * 0.5);
        for (const math::vec3& axis : {g.major, g.minor}) {
            if (glm::length(axis) < 0.05) continue;   // a pure linear state has no minor axis
            // Normalised: the Jones vector is unit power, so a circular state's axes are 1/sqrt2 long
            // and would draw a cross smaller than a linear line; equal arms read as "circular".
            const math::vec3 u = glm::normalize(axis) * half * glm::length(axis) / std::max(glm::length(g.major), 1e-12);
            tick_edges.push_back({tick_nodes.size(), tick_nodes.size() + 1});
            tick_nodes.emplace_back(glm::vec3(centre - u));
            tick_nodes.emplace_back(glm::vec3(centre + u));
        }
    }

    if (nodes.empty()) return;

    auto* net = polyscope::registerCurveNetwork("ray_paths", nodes, edges);
    net->setColor({1.0f, 0.85f, 0.2f});
    auto* q = net->addEdgeColorQuantity("light", colours);
    q->setEnabled(true);
    apply_ray_appearance();

    if (!tick_nodes.empty()) {
        auto* ticks = register_decoration_lines(kRayPolarisationName, tick_nodes, tick_edges);
        if (ticks) {
            ticks->setColor({0.95f, 0.95f, 1.0f});
            ticks->setRadius(std::max(0.00015f, 0.6f * ray_radius_m()), false);
        }
    }
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
