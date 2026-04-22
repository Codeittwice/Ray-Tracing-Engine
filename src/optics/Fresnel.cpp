#include "scrt/optics/Fresnel.hpp"
#include <cmath>

namespace scrt::optics {

FresnelResult fresnel_unpolarized(double cos_theta_i, double cos_theta_t,
                                   double n1, double n2) noexcept {
    double rs = (n1 * cos_theta_i - n2 * cos_theta_t) /
                (n1 * cos_theta_i + n2 * cos_theta_t);
    double rp = (n2 * cos_theta_i - n1 * cos_theta_t) /
                (n2 * cos_theta_i + n1 * cos_theta_t);
    double R  = 0.5 * (rs * rs + rp * rp);
    return {R, 1.0 - R};
}

double schlick_R0(double n1, double n2) noexcept {
    double r0 = (n1 - n2) / (n1 + n2);
    return r0 * r0;
}

double schlick_R(double cos_theta_i, double R0) noexcept {
    double x = 1.0 - cos_theta_i;
    return R0 + (1.0 - R0) * x * x * x * x * x;
}

} // namespace scrt::optics
