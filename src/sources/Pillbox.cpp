#include "scrt/sources/Pillbox.hpp"
#include "scrt/math/Constants.hpp"
#include <cmath>

namespace scrt::sources {

Pillbox::Pillbox(double half_angle_rad) : half_angle_(half_angle_rad) {}

core::Ray Pillbox::sample_ray(math::Rng& rng) const {
    // 1. Uniform sample on aperture disk
    math::vec2 disk = rng.unit_disk_concentric();
    math::vec3 u, v;
    aperture_.tangent_frame(u, v);
    math::vec3 origin = aperture_.center + aperture_.radius * (disk.x * u + disk.y * v);

    // 2. Perturb sun direction within pillbox cone: theta = half_angle*sqrt(xi)
    //    gives uniform area distribution within the solid-angle cap.
    double theta = half_angle_ * std::sqrt(rng.uniform01());
    double phi   = scrt::math::TWO_PI * rng.uniform01();
    double sin_t = std::sin(theta);
    double cos_t = std::cos(theta);

    // Build orthonormal frame with sun_direction_ as the cone axis
    math::vec3 axis = glm::normalize(sun_direction_);
    math::vec3 perp1, perp2;
    scene::orthonormal_frame(axis, perp1, perp2);

    math::vec3 direction = math::safe_normalize(
        cos_t * axis + sin_t * (std::cos(phi) * perp1 + std::sin(phi) * perp2));

    core::Ray ray;
    ray.origin    = origin;
    ray.direction = direction;
    ray.power         = 1.0;  // Relative weight; the tracer applies total_power_w() / N.
    ray.wavelength_nm = wavelength_nm_;
    return ray;
}

} // namespace scrt::sources
