#pragma once
#include "scrt/surfaces/Surface.hpp"

namespace scrt::surfaces {

/// A real lens: two spherical (or flat) faces joined by a cylindrical rim, as ONE closed solid.
///
/// Local frame: the optical axis is z, the front vertex sits at z = -t/2 and the back vertex at
/// z = +t/2, where t is the centre thickness. Radii are SIGNED: a positive radius puts that
/// face's centre of curvature on the +z side of the face, so a bi-convex lens is (R, -R), a
/// plano-convex (R, 0), a bi-concave (-R, R). A radius of 0 means flat.
///
/// Why one solid and not two cap surfaces (decision 3-B): Scene::intersect tracks no medium, and
/// Dielectric decides "entering or leaving glass" from Hit::front_face per hit. A closed body
/// makes that decision well defined for every ray: whatever enters through a face leaves through
/// a face or the rim. Two separate caps would let a ray exit through nothing into "glass
/// forever". The rim IS traced, as glass: a ray that leaves through the edge refracts there,
/// which is what an uncoated edge does.
///
/// Known limit, unchanged by this class: two overlapping dielectric bodies are undefined (gap G5).
/// Keep lenses disjoint.
class ThickLens final : public Surface {
public:
    /// Throws std::invalid_argument when a radius is finite but smaller than the half-diameter
    /// (the face cannot span the aperture), when the edge thickness would be negative (the faces
    /// cross inside the aperture), or when any length is non-positive.
    ThickLens(double radius1, double radius2, double center_thickness_m, double diameter_m);

    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const override;
    core::AABB local_bounds() const override;
    /// UniformOnly: a non-uniform scale makes the faces non-spherical, so the radii stop
    /// describing the optics.
    ScaleSupport scale_support() const override { return ScaleSupport::UniformOnly; }
    void tessellate(int nseg, std::vector<math::vec3>& verts,
                    std::vector<std::uint32_t>& indices) const override;

    double radius1() const { return r1_; }
    double radius2() const { return r2_; }
    double center_thickness() const { return t_; }
    double diameter() const { return d_; }

    /// Local z of face 1 at radial distance rho from the axis (rho <= diameter/2).
    double z_front(double rho) const;
    /// Local z of face 2 at radial distance rho from the axis.
    double z_back(double rho) const;
    /// Glass thickness at the rim, >= 0 by construction.
    double edge_thickness() const { return z_back(0.5 * d_) - z_front(0.5 * d_); }

private:
    double r1_, r2_, t_, d_;
};

} // namespace scrt::surfaces
