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
    // a = dx²+dy² is the quadratic leading coefficient and carries units of |d|², which
    // is 1/s² under a scale of s. Referencing it to |d|² makes the degeneracy test the
    // scale-free statement "the ray has (almost) no radial component", i.e. it is
    // parallel to the z-axis and the quadratic collapses to a linear equation.
    const double a_ref = glm::dot(lr.direction, lr.direction);
    if (std::abs(a) < 1e-14 * a_ref) {
        // Linear case: ray nearly parallel to z-axis.
        // b mixes a length (o, 4f) with a direction, so its natural magnitude is the sum
        // of its own terms' magnitudes; that is also the cancellation scale of the sum.
        const double b_ref = 2.0 * (std::abs(ox * dx) + std::abs(oy * dy))
                           + std::abs(f4 * dz);
        if (std::abs(b) <= 1e-14 * b_ref)
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
    math::vec3 outward = math::safe_normalize(math::vec3{2.0*p.x, 2.0*p.y, -f4});
    bool front         = (glm::dot(lr.direction, outward) < 0.0);
    math::vec3 n_local = front ? outward : -outward;

    hit.t          = t;
    hit.position   = xform_.point_to_world(p);
    hit.normal     = xform_.normal_to_world(n_local);
    hit.uv         = {p.x, p.y};
    hit.front_face = front;
    hit.surface    = this;
    return true;
}

core::AABB Paraboloid::local_bounds() const {
    double depth = aperture_radius_ * aperture_radius_ / (4.0 * focal_length_);
    return core::AABB{ {-aperture_radius_, -aperture_radius_, 0.0},
                       {aperture_radius_, aperture_radius_, depth} };
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
