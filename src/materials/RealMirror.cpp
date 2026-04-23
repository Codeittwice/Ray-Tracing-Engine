#include "scrt/materials/RealMirror.hpp"
#include "scrt/math/Constants.hpp"
#include <cmath>

namespace scrt::materials {

RealMirror::RealMirror(double reflectance, double slope_error_mrad)
    : rho_(reflectance), slope_error_(slope_error_mrad) {}

Interaction RealMirror::interact(const core::Ray& r, const core::Hit& h,
                                 math::Rng& rng) const {
    math::vec3 n = h.normal;

    if (slope_error_ > 0.0) {
        // Perturb normal by Gaussian slope error in the tangent plane
        double sigma = slope_error_ * 1e-3;  // mrad → rad
        math::vec3 ref = (std::abs(n.x) < 0.9) ? math::vec3{1.0, 0.0, 0.0}
                                                : math::vec3{0.0, 1.0, 0.0};
        math::vec3 t1 = glm::normalize(glm::cross(n, ref));
        math::vec3 t2 = glm::cross(n, t1);
        n = math::safe_normalize(n + rng.gaussian(sigma) * t1
                                   + rng.gaussian(sigma) * t2);
        // Re-ensure normal opposes ray after perturbation
        if (glm::dot(r.direction, n) > 0.0)
            n = -n;
    }

    Interaction ia;
    ia.kind               = InteractionKind::Reflected;
    ia.reflected          = r;
    ia.reflected.origin   = h.position;
    ia.reflected.direction = optics::reflect(r.direction, n);
    ia.reflected.power    = r.power * rho_;
    ia.reflected.bounces  = r.bounces + 1;
    return ia;
}

} // namespace scrt::materials
