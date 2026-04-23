#include "scrt/materials/PerfectMirror.hpp"

namespace scrt::materials {

Interaction PerfectMirror::interact(const core::Ray& r, const core::Hit& h,
                                    math::Rng& /*rng*/) const {
    Interaction ia;
    ia.kind            = InteractionKind::Reflected;
    ia.reflected       = r;
    ia.reflected.origin    = h.position;
    ia.reflected.direction = optics::reflect(r.direction, h.normal);
    ia.reflected.bounces   = r.bounces + 1;
    return ia;
}

} // namespace scrt::materials
