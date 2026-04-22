#pragma once
#include "scrt/math/Vec.hpp"
#include <glm/glm.hpp>

namespace scrt::optics {

/// Reflect incident direction i about unit normal n.  Both i and n must be unit vectors.
inline math::vec3 reflect(math::vec3 i, math::vec3 n) noexcept {
    return i - 2.0 * glm::dot(i, n) * n;
}

} // namespace scrt::optics
