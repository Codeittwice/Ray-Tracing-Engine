#include "scrt/materials/Dielectric.hpp"
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

Interaction Dielectric::interact(const core::Ray& r, const core::Hit& h,
                                 math::Rng& /*rng*/) const {
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

    return ia;
}

} // namespace scrt::materials
