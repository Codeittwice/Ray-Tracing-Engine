#include "scrt/sources/Laser.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/scene/Aperture.hpp"   // orthonormal_frame
#include <cmath>
#include <stdexcept>
#include <string>

namespace scrt::sources {

namespace {
void require_finite_nonneg(double v, const char* what) {
    if (!std::isfinite(v) || v < 0.0)
        throw std::invalid_argument(std::string("Laser::") + what +
                                    ": must be finite and >= 0 (got " + std::to_string(v) + ")");
}
} // namespace

void Laser::set_direction(math::vec3 d) {
    if (glm::dot(d, d) <= 0.0)
        throw std::invalid_argument("Laser::set_direction: zero-length direction names no beam");
    direction_ = math::safe_normalize(d);
}

void Laser::set_power_w(double w) {
    require_finite_nonneg(w, "set_power_w");
    power_w_ = w;
}

void Laser::set_wavelength_nm(double nm) {
    if (!std::isfinite(nm) || nm <= 0.0)
        throw std::invalid_argument("Laser::set_wavelength_nm: must be finite and > 0 (got " +
                                    std::to_string(nm) + ")");
    wavelength_nm_ = nm;
}

void Laser::set_beam_diameter_m(double d) {
    require_finite_nonneg(d, "set_beam_diameter_m");
    beam_diameter_m_ = d;
}

void Laser::set_divergence_mrad(double mrad) {
    require_finite_nonneg(mrad, "set_divergence_mrad");
    divergence_mrad_ = mrad;
}

core::Ray Laser::sample_ray(math::Rng& rng) const {
    // Always the same three draws (disk pair, theta, phi) whatever the parameters, so a
    // collimated pencil beam consumes the slot's RNG sequence exactly like a divergent one.
    const math::vec2 disk = rng.unit_disk_concentric();
    math::vec3 u, v;
    scene::orthonormal_frame(direction_, u, v);
    const math::vec3 origin =
        origin_ + (0.5 * beam_diameter_m_) * (disk.x * u + disk.y * v);

    // Uniform over the solid-angle cap of half-angle div/2: theta = half * sqrt(xi).
    const double half  = 0.5 * divergence_mrad_ * 1e-3;
    const double theta = half * std::sqrt(rng.uniform01());
    const double phi   = math::TWO_PI * rng.uniform01();
    const double sin_t = std::sin(theta);
    const double cos_t = std::cos(theta);

    core::Ray ray;
    ray.origin        = origin;
    ray.direction     = math::safe_normalize(
        cos_t * direction_ + sin_t * (std::cos(phi) * u + std::sin(phi) * v));
    ray.power         = 1.0;   // Relative weight; the tracer applies total_power_w() / N.
    ray.wavelength_nm = wavelength_nm_;
    return ray;
}

} // namespace scrt::sources
