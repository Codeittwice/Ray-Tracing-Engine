#include "scrt/sources/Pillbox.hpp"
#include "scrt/scene/Aperture.hpp"
#include "scrt/math/Constants.hpp"
#include <cmath>

namespace scrt::sources {

Pillbox::Pillbox(double half_angle_rad) : half_angle_(half_angle_rad) {}

core::Ray Pillbox::sample_ray(const scene::Aperture& ap, math::Rng& rng) const {
    // 1. Uniform sample on aperture disk
    math::vec2 disk = rng.unit_disk_concentric();
    math::vec3 u, v;
    ap.tangent_frame(u, v);
    math::vec3 origin = ap.center + ap.radius * (disk.x * u + disk.y * v);

    // 2. Perturb sun direction within pillbox cone: theta = half_angle*sqrt(xi)
    //    gives uniform area distribution within the solid-angle cap.
    double theta = half_angle_ * std::sqrt(rng.uniform01());
    double phi   = scrt::math::TWO_PI * rng.uniform01();
    double sin_t = std::sin(theta);
    double cos_t = std::cos(theta);

    // Build orthonormal frame with sun_direction_ as the cone axis
    math::vec3 axis = glm::normalize(sun_direction_);
    math::vec3 ref  = (std::abs(axis.x) < 0.9) ? math::vec3{1.0, 0.0, 0.0}
                                                : math::vec3{0.0, 1.0, 0.0};
    math::vec3 perp1 = glm::normalize(glm::cross(axis, ref));
    math::vec3 perp2 = glm::cross(axis, perp1);

    math::vec3 direction = math::safe_normalize(
        cos_t * axis + sin_t * (std::cos(phi) * perp1 + std::sin(phi) * perp2));

    core::Ray ray;
    ray.origin    = origin;
    ray.direction = direction;
    ray.power     = 1.0;  // Tracer scales to DNI * area / N
    return ray;
}

} // namespace scrt::sources
