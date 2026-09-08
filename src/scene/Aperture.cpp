#include "scrt/scene/Aperture.hpp"
#include "scrt/math/Constants.hpp"
#include <algorithm>
#include <cmath>

namespace scrt::scene {

double Aperture::area() const {
    return scrt::math::PI * radius * radius;
}

void Aperture::tangent_frame(math::vec3& u, math::vec3& v) const {
    orthonormal_frame(normal, u, v);
}

double Aperture::cosine_to(math::vec3 to_sun) const {
    // DNI is defined per unit area normal to the beam, so an aperture tilted away from
    // the sun collects only its foreshortened projection. Facing away collects nothing.
    return std::max(0.0, glm::dot(glm::normalize(normal), to_sun));
}

Aperture Aperture::auto_fit(const core::AABB& bounds, math::vec3 to_sun, double margin) {
    const math::vec3 axis = glm::normalize(to_sun);
    // Half the box diagonal bounds the distance from the centroid to any corner, so a
    // disk of that radius placed on the far side of the box always covers its shadow.
    const double R = 0.5 * glm::length(bounds.max() - bounds.min());
    const double scaled = R * (1.0 + margin);

    Aperture ap;
    ap.mode   = ApertureMode::AutoFitToSun;
    ap.margin = margin;
    ap.normal = axis;
    ap.center = bounds.centroid() + axis * scaled;
    ap.radius = scaled;
    return ap;
}

bool Aperture::covers(const core::AABB& bounds, math::vec3 to_sun) const {
    const math::vec3 axis = glm::normalize(to_sun);
    // Relative slack absorbs the rounding of the corner projections; it is far below any
    // physically meaningful gap for the metre-scale geometry this code handles.
    const double tol = 1e-12 * std::max(1.0, radius);

    for (int corner = 0; corner < 8; ++corner) {
        const math::vec3 p{
            (corner & 1) ? bounds.max().x : bounds.min().x,
            (corner & 2) ? bounds.max().y : bounds.min().y,
            (corner & 4) ? bounds.max().z : bounds.min().z};

        const math::vec3 d     = p - center;
        const double     along = glm::dot(d, axis);
        if (along > tol)
            return false;  // Corner sits in front of the disk; rays start past it.

        const math::vec3 perp = d - along * axis;
        if (glm::length(perp) > radius + tol)
            return false;  // Corner falls outside the disk's shadow.
    }
    return true;
}

} // namespace scrt::scene
