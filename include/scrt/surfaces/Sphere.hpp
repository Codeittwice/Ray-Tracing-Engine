#pragma once
#include "scrt/surfaces/Surface.hpp"

namespace scrt::surfaces {

/// Sphere of given radius centred at local origin.
class Sphere final : public Surface {
public:
    explicit Sphere(double radius);

    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const override;
    core::AABB world_bounds() const override;
    void tessellate(int nseg, std::vector<math::vec3>& verts,
                    std::vector<std::uint32_t>& indices) const override;

    double radius() const { return radius_; }

private:
    double radius_;
};

} // namespace scrt::surfaces
