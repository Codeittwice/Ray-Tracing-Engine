#pragma once
#include "scrt/core/Ray.hpp"
#include "scrt/math/Vec.hpp"

namespace scrt::core {

/// Axis-aligned bounding box; used for BVH construction and culling.
class AABB {
public:
    AABB();  ///< Constructs an inside-out box (expand before use).
    AABB(math::vec3 min, math::vec3 max);

    void expand(math::vec3 p);       ///< Grow to contain point p.
    void expand(const AABB& other);  ///< Grow to contain another AABB.

    /// Slab-method intersection; returns false if no hit in (0, +inf).
    bool intersect(const Ray& r, double& t_near, double& t_far) const;

    const math::vec3& min() const { return min_; }
    const math::vec3& max() const { return max_; }
    math::vec3 centroid() const { return 0.5 * (min_ + max_); }

private:
    math::vec3 min_;
    math::vec3 max_;
};

} // namespace scrt::core
