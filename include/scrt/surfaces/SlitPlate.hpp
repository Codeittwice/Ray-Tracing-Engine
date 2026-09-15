#pragma once
#include "scrt/surfaces/Surface.hpp"

namespace scrt::surfaces {

/// Flat rectangular plate in the local z = 0 plane with one or more open slots cut through it.
///
/// Slots run the full local height (along y), each `slit_width` wide, `slit_count` of them
/// centred on x = (i - (count - 1) / 2) * slit_pitch. A ray through a slot misses the plate and
/// continues; a ray on the plate hits it. One slot is Wave 6's single-slit demo, two the double
/// slit. A new type rather than a clip on Plane, for the same reason as Disk (gap G3).
class SlitPlate final : public Surface {
public:
    /// Throws std::invalid_argument unless every length is finite and > 0, slit_count >= 1,
    /// and (for two or more slots) slit_pitch > slit_width so the slots do not merge.
    SlitPlate(double half_width, double half_height, double slit_width, int slit_count,
              double slit_pitch);

    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const override;
    core::AABB local_bounds() const override;
    /// UniformOnly: a non-uniform scale changes the slit width the diffraction demos cite.
    ScaleSupport scale_support() const override { return ScaleSupport::UniformOnly; }
    void tessellate(int nseg, std::vector<math::vec3>& verts,
                    std::vector<std::uint32_t>& indices) const override;

    double half_width() const { return hw_; }
    double half_height() const { return hh_; }
    double slit_width() const { return slit_w_; }
    int    slit_count() const { return count_; }
    double slit_pitch() const { return pitch_; }

    /// True when local x lies inside one of the open slots.
    bool in_slot(double x) const;

private:
    double hw_, hh_, slit_w_, pitch_;
    int    count_;
};

} // namespace scrt::surfaces
