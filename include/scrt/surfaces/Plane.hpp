#pragma once
#include "scrt/surfaces/Surface.hpp"

namespace scrt::surfaces {

/// Bounded planar rectangle. In local space: z=0 plane, extents [-hw,hw] x [-hh,hh].
class Plane final : public Surface {
public:
    /// Canonical local frame: normal = (0,0,1), u-axis = (1,0,0), v-axis = (0,1,0).
    Plane(double half_width, double half_height);

    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const override;
    core::AABB world_bounds() const override;
    void tessellate(int nseg, std::vector<math::vec3>& verts,
                    std::vector<std::uint32_t>& indices) const override;

    double half_width()  const { return hw_; }
    double half_height() const { return hh_; }

private:
    double hw_;
    double hh_;
};

} // namespace scrt::surfaces
