#pragma once
#include "scrt/surfaces/Surface.hpp"

namespace scrt::surfaces {

/// Cylindrical paraboloid x²=4fz in local space; focal line at (0,y,f) for all y.
class CylindricalParaboloid final : public Surface {
public:
    /// focal_length f; aperture_half_width: max |x|; aperture_half_length: max |y|.
    CylindricalParaboloid(double focal_length,
                          double aperture_half_width,
                          double aperture_half_length);

    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const override;
    core::AABB world_bounds() const override;
    void tessellate(int nseg, std::vector<math::vec3>& verts,
                    std::vector<std::uint32_t>& indices) const override;

    double focal_length()         const { return focal_length_; }
    double aperture_half_width()  const { return aperture_half_width_; }
    double aperture_half_length() const { return aperture_half_length_; }

private:
    double focal_length_;
    double aperture_half_width_;
    double aperture_half_length_;
};

} // namespace scrt::surfaces
