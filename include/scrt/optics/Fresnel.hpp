#pragma once

namespace scrt::optics {

/// Reflectance and transmittance pair; R + T = 1 by energy conservation.
struct FresnelResult { double R; double T; };

/// Fresnel AMPLITUDE coefficients for the s (perpendicular) and p (parallel) polarisations.
///
/// Sign convention (the one the existing rp formula has always used, measured by the test):
/// rs = (n1 ci - n2 ct)/(n1 ci + n2 ct), rp = (n2 ci - n1 ct)/(n2 ci + n1 ct), so at NORMAL
/// incidence rp == -rs. Polarised code in Stage 3 must use this p reference direction, or a
/// mirror flips the handedness of circular light the wrong way.
/// Power transmittance is Ts = (n2 cos_t)/(n1 cos_i) * ts^2, and likewise for p; Rs + Ts == 1.
struct FresnelAmplitudes { double rs; double rp; double ts; double tp; };

/// Exact Fresnel amplitude coefficients for a non-TIR interface (cos_theta_t from Snell).
FresnelAmplitudes fresnel_amplitudes(double cos_theta_i, double cos_theta_t,
                                     double n1, double n2) noexcept;

/// Exact unpolarized Fresnel equations: the average of the s and p power reflectances.
///
/// Formed from fresnel_amplitudes() with the same operations in the same order as before that
/// function existed, so every existing result is bit-identical (tests/test_fresnel.cpp, `==`).
FresnelResult fresnel_unpolarized(double cos_theta_i, double cos_theta_t,
                                   double n1, double n2) noexcept;

/// Schlick R0 = ((n1-n2)/(n1+n2))^2.
double schlick_R0(double n1, double n2) noexcept;

/// Schlick approximation: R0 + (1-R0)*(1-cos_theta_i)^5.
double schlick_R(double cos_theta_i, double R0) noexcept;

} // namespace scrt::optics
