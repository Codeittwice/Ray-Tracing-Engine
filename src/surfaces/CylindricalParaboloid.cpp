#include "scrt/surfaces/CylindricalParaboloid.hpp"
#include "scrt/math/Constants.hpp"
#include <cmath>
#include <cstdint>

namespace scrt::surfaces {

CylindricalParaboloid::CylindricalParaboloid(double focal_length,
                                             double aperture_half_width,
                                             double aperture_half_length)
    : focal_length_(focal_length),
      aperture_half_width_(aperture_half_width),
      aperture_half_length_(aperture_half_length) {}

bool CylindricalParaboloid::intersect(const core::Ray& r, double t_min, double t_max,
                                      core::Hit& hit) const {
    core::Ray lr = xform_.ray_to_local(r);

    const double ox = lr.origin.x,    oz = lr.origin.z;
    const double dx = lr.direction.x, dz = lr.direction.z;
    const double f4 = 4.0 * focal_length_;

    // F(x,y,z) = x² − 4fz = 0.  y is free (extrusion axis).
    // Substituting P(t) = O + tD: dx²·t² + (2·ox·dx − f4·dz)·t + (ox² − f4·oz) = 0
    const double a = dx * dx;
    const double b = 2.0 * ox * dx - f4 * dz;
    const double c = ox * ox - f4 * oz;

    auto in_aperture = [&](const math::vec3& p) {
        return std::abs(p.x) <= aperture_half_width_ &&
               std::abs(p.y) <= aperture_half_length_ &&
               p.z >= 0.0;
    };

    double t_hit = -1.0;

    if (std::abs(a) < 1e-14) {
        // Ray direction has no x-component: linear equation in t.
        if (std::abs(b) < 1e-14)
            return false;
        double t_lin = -c / b;
        if (t_lin >= t_min && t_lin <= t_max) {
            math::vec3 p = lr.origin + t_lin * lr.direction;
            if (in_aperture(p))
                t_hit = t_lin;
        }
    } else {
        double disc = b * b - 4.0 * a * c;
        if (disc < 0.0)
            return false;
        double sq = std::sqrt(disc);
        double t1 = (-b - sq) / (2.0 * a);
        double t2 = (-b + sq) / (2.0 * a);
        for (double tc : {t1, t2}) {
            if (tc < t_min || tc > t_max)
                continue;
            math::vec3 p = lr.origin + tc * lr.direction;
            if (!in_aperture(p))
                continue;
            if (t_hit < 0.0 || tc < t_hit)
                t_hit = tc;
        }
    }

    if (t_hit < 0.0)
        return false;

    math::vec3 p = lr.origin + t_hit * lr.direction;

    // ∇F = (2x, 0, −4f)
    math::vec3 outward = math::safe_normalize(math::vec3{2.0 * p.x, 0.0, -f4});
    bool front         = (glm::dot(lr.direction, outward) < 0.0);
    math::vec3 n_local = front ? outward : -outward;

    hit.t          = t_hit;
    hit.position   = xform_.point_to_world(p);
    hit.normal     = xform_.normal_to_world(n_local);
    hit.uv         = {p.x, p.y};
    hit.front_face = front;
    hit.surface    = this;
    return true;
}

core::AABB CylindricalParaboloid::world_bounds() const {
    const double hw    = aperture_half_width_;
    const double hl    = aperture_half_length_;
    const double depth = hw * hw / (4.0 * focal_length_);
    core::AABB box;
    for (int sx : {-1, 1})
        for (int sy : {-1, 1}) {
            box.expand(xform_.point_to_world({sx * hw, sy * hl, 0.0}));
            box.expand(xform_.point_to_world({sx * hw, sy * hl, depth}));
        }
    return box;
}

void CylindricalParaboloid::tessellate(int nseg, std::vector<math::vec3>& verts,
                                       std::vector<std::uint32_t>& indices) const {
    const int N = std::max(nseg, 2);
    std::uint32_t base = static_cast<std::uint32_t>(verts.size());

    // Rectangular grid: x in [-hw, hw], y in [-hl, hl]; z = x²/(4f)
    for (int iy = 0; iy <= N; ++iy) {
        double y = aperture_half_length_ * (2.0 * iy / N - 1.0);
        for (int ix = 0; ix <= N; ++ix) {
            double x = aperture_half_width_ * (2.0 * ix / N - 1.0);
            double z = x * x / (4.0 * focal_length_);
            verts.push_back(xform_.point_to_world({x, y, z}));
        }
    }

    const std::uint32_t stride = static_cast<std::uint32_t>(N + 1);
    for (int iy = 0; iy < N; ++iy) {
        for (int ix = 0; ix < N; ++ix) {
            std::uint32_t v00 = base + static_cast<std::uint32_t>(iy)     * stride + static_cast<std::uint32_t>(ix);
            std::uint32_t v10 = v00 + 1;
            std::uint32_t v01 = v00 + stride;
            std::uint32_t v11 = v01 + 1;
            indices.insert(indices.end(), {v00, v10, v01, v10, v11, v01});
        }
    }
}

} // namespace scrt::surfaces
