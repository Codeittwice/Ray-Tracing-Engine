#include "scrt/materials/Dielectric.hpp"
#include <cmath>

namespace scrt::materials {

Dielectric::Dielectric(double n, double absorption_per_m)
    : n_(n), alpha_(absorption_per_m) {}

Interaction Dielectric::interact(const core::Ray& r, const core::Hit& h,
                                 math::Rng& /*rng*/) const {
    // front_face=true → entering medium (air→glass), front_face=false → exiting (glass→air)
    double n1 = h.front_face ? 1.0 : n_;
    double n2 = h.front_face ? n_  : 1.0;

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
    double p = r.power;
    if (!h.front_face && alpha_ > 0.0)
        p *= std::exp(-alpha_ * h.t);

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
