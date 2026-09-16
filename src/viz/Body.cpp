#include "scrt/viz/Body.hpp"

#include "scrt/math/Constants.hpp"
#include "scrt/surfaces/Disk.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace scrt::viz {

namespace {

using V = math::vec3;

/// Axis-aligned extent of the surface in its OWN local frame, from its tessellation.
struct LocalBox {
    V lo{std::numeric_limits<double>::max()};
    V hi{std::numeric_limits<double>::lowest()};
    bool valid() const { return lo.x <= hi.x; }
};

LocalBox local_extent(const surfaces::Surface& surf) {
    std::vector<V>             w;
    std::vector<std::uint32_t> i;
    surf.tessellate(24, w, i);
    LocalBox b;
    for (const V& p : w) {
        const V q = surf.transform().point_to_local(p);
        b.lo = glm::min(b.lo, q);
        b.hi = glm::max(b.hi, q);
    }
    return b;
}

/// Closed prism between two rings of equal size: side quads plus a fan cap at each end.
void add_prism(const std::vector<V>& bot, const std::vector<V>& top,
               std::vector<V>& verts, std::vector<std::uint32_t>& idx) {
    const auto n  = static_cast<std::uint32_t>(bot.size());
    const auto b0 = static_cast<std::uint32_t>(verts.size());
    verts.insert(verts.end(), bot.begin(), bot.end());
    verts.insert(verts.end(), top.begin(), top.end());
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t j = (i + 1) % n;
        idx.insert(idx.end(), {b0 + i, b0 + j, b0 + n + j, b0 + i, b0 + n + j, b0 + n + i});
    }
    for (std::uint32_t i = 1; i + 1 < n; ++i) {
        idx.insert(idx.end(), {b0, b0 + i + 1, b0 + i});
        idx.insert(idx.end(), {b0 + n, b0 + n + i, b0 + n + i + 1});
    }
}

/// n points of an ellipse centred at c spanning axes u (radius ru) and v (radius rv).
std::vector<V> ring(V c, V u, V v, double ru, double rv, int n, double phase = 0.0) {
    std::vector<V> r;
    r.reserve(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        const double a = phase + math::TWO_PI * k / n;
        r.push_back(c + ru * std::cos(a) * u + rv * std::sin(a) * v);
    }
    return r;
}

std::vector<V> to_world(const surfaces::Surface& surf, std::vector<V> pts) {
    for (V& p : pts) p = surf.transform().point_to_world(p);
    return pts;
}

/// The substrate slab or the cube, in world space. Built in the surface's local frame first, so
/// it rotates and scales with the part exactly as the surface itself does.
bool mount(const surfaces::Surface& surf, const io::BodyDoc& body,
           std::vector<V>& verts, std::vector<std::uint32_t>& idx) {
    if (!(body.substrate_m > 0.0) && !body.cube) return false;
    const LocalBox b = local_extent(surf);
    if (!b.valid()) return false;
    const V      c  = 0.5 * (b.lo + b.hi);
    const double hx = 0.5 * (b.hi.x - b.lo.x);
    const double hy = 0.5 * (b.hi.y - b.lo.y);
    const V X{1, 0, 0}, Y{0, 1, 0};

    if (body.cube) {
        // The surface is the cube's diagonal: its local x span is the diagonal of the square
        // cross-section in local x-z, and its y span is the height. A 45-degree plate whose
        // height is its width over sqrt(2) gives a true cube; anything else is a square prism.
        if (!(hx > 0.0) || !(hy > 0.0)) return false;
        const std::vector<V> diamond_lo = {c + V{hx, -hy, 0}, c + V{0, -hy, hx},
                                           c + V{-hx, -hy, 0}, c + V{0, -hy, -hx}};
        std::vector<V> diamond_hi = diamond_lo;
        for (V& p : diamond_hi) p.y += 2.0 * hy;
        add_prism(to_world(surf, diamond_lo), to_world(surf, diamond_hi), verts, idx);
        return true;
    }

    // Substrate: behind the surface (local -Z from its lowest point), matching its outline.
    const double z1 = b.lo.z;
    const double z0 = z1 - body.substrate_m;
    if (dynamic_cast<const surfaces::Disk*>(&surf)) {
        const double r = std::max(hx, hy);
        add_prism(to_world(surf, ring(V{c.x, c.y, z0}, X, Y, r, r, 48)),
                  to_world(surf, ring(V{c.x, c.y, z1}, X, Y, r, r, 48)), verts, idx);
    } else {
        // A rectangle as a 4-point ring: phase 45 degrees puts the points on the corners, and the
        // radii are the half-extents times sqrt(2) so those corners land exactly on the edges.
        const double s = std::sqrt(2.0);
        add_prism(to_world(surf, ring(V{c.x, c.y, z0}, X, Y, hx * s, hy * s, 4, math::PI / 4)),
                  to_world(surf, ring(V{c.x, c.y, z1}, X, Y, hx * s, hy * s, 4, math::PI / 4)),
                  verts, idx);
    }
    return true;
}

} // namespace

bool tessellate_body(const surfaces::Surface& surf, const io::BodyDoc& body, BodyPart part,
                     std::vector<V>& verts, std::vector<std::uint32_t>& indices) {
    verts.clear();
    indices.clear();
    if (part == BodyPart::Mount) return mount(surf, body, verts, indices);

    if (!body.post) return false;
    // The post stands under whatever is lowest: the surface, or its substrate/cube if it has one.
    std::vector<V>             pts;
    std::vector<std::uint32_t> ignored;
    surf.tessellate(24, pts, ignored);
    std::vector<V> m;
    if (mount(surf, body, m, ignored)) pts.insert(pts.end(), m.begin(), m.end());
    if (pts.empty()) return false;

    V lo{std::numeric_limits<double>::max()}, hi{std::numeric_limits<double>::lowest()};
    for (const V& p : pts) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
    // Stand the post under the part's centre and end it just inside the part, so it reads as
    // holding it rather than hovering a hair below.
    const double top = lo.z + 0.25 * (hi.z - lo.z);
    if (!(top > 1e-6)) return false;
    const V c{0.5 * (lo.x + hi.x), 0.5 * (lo.y + hi.y), 0.0};
    const V X{1, 0, 0}, Y{0, 1, 0};
    add_prism(ring(c, X, Y, kPostRadius, kPostRadius, 16),
              ring(V{c.x, c.y, top}, X, Y, kPostRadius, kPostRadius, 16), verts, indices);
    return true;
}

} // namespace scrt::viz
