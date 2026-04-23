#include "scrt/scene/Aperture.hpp"
#include "scrt/math/Constants.hpp"
#include <cmath>

namespace scrt::scene {

double Aperture::area() const {
    return scrt::math::PI * radius * radius;
}

void Aperture::tangent_frame(math::vec3& u, math::vec3& v) const {
    // Build two orthonormal vectors perpendicular to the disk normal
    math::vec3 n = glm::normalize(normal);
    math::vec3 ref = (std::abs(n.x) < 0.9) ? math::vec3{1.0, 0.0, 0.0}
                                             : math::vec3{0.0, 1.0, 0.0};
    u = glm::normalize(glm::cross(n, ref));
    v = glm::cross(n, u);
}

} // namespace scrt::scene
