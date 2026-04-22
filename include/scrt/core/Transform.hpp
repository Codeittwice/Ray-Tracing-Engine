#pragma once
#include "scrt/core/Ray.hpp"
#include "scrt/math/Vec.hpp"

namespace scrt::core {

/// Rigid + non-uniform-scale transform: places any Surface anywhere in the scene.
class Transform {
public:
    Transform();  ///< Identity.

    static Transform from_translation(math::vec3 t);
    static Transform from_rotation_axis_angle(math::vec3 axis, double radians);
    static Transform from_euler_xyz(math::vec3 euler_radians);
    static Transform from_look_at(math::vec3 eye, math::vec3 target, math::vec3 up);
    static Transform from_matrix(const math::mat4& m);

    /// Returns transform equivalent to applying child first, then this.
    Transform compose(const Transform& child) const;

    math::vec3 point_to_world(math::vec3 p)    const;
    math::vec3 direction_to_world(math::vec3 d) const;
    /// Transforms a surface normal using the inverse-transpose (handles non-uniform scale).
    math::vec3 normal_to_world(math::vec3 n)   const;

    math::vec3 point_to_local(math::vec3 p)    const;
    math::vec3 direction_to_local(math::vec3 d) const;
    /// Transforms a world-space ray into the surface's local frame.
    Ray        ray_to_local(const Ray& r)       const;

    const math::mat4& matrix()  const { return m_; }
    const math::mat4& inverse() const { return inv_; }

private:
    math::mat4 m_;
    math::mat4 inv_;
    math::mat3 normal_xform_;  ///< Transpose of inverse upper-left 3x3.
};

} // namespace scrt::core
