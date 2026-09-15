#pragma once
#include "scrt/surfaces/Surface.hpp"

namespace scrt::surfaces {

/// Flat circular disk in the local z = 0 plane, optionally with a concentric hole.
///
/// Radius alone makes a round mirror, screen or absorber; a nonzero hole radius makes an iris
/// or aperture stop, which passes light through the hole and stops it on the ring. The circular
/// aperture is also Wave 6's Airy demo. A new type rather than a clip on Plane, so the rectangle
/// every golden scene traces through is untouched by construction (Wave 3 gap G3).
class Disk final : public Surface {
public:
    /// radius: outer radius [m]; hole_radius: concentric hole [m], 0 for a solid disk.
    /// Throws std::invalid_argument unless 0 <= hole_radius < radius.
    explicit Disk(double radius, double hole_radius = 0.0);

    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const override;
    core::AABB local_bounds() const override;
    /// UniformOnly: a non-uniform scale makes an ellipse, so radius() stops describing it.
    ScaleSupport scale_support() const override { return ScaleSupport::UniformOnly; }
    void tessellate(int nseg, std::vector<math::vec3>& verts,
                    std::vector<std::uint32_t>& indices) const override;

    double radius() const { return radius_; }
    double hole_radius() const { return hole_; }

private:
    double radius_;
    double hole_;
};

} // namespace scrt::surfaces
