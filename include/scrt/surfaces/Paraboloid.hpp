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
    core::AABB local_bounds() const override;
    /// UniformOnly: a non-uniformly scaled paraboloid still traces correctly but is no longer
    /// a paraboloid and has no focus, while the UI would still report focal_length_m.
    ScaleSupport scale_support() const override { return ScaleSupport::UniformOnly; }
    void tessellate(int nseg, std::vector<math::vec3>& verts,
                    std::vector<std::uint32_t>& indices) const override;

    double focal_length()    const { return focal_length_; }
    double aperture_radius() const { return aperture_radius_; }
    /// Sets the focal length f in x²+y²=4fz.
    void set_focal_length(double focal_length) { focal_length_ = focal_length; }
    /// Sets the maximum radial extent of the aperture.
    void set_aperture_radius(double aperture_radius) { aperture_radius_ = aperture_radius; }

private:
    double focal_length_;
    double aperture_radius_;
};

} // namespace scrt::surfaces
