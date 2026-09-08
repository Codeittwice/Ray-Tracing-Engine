#pragma once
#include "scrt/surfaces/Surface.hpp"

namespace scrt::surfaces {

/// Sphere of given radius centred at local origin.
class Sphere final : public Surface {
public:
    explicit Sphere(double radius);

    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const override;
    core::AABB local_bounds() const override;
    /// UniformOnly: non-uniform scale makes an ellipsoid, so radius() no longer describes the geometry.
    ScaleSupport scale_support() const override { return ScaleSupport::UniformOnly; }
    void tessellate(int nseg, std::vector<math::vec3>& verts,
                    std::vector<std::uint32_t>& indices) const override;

    double radius() const { return radius_; }
    /// Sets the sphere radius.
    void set_radius(double radius) { radius_ = radius; }

private:
    double radius_;
};

} // namespace scrt::surfaces
