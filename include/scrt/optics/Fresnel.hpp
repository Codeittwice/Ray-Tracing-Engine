#pragma once

namespace scrt::optics {

/// Reflectance and transmittance pair; R + T = 1 by energy conservation.
struct FresnelResult { double R; double T; };

/// Exact unpolarized Fresnel equations.
FresnelResult fresnel_unpolarized(double cos_theta_i, double cos_theta_t,
                                   double n1, double n2) noexcept;

/// Schlick R0 = ((n1-n2)/(n1+n2))^2.
double schlick_R0(double n1, double n2) noexcept;

/// Schlick approximation: R0 + (1-R0)*(1-cos_theta_i)^5.
double schlick_R(double cos_theta_i, double R0) noexcept;

} // namespace scrt::optics
