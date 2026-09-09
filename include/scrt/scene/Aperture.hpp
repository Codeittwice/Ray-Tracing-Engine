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
/// well conditioned.
///
/// NOT byte-identical to the three copies it replaced, contrary to what this comment used
/// to claim: those received an already-unit normal and used it as-is, whereas the
/// glm::normalize below re-normalizes it. Re-normalizing a unit vector is not the identity
/// in floating point — for roughly one direction in seven the result differs in the last
/// bit, which perturbs u and v and therefore the sampled ray sequence. It is inert today
/// only because every bundled scene has direction exactly (0, 0, -1), where 1/sqrt(1) is
/// exact and the two frames agree bit for bit. Change the arithmetic below only with fresh
/// golden flux baselines in hand: off-axis suns will move.
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
    ///
    /// Throws std::invalid_argument rather than returning a nonsense disk when
    ///   - `margin` is negative or not finite (a negative margin shrinks the disk below the
    ///     bounds it is supposed to cover, so covers() would fail on its own output),
    ///   - `bounds` is inside-out or unbounded, as a default-constructed core::AABB is
    ///     (min = +DBL_MAX, max = -DBL_MAX gives radius +inf and an infinite centre), or
    ///   - `to_sun` is zero-length, or points below the horizon (+Z is the zenith). A
    ///     below-horizon sun would place the disk *underneath* the cooker and light it
    ///     from underground, producing plausible-looking power out of a sun that has set.
    static Aperture auto_fit(const core::AABB& bounds, math::vec3 to_sun, double margin);

    /// True when every corner of bounds lies behind this disk and inside its projection.
    bool covers(const core::AABB& bounds, math::vec3 to_sun) const;
};

} // namespace scrt::scene
