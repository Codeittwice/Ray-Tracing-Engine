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
    core::AABB local_bounds() const override;
    /// UniformOnly: a non-uniformly scaled paraboloid still traces correctly but is no longer
    /// a paraboloid and has no focus, while the UI would still report focal_length_m.
    ScaleSupport scale_support() const override { return ScaleSupport::UniformOnly; }
    void tessellate(int nseg, std::vector<math::vec3>& verts,
                    std::vector<std::uint32_t>& indices) const override;

    double focal_length()         const { return focal_length_; }
    double aperture_half_width()  const { return aperture_half_width_; }
    double aperture_half_length() const { return aperture_half_length_; }
    /// Sets the focal length f in x²=4fz.
    void set_focal_length(double focal_length) { focal_length_ = focal_length; }
    /// Sets the maximum |x| extent of the aperture.
    void set_aperture_half_width(double aperture_half_width) { aperture_half_width_ = aperture_half_width; }
    /// Sets the maximum |y| extent of the aperture.
    void set_aperture_half_length(double aperture_half_length) { aperture_half_length_ = aperture_half_length; }

private:
    double focal_length_;
    double aperture_half_width_;
    double aperture_half_length_;
};

} // namespace scrt::surfaces
