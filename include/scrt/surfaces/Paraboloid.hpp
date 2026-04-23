#pragma once
#include "scrt/surfaces/Surface.hpp"

namespace scrt::surfaces {

/// Paraboloid x²+y²=4fz in local space; vertex at origin, focus at (0,0,f).
class Paraboloid final : public Surface {
public:
    /// focal_length: f in the equation; aperture_radius: maximum radial extent.
    Paraboloid(double focal_length, double aperture_radius);

    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const override;
    core::AABB world_bounds() const override;
    void tessellate(int nseg, std::vector<math::vec3>& verts,
                    std::vector<std::uint32_t>& indices) const override;

    double focal_length()    const { return focal_length_; }
    double aperture_radius() const { return aperture_radius_; }

private:
    double focal_length_;
    double aperture_radius_;
};

} // namespace scrt::surfaces
