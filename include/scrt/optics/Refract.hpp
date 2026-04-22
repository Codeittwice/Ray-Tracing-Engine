#pragma once
#include "scrt/math/Vec.hpp"

namespace scrt::optics {

/// Vector Snell's law.  eta = n1/n2.  n must point against the incoming ray (i·n < 0).
/// Sets tir=true and returns reflect(i,n) on total internal reflection.
math::vec3 refract(math::vec3 i, math::vec3 n, double eta, bool& tir) noexcept;

} // namespace scrt::optics
