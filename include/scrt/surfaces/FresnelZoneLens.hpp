#pragma once
#include "scrt/surfaces/Surface.hpp"
#include <vector>

namespace scrt::surfaces {

/// Flat Fresnel zone lens at z=0 in local space; focal point at (0,0,-focal_length).
/// Divided into n_zones annular zones each of width pitch, starting at inner_radius.
/// Each zone's facet normal is tilted so that a paraxial ray (0,0,-1) refracts
/// exactly toward the focal point. Pair with Dielectric(n_lens) for correct refraction.
class FresnelZoneLens final : public Surface {
public:
    /// @param focal_length   Distance from lens plane to focal point (m, > 0).
    /// @param inner_radius   Inner radius of the first zone (m).
    /// @param pitch          Radial width of each zone (m).
    /// @param n_zones        Number of annular zones.
    /// @param n_lens         Refractive index of the lens material (must match Dielectric material).
    FresnelZoneLens(double focal_length, double inner_radius,
                    double pitch, int n_zones, double n_lens);

    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const override;
    core::AABB local_bounds() const override;
    /// UniformOnly: per-zone facet normals are fictitious micro-facet normals on a flat plane,
    /// not surface gradients, so scaling them breaks focusing entirely.
    ScaleSupport scale_support() const override { return ScaleSupport::UniformOnly; }
    void tessellate(int nseg, std::vector<math::vec3>& verts,
                    std::vector<std::uint32_t>& indices) const override;

    double focal_length()  const { return focal_length_; }
    double inner_radius()  const { return inner_radius_; }
    double pitch()         const { return pitch_; }
    int    n_zones()       const { return n_zones_; }
    double n_lens()        const { return n_lens_; }
    double outer_radius()  const { return inner_radius_ + n_zones_ * pitch_; }
    /// Sets the focal length and recomputes per-zone facet normals.
    void set_focal_length(double focal_length) { focal_length_ = focal_length; rebuild_zones(); }
    /// Sets the inner radius of the first zone and recomputes per-zone facet normals.
    void set_inner_radius(double inner_radius) { inner_radius_ = inner_radius; rebuild_zones(); }
    /// Sets the radial zone width and recomputes per-zone facet normals.
    void set_pitch(double pitch) { pitch_ = pitch; rebuild_zones(); }

private:
    double focal_length_;
    double inner_radius_;
    double pitch_;
    int    n_zones_;
    double n_lens_;

    std::vector<double> sin_alpha_;  ///< Radial component of facet normal, per zone.
    std::vector<double> cos_alpha_;  ///< Z component of facet normal, per zone.

    /// Recomputes sin_alpha_/cos_alpha_ from focal_length_, inner_radius_ and pitch_.
    void rebuild_zones();
};

} // namespace scrt::surfaces
