#include "scrt/materials/Dielectric.hpp"
#include "scrt/optics/Polarisation.hpp"
#include <algorithm>
#include <cmath>

namespace scrt::materials {

Dielectric::Dielectric(double n, double absorption_per_m)
    : n_(n), alpha_(absorption_per_m) {}

void Dielectric::set_alpha_spectrum(std::vector<std::pair<double,double>> spec) {
    std::sort(spec.begin(), spec.end());
    alpha_spectrum_ = std::move(spec);
}

double Dielectric::alpha_at(double wavelength_nm) const {
    if (alpha_spectrum_.empty())
        return alpha_;
    if (wavelength_nm <= alpha_spectrum_.front().first)
        return alpha_spectrum_.front().second;
    if (wavelength_nm >= alpha_spectrum_.back().first)
        return alpha_spectrum_.back().second;
    // Binary search for the upper bracket node.
    auto it = std::lower_bound(alpha_spectrum_.begin(), alpha_spectrum_.end(),
                               std::make_pair(wavelength_nm, 0.0));
    const auto& hi = *it;
    const auto& lo = *std::prev(it);
    double t = (wavelength_nm - lo.first) / (hi.first - lo.first);
    return lo.second + t * (hi.second - lo.second);
}

double Dielectric::n_at(double wavelength_nm) const {
    if (!sellmeier_) return n_;
    const auto& s = *sellmeier_;
    double lam_um = wavelength_nm * 1e-3;       // nm → µm
    double l2     = lam_um * lam_um;
    double n2     = 1.0
                  + s.B1 * l2 / (l2 - s.C1)
                  + s.B2 * l2 / (l2 - s.C2)
                  + s.B3 * l2 / (l2 - s.C3);
    return std::sqrt(n2 > 1.0 ? n2 : 1.0);
}

namespace {

/// The polarised twin of Dielectric::interact: same refraction, same Beer-Lambert, but reflectance
/// from the ray's own s/p state instead of the average, and complex phases under TIR.
///
/// A separate function, and entered only for a polarised ray, so the unpolarised body below is not
/// touched by so much as a reordered expression: every existing dielectric result stays bit-identical.
Interaction interact_polarised(const core::Ray& r, const core::Hit& h, double n_glass,
                               double alpha) {
    const double n1    = h.front_face ? 1.0 : n_glass;
    const double n2    = h.front_face ? n_glass : 1.0;
    const double cos_i = -glm::dot(r.direction, h.normal);

    bool             tir   = false;
    const math::vec3 t_dir = optics::refract(r.direction, h.normal, n1 / n2, tir);

    if (tir) {
        optics::cplx rs, rp;
        optics::tir_amplitudes(cos_i, n1, n2, rs, rp);
        Interaction ia;
        ia.kind                = InteractionKind::Reflected;
        ia.reflected           = r;
        ia.reflected.origin    = h.position;
        ia.reflected.direction = optics::reflect(r.direction, h.normal);
        ia.reflected.bounces   = r.bounces + 1;
        optics::transfer_state(r, h.normal, ia.reflected, rs, rp);   // |rs| = |rp| = 1: power kept
        return ia;
    }

    const double cos_t = -glm::dot(t_dir, h.normal);
    const auto   a     = optics::fresnel_amplitudes(cos_i, cos_t, n1, n2);

    double p = r.power;
    if (!h.front_face && alpha > 0.0) p *= std::exp(-alpha * h.t);

    Interaction ia;
    ia.kind = InteractionKind::Split;

    ia.reflected           = r;
    ia.reflected.origin    = h.position;
    ia.reflected.direction = optics::reflect(r.direction, h.normal);
    ia.reflected.bounces   = r.bounces + 1;
    const double R = optics::transfer_state(r, h.normal, ia.reflected, a.rs, a.rp);
    ia.reflected.power = p * R;

    ia.transmitted           = r;
    ia.transmitted.origin    = h.position;
    ia.transmitted.direction = t_dir;
    ia.transmitted.bounces   = r.bounces + 1;
    ia.transmitted.medium_n  = n2;   // entering the glass, or leaving it into air
    // The state takes (ts, tp); the POWER is 1 - R, which is what (n2 ct / n1 ci)|t|^2 sums to and
    // avoids carrying that obliquity factor through a second path that could drift from it.
    optics::transfer_state(r, h.normal, ia.transmitted, a.ts, a.tp);
    ia.transmitted.power = p * (1.0 - R);
    return ia;
}

} // namespace

Interaction Dielectric::interact(const core::Ray& r, const core::Hit& h,
                                 math::Rng& rng) const {
    if (r.polarised) return interact_polarised(r, h, n_at(r.wavelength_nm), alpha_at(r.wavelength_nm));
    (void)rng;
    double n_glass = n_at(r.wavelength_nm);
    // front_face=true → entering medium (air→glass), front_face=false → exiting (glass→air)
    double n1 = h.front_face ? 1.0    : n_glass;
    double n2 = h.front_face ? n_glass : 1.0;

    // h.normal is already oriented against the incoming ray
    double cos_i = -glm::dot(r.direction, h.normal);  // positive

    bool tir = false;
    math::vec3 t_dir = optics::refract(r.direction, h.normal, n1 / n2, tir);

    if (tir) {
        Interaction ia;
        ia.kind               = InteractionKind::Reflected;
        ia.reflected          = r;
        ia.reflected.origin   = h.position;
        ia.reflected.direction = optics::reflect(r.direction, h.normal);
        ia.reflected.bounces  = r.bounces + 1;
        return ia;
    }

    double cos_t = -glm::dot(t_dir, h.normal);  // positive (t_dir goes into surface)
    auto fr = optics::fresnel_unpolarized(cos_i, cos_t, n1, n2);

    // Apply Beer-Lambert along the path already travelled inside the glass (on exit)
    double p     = r.power;
    double alpha = alpha_at(r.wavelength_nm);
    if (!h.front_face && alpha > 0.0)
        p *= std::exp(-alpha * h.t);

    Interaction ia;
    ia.kind = InteractionKind::Split;

    ia.reflected          = r;
    ia.reflected.origin   = h.position;
    ia.reflected.direction = optics::reflect(r.direction, h.normal);
    ia.reflected.power    = p * fr.R;
    ia.reflected.bounces  = r.bounces + 1;

    ia.transmitted          = r;
    ia.transmitted.origin   = h.position;
    ia.transmitted.direction = t_dir;
    ia.transmitted.power    = p * fr.T;
    ia.transmitted.bounces  = r.bounces + 1;
    // Wave 5: the medium the transmitted ray now travels in, for its optical path length. Power,
    // direction and every draw above are untouched, so no existing result moves (corpus-checked).
    ia.transmitted.medium_n = n2;

    return ia;
}

} // namespace scrt::materials
