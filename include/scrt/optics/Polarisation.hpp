#pragma once

#include "scrt/core/Ray.hpp"
#include "scrt/math/Vec.hpp"

#include <complex>

namespace scrt::optics {

/// Complex amplitude type used for Jones vectors and Fresnel coefficients.
using cplx = std::complex<double>;

/// Unit vector of the ray's p axis: direction x s_axis. With s, p and direction this is a
/// right-handed frame, and it is the frame the rp sign convention in Fresnel.hpp assumes.
math::vec3 p_axis(const core::Ray& r) noexcept;

/// The polarisation state as a world-space complex field vector (unit power): Es s + Ep p.
struct FieldVec {
    cplx x, y, z;
};

/// World-space field of a polarised ray; used by tests and by anything comparing states.
FieldVec world_field(const core::Ray& r) noexcept;

/// Re-expresses the ray's Jones vector in the s/p frame of an interface with normal `n`.
///
/// s becomes the unit normal of the plane of incidence (direction x n). The physical field is
/// unchanged - only its components move. At normal incidence that plane is undefined and every
/// choice of s is equally valid, so the ray's own s axis is kept.
void align_to_interface(core::Ray& r, math::vec3 n) noexcept;

/// Applies amplitude coefficients (as, ap) to an outgoing ray whose DIRECTION is already set.
///
/// Its s axis must already be the interface's (align_to_interface on the incoming ray, then copy).
/// Returns |as Es|^2 + |ap Ep|^2 - the fraction of the incoming power this branch carries - and
/// renormalises the Jones vector, because power lives in Ray::power and the vector is the state.
double apply_jones(core::Ray& out, cplx as, cplx ap) noexcept;

/// Carries `in`'s state, aligned to the interface normal `n`, onto the outgoing ray `out` (whose
/// direction is already set) and applies (as, ap). Returns the power fraction, as apply_jones.
double transfer_state(const core::Ray& in, math::vec3 n, core::Ray& out, cplx as, cplx ap) noexcept;

/// Fresnel amplitude coefficients under total internal reflection (|rs| = |rp| = 1, complex phase).
///
/// Same formulas and sign convention as fresnel_amplitudes, with cos_theta_t = i sqrt(sin^2 - 1):
/// the phase difference between s and p is what a Fresnel rhomb turns into circular light.
void tir_amplitudes(double cos_theta_i, double n1, double n2, cplx& rs, cplx& rp) noexcept;

/// A fully polarised state on a ray travelling along `direction`: linear at `angle_rad` from the
/// reference axis, or circular. The reference (s) axis is world +Z projected across the beam, or
/// world +X for a beam running along Z, so 0 degrees is "vertical" on a horizontal bench.
enum class PolarisationKind { Unpolarised, Linear, CircularLeft, CircularRight };

/// Stamps a polarisation state onto a ray (whose direction is already set). No RNG is used.
///
/// Handedness follows the traditional optics convention with time dependence e^{-i w t}:
/// LEFT circular turns counter-clockwise as seen looking INTO the beam (towards the source), which
/// with s, p = direction x s is (Es, Ep) = (1, +i)/sqrt(2); RIGHT is (1, -i)/sqrt(2).
void set_polarisation(core::Ray& r, PolarisationKind kind, double angle_rad) noexcept;

} // namespace scrt::optics
