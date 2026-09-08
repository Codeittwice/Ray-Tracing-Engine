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
    /// Pure (possibly non-uniform) scale about the local origin.
    static Transform from_scale(math::vec3 s);
    /// Translate * Rotate(euler XYZ) * Scale, applied to a local point in that order.
    static Transform from_trs(math::vec3 t, math::vec3 euler_rad, math::vec3 scale);

    /// Recovers the T*R(XYZ)*S factors; returns false if the linear part is sheared.
    bool decompose_trs(math::vec3& t, math::vec3& euler_rad, math::vec3& scale) const;

    /// Largest singular value of the linear part: the worst-case length magnification.
    double max_scale_factor() const;
    /// True when the linear part is a rotation (or reflection) times one scalar factor.
    bool is_uniform_scale(double tol = 1e-9) const;

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
