#include "scrt/core/AABB.hpp"
#include <algorithm>
#include <limits>

namespace scrt::core {

AABB::AABB()
    : min_( std::numeric_limits<double>::max())
    , max_(std::numeric_limits<double>::lowest()) {}

AABB::AABB(math::vec3 min, math::vec3 max) : min_(min), max_(max) {}

void AABB::expand(math::vec3 p) {
    min_ = glm::min(min_, p);
    max_ = glm::max(max_, p);
}

void AABB::expand(const AABB& other) {
    min_ = glm::min(min_, other.min_);
    max_ = glm::max(max_, other.max_);
}

bool AABB::intersect(const Ray& r, double& t_near, double& t_far) const {
    t_near = std::numeric_limits<double>::lowest();
    t_far  = std::numeric_limits<double>::max();
    for (int i = 0; i < 3; ++i) {
        double inv_d = 1.0 / r.direction[i];  // inf on axis-aligned rays — handled correctly.
        double t0    = (min_[i] - r.origin[i]) * inv_d;
        double t1    = (max_[i] - r.origin[i]) * inv_d;
        if (inv_d < 0.0) std::swap(t0, t1);
        t_near = std::max(t_near, t0);
        t_far  = std::min(t_far,  t1);
        if (t_near > t_far) return false;
    }
    return true;
}

} // namespace scrt::core
