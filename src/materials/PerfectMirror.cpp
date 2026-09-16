#include "scrt/materials/PerfectMirror.hpp"
#include "scrt/optics/Polarisation.hpp"

namespace scrt::materials {

Interaction PerfectMirror::interact(const core::Ray& r, const core::Hit& h,
                                    math::Rng& /*rng*/) const {
    Interaction ia;
    ia.kind            = InteractionKind::Reflected;
    ia.reflected       = r;
    ia.reflected.origin    = h.position;
    ia.reflected.direction = optics::reflect(r.direction, h.normal);
    ia.reflected.bounces   = r.bounces + 1;
    // A perfect conductor: rs = -1, rp = +1 in this project's convention (the n2 -> infinity limit
    // of fresnel_amplitudes), so the field flips at normal incidence and circular light changes hand.
    if (r.polarised) optics::transfer_state(r, h.normal, ia.reflected, -1.0, 1.0);
    return ia;
}

} // namespace scrt::materials
