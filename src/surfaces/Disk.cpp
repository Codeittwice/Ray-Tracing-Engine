#include "scrt/surfaces/Disk.hpp"
#include "scrt/math/Constants.hpp"
#include <cmath>
#include <stdexcept>
#include <string>

namespace scrt::surfaces {

Disk::Disk(double radius, double hole_radius) : radius_(radius), hole_(hole_radius) {
    if (!(radius > 0.0) || !std::isfinite(radius))
        throw std::invalid_argument("Disk: radius must be finite and > 0 (got " +
                                    std::to_string(radius) + ")");
    if (!(hole_radius >= 0.0) || !std::isfinite(hole_radius) || hole_radius >= radius)
        throw std::invalid_argument("Disk: hole_radius must satisfy 0 <= hole < radius (got " +
                                    std::to_string(hole_radius) + ")");
}

bool Disk::intersect(const core::Ray& r, double t_min, double t_max, core::Hit& hit) const {
    // Same plane test as Plane::intersect, then a radial clip instead of a rectangular one.
    core::Ray lr = xform_.ray_to_local(r);
    if (std::abs(lr.direction.z) < 1e-14 * glm::length(lr.direction))
        return false;

    const double t = -lr.origin.z / lr.direction.z;
    if (t < t_min || t > t_max)
        return false;

    const double x  = lr.origin.x + t * lr.direction.x;
    const double y  = lr.origin.y + t * lr.direction.y;
    const double r2 = x * x + y * y;
    // Outside the rim, or inside the hole: a miss. The hole is open, so a ray through it
    // continues to whatever lies behind, which is the whole point of an aperture stop.
    if (r2 > radius_ * radius_ || r2 < hole_ * hole_)
        return false;

    const bool front = (lr.direction.z < 0.0);
    const math::vec3 n_local{0.0, 0.0, front ? 1.0 : -1.0};

    hit.t          = t;
    hit.position   = xform_.point_to_world(math::vec3{x, y, 0.0});
    hit.normal     = xform_.normal_to_world(n_local);
    hit.uv         = {x, y};   // local metres, as Plane reports, so a flux grid could bin it
    hit.front_face = front;
    hit.surface    = this;
    return true;
}

core::AABB Disk::local_bounds() const {
    return core::AABB{{-radius_, -radius_, -1e-4}, {radius_, radius_, 1e-4}};
}

void Disk::tessellate(int nseg, std::vector<math::vec3>& verts,
                      std::vector<std::uint32_t>& indices) const {
    const int n = nseg < 8 ? 8 : nseg * 2;
    const auto base = static_cast<std::uint32_t>(verts.size());

    if (hole_ <= 0.0) {
        // Fan about the centre.
        verts.push_back(xform_.point_to_world({0.0, 0.0, 0.0}));
        for (int i = 0; i < n; ++i) {
            const double a = math::TWO_PI * i / n;
            verts.push_back(xform_.point_to_world({radius_ * std::cos(a), radius_ * std::sin(a), 0.0}));
        }
        for (int i = 0; i < n; ++i) {
            const auto a = base + 1 + static_cast<std::uint32_t>(i);
            const auto b = base + 1 + static_cast<std::uint32_t>((i + 1) % n);
            indices.insert(indices.end(), {base, a, b});
        }
        return;
    }

    // Ring: inner and outer loops, two triangles per segment.
    for (int i = 0; i < n; ++i) {
        const double a = math::TWO_PI * i / n;
        verts.push_back(xform_.point_to_world({hole_ * std::cos(a), hole_ * std::sin(a), 0.0}));
        verts.push_back(xform_.point_to_world({radius_ * std::cos(a), radius_ * std::sin(a), 0.0}));
    }
    for (int i = 0; i < n; ++i) {
        const auto i0 = base + 2 * static_cast<std::uint32_t>(i);
        const auto o0 = i0 + 1;
        const auto i1 = base + 2 * static_cast<std::uint32_t>((i + 1) % n);
        const auto o1 = i1 + 1;
        indices.insert(indices.end(), {i0, o0, o1, i0, o1, i1});
    }
}

} // namespace scrt::surfaces
