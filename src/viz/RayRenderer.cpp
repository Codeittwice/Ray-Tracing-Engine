#include "scrt/viz/RayRenderer.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/scene/Aperture.hpp"
#include <cmath>
#include <string>
#include <vector>

#include "polyscope/curve_network.h"
#include "polyscope/polyscope.h"
#include "polyscope/surface_mesh.h"

namespace scrt::viz {

static void to_poly(const std::vector<math::vec3>& in,
                    std::vector<std::array<double, 3>>& out) {
    out.clear();
    out.reserve(in.size());
    for (const auto& v : in)
        out.push_back({v.x, v.y, v.z});
}

static void to_faces(const std::vector<std::uint32_t>& idx,
                     std::vector<std::array<std::uint32_t, 3>>& out) {
    out.clear();
    out.reserve(idx.size() / 3);
    for (std::size_t k = 0; k + 2 < idx.size(); k += 3)
        out.push_back({idx[k], idx[k + 1], idx[k + 2]});
}

void RayRenderer::register_surfaces(int tess_segs) {
    int id = 0;
    for (const auto& surf : scene_->surfaces()) {
        std::vector<math::vec3>    verts;
        std::vector<std::uint32_t> indices;
        surf->tessellate(tess_segs, verts, indices);

        if (verts.empty() || indices.size() < 3) { ++id; continue; }

        std::vector<std::array<double, 3>>      pv;
        std::vector<std::array<std::uint32_t, 3>> pf;
        to_poly(verts, pv);
        to_faces(indices, pf);

        std::string name = surf->name().empty()
                               ? "surface_" + std::to_string(id)
                               : surf->name();
        polyscope::registerSurfaceMesh(name, pv, pf);
        ++id;
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

    auto* mesh = polyscope::registerSurfaceMesh("aperture", pv, pf);
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
    net->setRadius(0.001f);
}

void RayRenderer::clear() {
    polyscope::removeAllStructures();
}

} // namespace scrt::viz
