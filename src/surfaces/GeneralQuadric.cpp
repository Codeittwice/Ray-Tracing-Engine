#include "scrt/surfaces/GeneralQuadric.hpp"
#include "scrt/math/Vec.hpp"
#include <cmath>

namespace scrt::surfaces {

GeneralQuadric::GeneralQuadric(QuadricCoeffs c, core::AABB clip)
    : coeffs_(c), clip_(clip) {}

// F(p) = Ax²+By²+Cz²+Dxy+Exz+Fyz+Gx+Hy+Iz+J
double GeneralQuadric::eval(math::vec3 p) const noexcept {
    const auto& c = coeffs_;
    return c.A*p.x*p.x + c.B*p.y*p.y + c.C*p.z*p.z
         + c.D*p.x*p.y + c.E*p.x*p.z + c.F*p.y*p.z
         + c.G*p.x     + c.H*p.y     + c.I*p.z + c.J;
}

// ∇F = (2Ax+Dy+Ez+G,  2By+Dx+Fz+H,  2Cz+Ex+Fy+I)
math::vec3 GeneralQuadric::grad(math::vec3 p) const noexcept {
    const auto& c = coeffs_;
    return {2.0*c.A*p.x + c.D*p.y + c.E*p.z + c.G,
            2.0*c.B*p.y + c.D*p.x + c.F*p.z + c.H,
            2.0*c.C*p.z + c.E*p.x + c.F*p.y + c.I};
}

bool GeneralQuadric::intersect(const core::Ray& r, double t_min, double t_max,
                                core::Hit& hit) const {
    core::Ray lr = xform_.ray_to_local(r);

    const auto& c  = coeffs_;
    const double ox = lr.origin.x,    oy = lr.origin.y,    oz = lr.origin.z;
    const double dx = lr.direction.x, dy = lr.direction.y, dz = lr.direction.z;

    // Substitute P(t) = O + t*D into F, collect powers of t.
    double qa = c.A*dx*dx + c.B*dy*dy + c.C*dz*dz
              + c.D*dx*dy + c.E*dx*dz + c.F*dy*dz;

    double qb = 2.0*c.A*ox*dx + 2.0*c.B*oy*dy + 2.0*c.C*oz*dz
              + c.D*(ox*dy + oy*dx) + c.E*(ox*dz + oz*dx) + c.F*(oy*dz + oz*dy)
              + c.G*dx + c.H*dy + c.I*dz;

    double qc = eval(lr.origin);

    // Solve qa*t² + qb*t + qc = 0
    double t_hit = -1.0;
    if (std::abs(qa) < 1e-14) {
        if (std::abs(qb) < 1e-14)
            return false;
        double t_lin = -qc / qb;
        if (t_lin >= t_min && t_lin <= t_max) {
            math::vec3 p = lr.origin + t_lin * lr.direction;
            if (p.x >= clip_.min().x && p.x <= clip_.max().x &&
                p.y >= clip_.min().y && p.y <= clip_.max().y &&
                p.z >= clip_.min().z && p.z <= clip_.max().z)
                t_hit = t_lin;
        }
    } else {
        double disc = qb*qb - 4.0*qa*qc;
        if (disc < 0.0)
            return false;
        double sq = std::sqrt(disc);
        double t1 = (-qb - sq) / (2.0*qa);
        double t2 = (-qb + sq) / (2.0*qa);
        for (double tc : {t1, t2}) {
            if (tc < t_min || tc > t_max)
                continue;
            math::vec3 p = lr.origin + tc * lr.direction;
            if (p.x < clip_.min().x || p.x > clip_.max().x ||
                p.y < clip_.min().y || p.y > clip_.max().y ||
                p.z < clip_.min().z || p.z > clip_.max().z)
                continue;
            if (t_hit < 0.0 || tc < t_hit)
                t_hit = tc;
        }
    }

    if (t_hit < 0.0)
        return false;

    math::vec3 p       = lr.origin + t_hit * lr.direction;
    math::vec3 g       = grad(p);
    double     g_len   = glm::length(g);
    if (g_len < 1e-14)
        return false;
    math::vec3 outward = g / g_len;
    bool       front   = (glm::dot(lr.direction, outward) < 0.0);
    math::vec3 n_local = front ? outward : -outward;

    hit.t          = t_hit;
    hit.position   = xform_.point_to_world(p);
    hit.normal     = xform_.normal_to_world(n_local);
    hit.uv         = {p.x, p.y};
    hit.front_face = front;
    hit.surface    = this;
    return true;
}

core::AABB GeneralQuadric::world_bounds() const {
    // Conservative: transform all 8 corners of the local clip box.
    core::AABB box;
    for (int sx : {0, 1})
        for (int sy : {0, 1})
            for (int sz : {0, 1}) {
                math::vec3 corner{sx ? clip_.max().x : clip_.min().x,
                                  sy ? clip_.max().y : clip_.min().y,
                                  sz ? clip_.max().z : clip_.min().z};
                box.expand(xform_.point_to_world(corner));
            }
    return box;
}

void GeneralQuadric::tessellate(int /*nseg*/, std::vector<math::vec3>& /*verts*/,
                                std::vector<std::uint32_t>& /*indices*/) const {
    // Phase 6: implement marching-cubes tessellation.
}

} // namespace scrt::surfaces
