#include "scrt/materials/Absorber.hpp"

namespace scrt::materials {

Interaction Absorber::interact(const core::Ray& r, const core::Hit& /*h*/,
                               math::Rng& /*rng*/) const {
    Interaction ia;
    ia.kind      = InteractionKind::Absorbed;
    ia.terminate = true;
    ia.reflected = r;  // unused but avoids indeterminate value
    return ia;
}

} // namespace scrt::materials
