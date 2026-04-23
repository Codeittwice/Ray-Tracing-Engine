#pragma once
#include "scrt/math/Vec.hpp"
#include <span>

namespace scrt::optics {

/// 2×2 ABCD ray-transfer matrix: M*[y, theta]^T = [y', theta']^T.
using Mat2 = math::mat2;  // glm::dmat2, column-major

/// Free-space propagation over distance d.
Mat2 abcd_free_space(double d);

/// Thin lens of focal length f.
Mat2 abcd_thin_lens(double f);

/// Flat refracting interface (angle of refraction via paraxial Snell's law).
Mat2 abcd_flat_refraction(double n1, double n2);

/// Spherical refracting surface, radius R (positive = centre on right).
Mat2 abcd_spherical_refraction(double n1, double n2, double R);

/// Spherical mirror, radius of curvature R (positive = concave toward incoming ray).
Mat2 abcd_spherical_mirror(double R);

/// Cascade matrices in propagation order; returns M_n * ... * M_1.
Mat2 cascade(std::span<const Mat2> elements_in_order);

/// Paraxial ray described by height y (m) and angle theta (rad).
struct ParaxialRay { double y; double theta; };

/// Propagate a paraxial ray through ABCD matrix M.
ParaxialRay apply(const Mat2& M, ParaxialRay in);

} // namespace scrt::optics
