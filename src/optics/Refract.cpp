#include "scrt/optics/Refract.hpp"
#include "scrt/optics/Reflect.hpp"
#include <cmath>

namespace scrt::optics {

math::vec3 refract(math::vec3 i, math::vec3 n, double eta, bool& tir) noexcept {
    // i·n < 0 by convention (n points against the incoming ray).
    double cos_i  = -glm::dot(i, n);          // positive: angle between i and -n
    double sin2_t = eta * eta * (1.0 - cos_i * cos_i);
    if (sin2_t >= 1.0) {
        tir = true;
        return reflect(i, n);
    }
    tir = false;
    double cos_t = std::sqrt(1.0 - sin2_t);
    return eta * i + (eta * cos_i - cos_t) * n;
}

} // namespace scrt::optics
