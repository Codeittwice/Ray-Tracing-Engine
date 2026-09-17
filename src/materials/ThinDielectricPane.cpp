#include "scrt/materials/ThinDielectricPane.hpp"
#include "scrt/optics/Polarisation.hpp"
#include <algorithm>
#include <cmath>

namespace scrt::materials {

ThinDielectricPane::ThinDielectricPane(double n, double thickness_m,
                                       double absorption_per_m)
    : n_(n), thickness_m_(thickness_m), absorption_per_m_(absorption_per_m) {}

namespace {

/// Incoherent two-face slab reflectance and transmittance for ONE polarisation's interface R.
void slab(double r_interface, double a, double& slab_r, double& slab_t) {
    const double denom = std::max(1.0e-12, 1.0 - r_interface * r_interface * a * a);
    slab_t = ((1.0 - r_interface) * (1.0 - r_interface) * a) / denom;
    slab_r = r_interface + ((1.0 - r_interface) * (1.0 - r_interface) * r_interface * a * a) / denom;
}

} // namespace

Interaction ThinDielectricPane::interact(const core::Ray& r, const core::Hit& h,
                                         math::Rng& rng) const {
    if (r.polarised) {
        // The same incoherent slab as below, done separately for s and p, since a pane at an angle
        // reflects s far more than p. Internal-reflection PHASES are not tracked (the model has
        // always summed the bounces in power), so the state gets each branch's amplitude with the
        // single-interface sign; a thin film's interference colours are out of reach here.
        const double cos_i  = std::clamp(std::abs(glm::dot(r.direction, h.normal)), 0.0, 1.0);
        const double sin_i2 = std::max(0.0, 1.0 - cos_i * cos_i);
        const double cos_t  = std::sqrt(std::max(0.0, 1.0 - sin_i2 / (n_ * n_)));
        const auto   amp    = optics::fresnel_amplitudes(cos_i, cos_t, 1.0, n_);
        const double a = std::exp(-std::max(0.0, absorption_per_m_) * thickness_m_
                                  / std::max(cos_t, 1.0e-9));
        double rs_slab, ts_slab, rp_slab, tp_slab;
        slab(std::clamp(amp.rs * amp.rs, 0.0, 1.0), a, rs_slab, ts_slab);
        slab(std::clamp(amp.rp * amp.rp, 0.0, 1.0), a, rp_slab, tp_slab);
        const double sgn_s = amp.rs < 0.0 ? -1.0 : 1.0;
        const double sgn_p = amp.rp < 0.0 ? -1.0 : 1.0;
        const math::vec3 n = glm::dot(r.direction, h.normal) < 0.0 ? h.normal : -h.normal;

        Interaction ia;
        ia.kind                = InteractionKind::Split;
        ia.reflected           = r;
        ia.reflected.origin    = h.position;
        ia.reflected.direction = optics::reflect(r.direction, h.normal);
        ia.reflected.bounces   = r.bounces + 1;
        ia.reflected.power     = r.power * optics::transfer_state(
            r, n, ia.reflected, sgn_s * std::sqrt(rs_slab), sgn_p * std::sqrt(rp_slab));

        ia.transmitted           = r;
        ia.transmitted.origin    = h.position + r.direction * 1.0e-7;
        ia.transmitted.direction = r.direction;
        ia.transmitted.bounces   = r.bounces + 1;
        ia.transmitted.power     = r.power * optics::transfer_state(
            r, n, ia.transmitted, std::sqrt(ts_slab), std::sqrt(tp_slab));
        ia.transmitted.opl_m += n_ * thickness_m_ / std::max(cos_t, 1.0e-9) + r.medium_n * 1.0e-7;
        return ia;
    }
    (void)rng;
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
    // Wave 5 optical path: the pane has no geometric thickness, so its glass is added here - n t
    // along the refracted path - plus the 1e-7 m the origin is nudged. Air the real slab would have
    // displaced is not subtracted: this model has never offset the transmitted ray either.
    ia.transmitted.opl_m += n_ * thickness_m_ / std::max(cos_t, 1.0e-9) + r.medium_n * 1.0e-7;
    return ia;
}

} // namespace scrt::materials
