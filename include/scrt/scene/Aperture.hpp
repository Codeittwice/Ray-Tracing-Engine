#pragma once
#include "scrt/core/AABB.hpp"
#include "scrt/math/Vec.hpp"
#include <cmath>

namespace scrt::scene {

/// Build two unit vectors u, v spanning the plane perpendicular to unit normal n.
///
/// Single shared definition of the reference-axis heuristic that Aperture::tangent_frame,
/// Pillbox::sample_ray and Buie::sample_ray each used to carry their own copy of. The
/// |n.x| < 0.9 pick guarantees `ref` is never near-parallel to n, so the cross product is
/// well conditioned. Keep the arithmetic below byte-identical: sampled ray sequences (and
/// therefore the golden flux baselines) depend on it.
inline void orthonormal_frame(math::vec3 n, math::vec3& u, math::vec3& v) {
    n = glm::normalize(n);
    const math::vec3 ref = (std::abs(n.x) < 0.9) ? math::vec3{1.0, 0.0, 0.0}
                                                 : math::vec3{0.0, 1.0, 0.0};
    u = glm::normalize(glm::cross(n, ref));
    v = glm::cross(n, u);
}

/// How the collection aperture is positioned relative to the sun.
enum class ApertureMode {
    Fixed,        ///< Centre, normal and radius are taken verbatim from the scene.
    AutoFitToSun  ///< Re-fitted to face the sun and enclose the scene bounds.
};

/// Disk-shaped collection aperture perpendicular to the sun direction.
struct Aperture {
    math::vec3   center {0.0, 0.0, 2.0};        ///< World-space center.
    math::vec3   normal {0.0, 0.0, 1.0};        ///< Unit normal (toward sun).
    double       radius {1.0};                  ///< Metres.
    ApertureMode mode   {ApertureMode::Fixed};  ///< Fixed keeps existing scenes unchanged.
    double       margin {0.05};                 ///< Fractional slack used by auto_fit.

    double area() const;  ///< pi * radius^2

    /// Two orthonormal vectors spanning the aperture disk.
    void tangent_frame(math::vec3& u, math::vec3& v) const;

    /// Foreshortening factor max(0, dot(unit normal, to_sun)); 1 when facing the sun.
    double cosine_to(math::vec3 to_sun) const;

    /// Disk facing to_sun, pushed clear of bounds and sized to enclose their projection.
    static Aperture auto_fit(const core::AABB& bounds, math::vec3 to_sun, double margin);

    /// True when every corner of bounds lies behind this disk and inside its projection.
    bool covers(const core::AABB& bounds, math::vec3 to_sun) const;
};

} // namespace scrt::scene
