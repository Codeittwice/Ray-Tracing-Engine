#include "scrt/surfaces/Paraboloid.hpp"
#include "scrt/math/Constants.hpp"
#include <cmath>

namespace scrt::surfaces {

Paraboloid::Paraboloid(double focal_length, double aperture_radius)
    : focal_length_(focal_length), aperture_radius_(aperture_radius) {}

bool Paraboloid::intersect(const core::Ray& r, double t_min, double t_max,
                           core::Hit& hit) const {
    core::Ray lr = xform_.ray_to_local(r);

    const double ox = lr.origin.x,    oy = lr.origin.y,    oz = lr.origin.z;
    const double dx = lr.direction.x, dy = lr.direction.y, dz = lr.direction.z;
    const double f4 = 4.0 * focal_length_;

    // x²+y²−4fz=0  →  (dx²+dy²)t² + (2ox·dx + 2oy·dy − f4·dz)t + (ox²+oy² − f4·oz) = 0
    double a = dx*dx + dy*dy;
    double b = 2.0*(ox*dx + oy*dy) - f4*dz;
    double c = ox*ox + oy*oy - f4*oz;

    double t = -1.0;
    if (std::abs(a) < 1e-14) {
        // Linear case: ray nearly parallel to z-axis
        if (std::abs(b) < 1e-14)
            return false;
        t = -c / b;
    } else {
        double disc = b*b - 4.0*a*c;
        if (disc < 0.0)
            return false;
        double sq = std::sqrt(disc);
        double t1 = (-b - sq) / (2.0*a);
        double t2 = (-b + sq) / (2.0*a);
        // Choose smallest valid t within aperture
        t = -1.0;
        for (double tc : {t1, t2}) {
            if (tc < t_min || tc > t_max)
                continue;
            math::vec3 p = lr.origin + tc * lr.direction;
            if (p.x*p.x + p.y*p.y <= aperture_radius_*aperture_radius_ && p.z >= 0.0) {
                if (t < 0.0 || tc < t)
                    t = tc;
            }
        }
        if (t < 0.0)
            return false;
    }

    if (t < t_min || t > t_max)
        return false;

    math::vec3 p = lr.origin + t * lr.direction;
    if (p.x*p.x + p.y*p.y > aperture_radius_*aperture_radius_ || p.z < 0.0)
        return false;

    // Gradient of F(x,y,z)=x²+y²−4fz: (2x, 2y, −4f)
    math::vec3 grad{2.0*p.x, 2.0*p.y, -f4};
    math::vec3 n_local = math::safe_normalize(grad);
    if (glm::dot(lr.direction, n_local) > 0.0)
        n_local = -n_local;

    hit.t        = t;
    hit.position = xform_.point_to_world(p);
    hit.normal   = xform_.normal_to_world(n_local);
    hit.uv       = {p.x, p.y};
    hit.surface  = this;
    return true;
}

core::AABB Paraboloid::world_bounds() const {
    double depth = aperture_radius_ * aperture_radius_ / (4.0 * focal_length_);
    core::AABB box;
    const double r = aperture_radius_;
    for (int sx : {-1, 1}) for (int sy : {-1, 1}) {
        box.expand(xform_.point_to_world({sx*r, sy*r, 0.0}));
        box.expand(xform_.point_to_world({sx*r, sy*r, depth}));
    }
    return box;
}

void Paraboloid::tessellate(int nseg, std::vector<math::vec3>& verts,
                            std::vector<std::uint32_t>& indices) const {
    using namespace scrt::math;
    std::uint32_t base = static_cast<std::uint32_t>(verts.size());
    int rings = nseg, segs = nseg * 2;

    for (int i = 0; i <= rings; ++i) {
        double r = aperture_radius_ * static_cast<double>(i) / rings;
        double z = r * r / (4.0 * focal_length_);
        for (int j = 0; j <= segs; ++j) {
            double phi = TWO_PI * j / segs;
            verts.push_back(xform_.point_to_world({r*std::cos(phi), r*std::sin(phi), z}));
        }
    }
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < segs; ++j) {
            std::uint32_t a = base + static_cast<std::uint32_t>(i*(segs+1) + j);
            std::uint32_t b = a + 1;
            std::uint32_t c = base + static_cast<std::uint32_t>((i+1)*(segs+1) + j);
            std::uint32_t d = c + 1;
            indices.insert(indices.end(), {a, c, b, b, c, d});
        }
    }
}

} // namespace scrt::surfaces
