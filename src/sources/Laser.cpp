#include "scrt/sources/Laser.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/scene/Aperture.hpp"   // orthonormal_frame
#include <algorithm>
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
    // A full angle above 180 degrees would put part of the cap behind the emitter and send
    // rays backwards, silently. Refuse it rather than sample it.
    if (mrad > 1000.0 * math::PI)
        throw std::invalid_argument("Laser::set_divergence_mrad: full-angle divergence must be "
                                    "<= 180 degrees (" + std::to_string(1000.0 * math::PI) +
                                    " mrad); got " + std::to_string(mrad));
    divergence_mrad_ = mrad;
}

void Laser::set_polarisation(optics::PolarisationKind kind, double linear_deg) {
    if (!std::isfinite(linear_deg))
        throw std::invalid_argument("Laser::set_polarisation: linear angle must be finite");
    polarisation_     = kind;
    polarisation_deg_ = linear_deg;
}

void Laser::set_coherence_length_m(double m) {
    require_finite_nonneg(m, "set_coherence_length_m");
    coherence_length_m_ = m;
}

namespace {

/// Radical inverse of k in the given base (van der Corput): a low-discrepancy sequence in [0, 1).
double radical_inverse(std::size_t k, std::size_t base) {
    double inv = 1.0 / static_cast<double>(base), f = inv, r = 0.0;
    while (k > 0) {
        r += f * static_cast<double>(k % base);
        k /= base;
        f *= inv;
    }
    return r;
}

} // namespace

core::Ray Laser::sample_ray_indexed(math::Rng& rng, std::size_t k, std::size_t n) const {
    if (sampling_ != Sampling::Grid || n == 0) return sample_ray(rng);
    // Vogel's sunflower spiral: point k at radius sqrt((k+1/2)/n) and angle k times the golden angle
    // covers the disk with equal area per point and no preferred direction - so every receiver bin
    // under the beam sees rays from neighbouring, smoothly varying positions. No Rng draw at all.
    const double r = std::sqrt((static_cast<double>(k) + 0.5) / static_cast<double>(n));
    const double a = static_cast<double>(k) * 2.399963229728653;   // pi (3 - sqrt 5)
    return make_ray({r * std::cos(a), r * std::sin(a)}, radical_inverse(k + 1, 2),
                    radical_inverse(k + 1, 3));
}

core::Ray Laser::sample_ray(math::Rng& rng) const {
    // Always the same three draws (disk pair, theta, phi) whatever the parameters, so a
    // collimated pencil beam consumes the slot's RNG sequence exactly like a divergent one.
    const math::vec2 disk  = rng.unit_disk_concentric();
    const double     cap_u = rng.uniform01();
    const double     phi_u = rng.uniform01();
    return make_ray(disk, cap_u, phi_u);
}

core::Ray Laser::make_ray(math::vec2 disk, double cap_u, double phi_u) const {
    math::vec3 u, v;
    scene::orthonormal_frame(direction_, u, v);
    const math::vec3 origin =
        origin_ + (0.5 * beam_diameter_m_) * (disk.x * u + disk.y * v);

    // Exactly uniform over the spherical cap of half-angle div/2: cos(theta) is uniform on
    // [cos(half), 1]. Pillbox uses theta = half * sqrt(xi), the small-angle form, which is
    // exact to theta^2/6 at 4.65 mrad but not at a divergence a user may type in degrees.
    // Same two draws in the same order (cap, then azimuth) as before.
    const double half  = 0.5 * divergence_mrad_ * 1e-3;
    const double cos_t = 1.0 - cap_u * (1.0 - std::cos(half));
    const double sin_t = std::sqrt(std::max(0.0, 1.0 - cos_t * cos_t));
    const double phi   = math::TWO_PI * phi_u;

    core::Ray ray;
    ray.origin        = origin;
    ray.direction     = math::safe_normalize(
        cos_t * direction_ + sin_t * (std::cos(phi) * u + std::sin(phi) * v));
    ray.power         = 1.0;   // Relative weight; the tracer applies total_power_w() / N.
    ray.wavelength_nm = wavelength_nm_;
    if (polarisation_ != optics::PolarisationKind::Unpolarised)
        optics::set_polarisation(ray, polarisation_, polarisation_deg_ * math::PI / 180.0);
    return ray;
}

} // namespace scrt::sources
