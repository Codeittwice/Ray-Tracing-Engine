#include "scrt/optics/Fresnel.hpp"
#include <cmath>

namespace scrt::optics {

FresnelAmplitudes fresnel_amplitudes(double cos_theta_i, double cos_theta_t,
                                     double n1, double n2) noexcept {
    // rs and rp are written EXACTLY as they were inside fresnel_unpolarized, which now reads them
    // from here. Changing either expression, even to an algebraically equal one, can move the
    // last bit of every dielectric result in the corpus.
    const double rs = (n1 * cos_theta_i - n2 * cos_theta_t) /
                      (n1 * cos_theta_i + n2 * cos_theta_t);
    const double rp = (n2 * cos_theta_i - n1 * cos_theta_t) /
                      (n2 * cos_theta_i + n1 * cos_theta_t);
    const double ts = (2.0 * n1 * cos_theta_i) / (n1 * cos_theta_i + n2 * cos_theta_t);
    const double tp = (2.0 * n1 * cos_theta_i) / (n2 * cos_theta_i + n1 * cos_theta_t);
    return {rs, rp, ts, tp};
}

FresnelResult fresnel_unpolarized(double cos_theta_i, double cos_theta_t,
                                   double n1, double n2) noexcept {
    const FresnelAmplitudes a = fresnel_amplitudes(cos_theta_i, cos_theta_t, n1, n2);
    double R = 0.5 * (a.rs * a.rs + a.rp * a.rp);
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
