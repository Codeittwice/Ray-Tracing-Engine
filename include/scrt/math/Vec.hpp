#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/norm.hpp>
#include <cassert>
#include <cmath>

namespace scrt::math {

using vec2 = glm::dvec2;
using vec3 = glm::dvec3;
using vec4 = glm::dvec4;
using mat2 = glm::dmat2;
using mat3 = glm::dmat3;
using mat4 = glm::dmat4;

/// Normalize v; asserts v is non-zero.
inline vec3 safe_normalize(const vec3& v) {
    double l2 = glm::dot(v, v);
    assert(l2 > 0.0 && "safe_normalize on zero vector");
    return v / std::sqrt(l2);
}

} // namespace scrt::math
