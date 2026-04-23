#pragma once
#include "scrt/math/Vec.hpp"

namespace scrt::scene {

/// Disk-shaped collection aperture perpendicular to the sun direction.
struct Aperture {
    math::vec3 center  {0.0, 0.0, 2.0};  ///< World-space center.
    math::vec3 normal  {0.0, 0.0, 1.0};  ///< Unit normal (toward sun).
    double     radius  {1.0};            ///< Metres.

    double area() const;  ///< pi * radius^2

    /// Two orthonormal vectors spanning the aperture disk.
    void tangent_frame(math::vec3& u, math::vec3& v) const;
};

} // namespace scrt::scene
