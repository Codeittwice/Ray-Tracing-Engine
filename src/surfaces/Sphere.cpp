#include "scrt/surfaces/Sphere.hpp"
#include "scrt/math/Constants.hpp"
#include <cmath>
#include <limits>

namespace scrt::surfaces {

Sphere::Sphere(double radius) : radius_(radius) {}

bool Sphere::intersect(const core::Ray& r, double t_min, double t_max, core::Hit& hit) const {
    core::Ray lr = xform_.ray_to_local(r);

    // Solve |O + t*D|^2 = R^2 using the half-b form for numerical stability
    double a      = glm::dot(lr.direction, lr.direction);
    double b_half = glm::dot(lr.origin, lr.direction);
    double c      = glm::dot(lr.origin, lr.origin) - radius_ * radius_;
    double disc   = b_half * b_half - a * c;

    if (disc < 0.0)
        return false;

    double sq = std::sqrt(disc);
    double t  = (-b_half - sq) / a;
    if (t < t_min || t > t_max) {
        t = (-b_half + sq) / a;
        if (t < t_min || t > t_max)
            return false;
    }

    math::vec3 p_local = lr.origin + t * lr.direction;
    math::vec3 n_local = p_local / radius_;

    if (glm::dot(lr.direction, n_local) > 0.0)
        n_local = -n_local;

    hit.t        = t;
    hit.position = xform_.point_to_world(p_local);
    hit.normal   = xform_.normal_to_world(n_local);
    hit.uv       = {std::atan2(p_local.y, p_local.x), std::acos(p_local.z / radius_)};
    hit.surface  = this;
    return true;
}

core::AABB Sphere::world_bounds() const {
    core::AABB box;
    math::vec3 r3{radius_, radius_, radius_};
    box.expand(xform_.point_to_world(-r3));
    box.expand(xform_.point_to_world( r3));
    // Expand 8 corners to handle rotated transforms
    for (int sx : {-1, 1}) {
        for (int sy : {-1, 1}) {
            for (int sz : {-1, 1}) {
                box.expand(xform_.point_to_world(
                    {sx * radius_, sy * radius_, sz * radius_}));
            }
        }
    }
    return box;
}

void Sphere::tessellate(int nseg, std::vector<math::vec3>& verts,
                        std::vector<std::uint32_t>& indices) const {
    using namespace scrt::math;
    int rings = nseg, segs = nseg * 2;
    std::uint32_t base = static_cast<std::uint32_t>(verts.size());

    for (int i = 0; i <= rings; ++i) {
        double phi = PI * i / rings;
        for (int j = 0; j <= segs; ++j) {
            double theta = TWO_PI * j / segs;
            math::vec3 p{std::sin(phi) * std::cos(theta),
                         std::sin(phi) * std::sin(theta),
                         std::cos(phi)};
            verts.push_back(xform_.point_to_world(radius_ * p));
        }
    }
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < segs; ++j) {
            std::uint32_t a = base + static_cast<std::uint32_t>(i * (segs+1) + j);
            std::uint32_t b = a + 1;
            std::uint32_t c = base + static_cast<std::uint32_t>((i+1) * (segs+1) + j);
            std::uint32_t d = c + 1;
            indices.insert(indices.end(), {a, c, b, b, c, d});
        }
    }
}

} // namespace scrt::surfaces
