#include "scrt/surfaces/Plane.hpp"
#include <cmath>
#include <limits>

namespace scrt::surfaces {

Plane::Plane(double half_width, double half_height) : hw_(half_width), hh_(half_height) {}

bool Plane::intersect(const core::Ray& r, double t_min, double t_max, core::Hit& hit) const {
    core::Ray lr = xform_.ray_to_local(r);

    // Local plane is z=0; skip if ray is parallel
    if (std::abs(lr.direction.z) < 1e-14)
        return false;

    double t = -lr.origin.z / lr.direction.z;
    if (t < t_min || t > t_max)
        return false;

    double x = lr.origin.x + t * lr.direction.x;
    double y = lr.origin.y + t * lr.direction.y;
    if (std::abs(x) > hw_ || std::abs(y) > hh_)
        return false;

    bool front = (lr.direction.z < 0.0);
    math::vec3 n_local{0.0, 0.0, front ? 1.0 : -1.0};

    hit.t          = t;
    hit.position   = xform_.point_to_world(math::vec3{x, y, 0.0});
    hit.normal     = xform_.normal_to_world(n_local);
    hit.uv         = {x, y};
    hit.front_face = front;
    hit.surface    = this;
    return true;
}

core::AABB Plane::world_bounds() const {
    core::AABB box;
    // Transform all 4 corners of the local rectangle
    const double zeps = 1e-4;
    for (double sx : {-hw_, hw_}) {
        for (double sy : {-hh_, hh_}) {
            for (double sz : {-zeps, zeps}) {
                box.expand(xform_.point_to_world({sx, sy, sz}));
            }
        }
    }
    return box;
}

void Plane::tessellate(int /*nseg*/, std::vector<math::vec3>& verts,
                       std::vector<std::uint32_t>& indices) const {
    std::uint32_t base = static_cast<std::uint32_t>(verts.size());
    verts.push_back(xform_.point_to_world({-hw_, -hh_, 0.0}));
    verts.push_back(xform_.point_to_world({ hw_, -hh_, 0.0}));
    verts.push_back(xform_.point_to_world({ hw_,  hh_, 0.0}));
    verts.push_back(xform_.point_to_world({-hw_,  hh_, 0.0}));
    indices.insert(indices.end(), {base, base+1, base+2, base, base+2, base+3});
}

} // namespace scrt::surfaces
