#include "scrt/sources/Buie.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/scene/Aperture.hpp"
#include <cassert>
#include <cmath>
#include <vector>

namespace scrt::sources {

// Buie et al. 2003, Solar Energy 74(2):113-122
// Solar disk edge θ_d = 4.65 mrad; circumsolar limit θ_c = 43.6 mrad.
static constexpr double THETA_DISK = 4.65e-3;  // rad
static constexpr double THETA_MAX  = 43.6e-3;  // rad
static constexpr int    N_TABLE    = 4096;

Buie::Buie(double chi) : chi_(chi) {
    assert(chi >= 0.01 && chi <= 0.15);

    // Circumsolar shape parameters (Eq. 5 & 6 in Buie 2003)
    double kappa = 0.9  * std::log(13.5 * chi) * std::pow(chi, -0.3);
    double gamma = 2.2  * std::log(0.52  * chi) * std::pow(chi,  0.43) - 0.1;

    // Build solid-angle-weighted PDF: f(θ) * sin(θ), uniformly in θ ∈ [0, THETA_MAX]
    std::vector<double> pdf(N_TABLE);
    double step = THETA_MAX / N_TABLE;
    for (int i = 0; i < N_TABLE; ++i) {
        double theta = (i + 0.5) * step;
        double radiance = (theta <= THETA_DISK)
            ? std::cos(0.326 * theta) / std::cos(0.308 * theta)
            : std::exp(kappa) * std::pow(theta, gamma);
        pdf[static_cast<std::size_t>(i)] = radiance * std::sin(theta);
    }

    theta_sampler_.build(std::move(pdf), 0.0, THETA_MAX);
}

core::Ray Buie::sample_ray(const scene::Aperture& ap, math::Rng& rng) const {
    // Uniform sample on aperture disk
    math::vec2 disk = rng.unit_disk_concentric();
    math::vec3 u, v;
    ap.tangent_frame(u, v);
    math::vec3 origin = ap.center + ap.radius * (disk.x * u + disk.y * v);

    // Sample angular radius from Buie CDF; azimuth uniform in [0, 2π)
    double theta = theta_sampler_.sample(rng.uniform01());
    double phi   = math::TWO_PI * rng.uniform01();
    double sin_t = std::sin(theta);
    double cos_t = std::cos(theta);

    // Orthonormal frame: sun_direction_ as cone axis
    math::vec3 axis  = glm::normalize(sun_direction_);
    math::vec3 ref   = (std::abs(axis.x) < 0.9) ? math::vec3{1.0, 0.0, 0.0}
                                                 : math::vec3{0.0, 1.0, 0.0};
    math::vec3 perp1 = glm::normalize(glm::cross(axis, ref));
    math::vec3 perp2 = glm::cross(axis, perp1);

    math::vec3 direction = math::safe_normalize(
        cos_t * axis + sin_t * (std::cos(phi) * perp1 + std::sin(phi) * perp2));

    core::Ray ray;
    ray.origin    = origin;
    ray.direction = direction;
    ray.power     = 1.0;  // Tracer scales by DNI * aperture_area / N_rays
    return ray;
}

} // namespace scrt::sources
