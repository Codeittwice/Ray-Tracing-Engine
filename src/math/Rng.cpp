#include "scrt/math/Rng.hpp"
#include "scrt/math/Constants.hpp"
#include <cmath>

namespace scrt::math {

Rng::Rng() {
    std::random_device rd;
    engine_.seed(rd());
}

Rng::Rng(uint64_t s) { engine_.seed(s); }

void Rng::seed(uint64_t s) {
    engine_.seed(s);
    dist_.reset();
}

double Rng::uniform01() { return dist_(engine_); }

double Rng::uniform(double a, double b) {
    return a + (b - a) * uniform01();
}

vec2 Rng::unit_disk_concentric() {
    // Shirley's concentric mapping from square [-1,1]^2 to unit disk.
    double u = 2.0 * uniform01() - 1.0;
    double v = 2.0 * uniform01() - 1.0;
    if (u == 0.0 && v == 0.0) return {0.0, 0.0};
    double r, theta;
    if (std::abs(u) >= std::abs(v)) {
        r     = u;
        theta = (PI / 4.0) * (v / u);
    } else {
        r     = v;
        theta = (PI / 2.0) - (PI / 4.0) * (u / v);
    }
    return {r * std::cos(theta), r * std::sin(theta)};
}

vec3 Rng::unit_sphere_direction() {
    // Inverse-CDF: cos(theta) uniform in [-1,1], phi uniform in [0, 2*pi).
    double cos_theta = 1.0 - 2.0 * uniform01();
    double phi       = TWO_PI * uniform01();
    double sin_theta = std::sqrt(1.0 - cos_theta * cos_theta);
    return {sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta};
}

} // namespace scrt::math
