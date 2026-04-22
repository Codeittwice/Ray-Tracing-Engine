#include "scrt/core/Transform.hpp"

namespace scrt::core {

namespace {
math::mat3 make_normal_xform(const math::mat4& inv) {
    // Normal transform = transpose of inverse of upper-left 3x3.
    return math::mat3(glm::transpose(inv));
}
} // anonymous namespace

Transform::Transform()
    : m_(1.0), inv_(1.0), normal_xform_(1.0) {}

Transform Transform::from_translation(math::vec3 t) {
    Transform xf;
    xf.m_            = glm::translate(math::mat4(1.0), t);
    xf.inv_          = glm::translate(math::mat4(1.0), -t);
    xf.normal_xform_ = make_normal_xform(xf.inv_);
    return xf;
}

Transform Transform::from_rotation_axis_angle(math::vec3 axis, double radians) {
    Transform xf;
    xf.m_            = glm::rotate(math::mat4(1.0), radians, axis);
    xf.inv_          = glm::inverse(xf.m_);
    xf.normal_xform_ = make_normal_xform(xf.inv_);
    return xf;
}

Transform Transform::from_euler_xyz(math::vec3 euler_radians) {
    math::mat4 m = math::mat4(1.0);
    m = glm::rotate(m, euler_radians.x, math::vec3{1, 0, 0});
    m = glm::rotate(m, euler_radians.y, math::vec3{0, 1, 0});
    m = glm::rotate(m, euler_radians.z, math::vec3{0, 0, 1});
    return from_matrix(m);
}

Transform Transform::from_look_at(math::vec3 eye, math::vec3 target, math::vec3 up) {
    return from_matrix(glm::lookAt(eye, target, up));
}

Transform Transform::from_matrix(const math::mat4& m) {
    Transform xf;
    xf.m_            = m;
    xf.inv_          = glm::inverse(m);
    xf.normal_xform_ = make_normal_xform(xf.inv_);
    return xf;
}

Transform Transform::compose(const Transform& child) const {
    return from_matrix(m_ * child.m_);
}

math::vec3 Transform::point_to_world(math::vec3 p) const {
    return math::vec3(m_ * math::vec4(p, 1.0));
}

math::vec3 Transform::direction_to_world(math::vec3 d) const {
    return math::vec3(m_ * math::vec4(d, 0.0));
}

math::vec3 Transform::normal_to_world(math::vec3 n) const {
    return glm::normalize(normal_xform_ * n);
}

math::vec3 Transform::point_to_local(math::vec3 p) const {
    return math::vec3(inv_ * math::vec4(p, 1.0));
}

math::vec3 Transform::direction_to_local(math::vec3 d) const {
    return math::vec3(inv_ * math::vec4(d, 0.0));
}

Ray Transform::ray_to_local(const Ray& r) const {
    Ray local      = r;
    local.origin    = point_to_local(r.origin);
    local.direction = direction_to_local(r.direction);
    return local;
}

} // namespace scrt::core
