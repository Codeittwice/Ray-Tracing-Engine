#include "scrt/materials/ThinDielectricPane.hpp"
#include <algorithm>
#include <cmath>

namespace scrt::materials {

ThinDielectricPane::ThinDielectricPane(double n, double thickness_m,
                                       double absorption_per_m)
    : n_(n), thickness_m_(thickness_m), absorption_per_m_(absorption_per_m) {}

Interaction ThinDielectricPane::interact(const core::Ray& r, const core::Hit& h,
                                         math::Rng& /*rng*/) const {
    const double cos_i = std::clamp(std::abs(glm::dot(r.direction, h.normal)), 0.0, 1.0);
    const double sin_i2 = std::max(0.0, 1.0 - cos_i * cos_i);
    const double sin_t2 = sin_i2 / (n_ * n_);
    const double cos_t = std::sqrt(std::max(0.0, 1.0 - sin_t2));
    const auto fr = optics::fresnel_unpolarized(cos_i, cos_t, 1.0, n_);
    const double r_interface = std::clamp(fr.R, 0.0, 1.0);
    const double a = std::exp(-std::max(0.0, absorption_per_m_) * thickness_m_
                              / std::max(cos_t, 1.0e-9));
    const double denom = std::max(1.0e-12, 1.0 - r_interface * r_interface * a * a);
    const double slab_t = ((1.0 - r_interface) * (1.0 - r_interface) * a) / denom;
    const double slab_r =
        r_interface + ((1.0 - r_interface) * (1.0 - r_interface)
                       * r_interface * a * a) / denom;

    Interaction ia;
    ia.kind = InteractionKind::Split;

    ia.reflected = r;
    ia.reflected.origin = h.position;
    ia.reflected.direction = optics::reflect(r.direction, h.normal);
    ia.reflected.power = r.power * std::clamp(slab_r, 0.0, 1.0);
    ia.reflected.bounces = r.bounces + 1;

    ia.transmitted = r;
    ia.transmitted.origin = h.position + r.direction * 1.0e-7;
    ia.transmitted.direction = r.direction;
    ia.transmitted.power = r.power * std::clamp(slab_t, 0.0, 1.0);
    ia.transmitted.bounces = r.bounces + 1;
    return ia;
}

} // namespace scrt::materials
