#include "scrt/materials/BeamSplitter.hpp"
#include "scrt/optics/Polarisation.hpp"
#include "scrt/optics/Reflect.hpp"
#include <cmath>
#include <stdexcept>
#include <string>

namespace scrt::materials {

namespace {
void check(double r, double a) {
    if (!std::isfinite(r) || !std::isfinite(a) || r < 0.0 || a < 0.0 || r + a > 1.0)
        throw std::invalid_argument("BeamSplitter: need 0 <= reflectance, 0 <= absorptance and "
                                    "reflectance + absorptance <= 1 (got R=" +
                                    std::to_string(r) + ", A=" + std::to_string(a) + ")");
}
} // namespace

BeamSplitter::BeamSplitter(double reflectance, double absorptance)
    : r_(reflectance), a_(absorptance) {
    check(r_, a_);
}

void BeamSplitter::set_reflectance(double reflectance) {
    check(reflectance, a_);
    r_ = reflectance;
}

void BeamSplitter::set_absorptance(double absorptance) {
    check(r_, absorptance);
    a_ = absorptance;
}

Interaction BeamSplitter::interact(const core::Ray& r, const core::Hit& h,
                                   math::Rng& /*rng*/) const {
    // Both branches always, at the designed ratio, whatever the angle. The tracer drops a
    // branch below its power cutoff, so an R = 0 or T = 0 splitter costs nothing extra.
    Interaction ia;
    ia.kind = InteractionKind::Split;

    ia.reflected           = r;
    ia.reflected.origin    = h.position;
    ia.reflected.direction = optics::reflect(r.direction, h.normal);
    ia.reflected.power     = r.power * r_;
    ia.reflected.bounces   = r.bounces + 1;

    ia.transmitted           = r;
    ia.transmitted.origin    = h.position;
    ia.transmitted.direction = r.direction;   // zero thickness: no bend, no offset
    ia.transmitted.power     = r.power * (1.0 - r_ - a_);
    ia.transmitted.bounces   = r.bounces + 1;

    if (r.polarised) {
        // A DESIGNED, polarisation-independent ratio: power is untouched and the state goes through
        // with metal-like reflection phases (rs = -1, rp = +1 in this project's convention). A real
        // coating's s/p phases depend on its layer design, which this material does not describe.
        optics::transfer_state(r, h.normal, ia.reflected, -1.0, 1.0);
        optics::transfer_state(r, h.normal, ia.transmitted, 1.0, 1.0);
    }
    return ia;
}

} // namespace scrt::materials
