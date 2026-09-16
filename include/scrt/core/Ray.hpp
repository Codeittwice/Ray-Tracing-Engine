#pragma once
#include "scrt/math/Vec.hpp"
#include <complex>
#include <cstdint>

namespace scrt::core {

/// A ray carrying optical power through the scene.
///
/// Wave 5 added the polarisation and phase payload below. It is carried by every ray but READ only
/// when `polarised` is true (Stage 3 onward); an unpolarised ray - every solar ray, and a laser
/// unless its scene says otherwise - runs exactly the pre-Wave-5 code, which is why the goldens
/// cannot move. The cost is size: see the Stage 2 measurement in CLAUDE.md.
struct Ray {
    math::vec3    origin      {0.0};
    math::vec3    direction   {0.0, 0.0, 1.0};  ///< Unit vector.
    double        power       {1.0};             ///< Watts.
    double        wavelength_nm{550.0};          ///< Nanometres (I/O only; physics in metres).
    int           bounces     {0};
    std::uint32_t id          {0};

    /// The s-axis the Jones vector is expressed in: a unit vector perpendicular to `direction`.
    /// Every interaction rotates (Es, Ep) into the interface's own s/p frame before applying Fresnel.
    math::vec3           s_axis  {0.0, 1.0, 0.0};
    /// Jones vector in the (s_axis, direction x s_axis) basis. |Es|^2 + |Ep|^2 == 1 for a fully
    /// polarised ray; power stays in `power`, so this carries the STATE, not the amount.
    std::complex<double> Es      {1.0, 0.0};
    std::complex<double> Ep      {0.0, 0.0};
    /// Accumulated optical path length [m]: geometric distance times the index of each medium.
    /// Phase is 2 pi opl / wavelength (Stage 5).
    double               opl_m   {0.0};
    /// Refractive index of the medium the ray is currently travelling in (Stage 5).
    double               medium_n{1.0};
    /// True only for a ray whose source declared a polarisation. False means: ignore the Jones
    /// vector and use the averaged (unpolarised) optics, bit for bit as before Wave 5.
    bool                 polarised{false};
};

} // namespace scrt::core
