#include "scrt/core/Transform.hpp"
#include <algorithm>
#include <cmath>

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

Transform Transform::from_scale(math::vec3 s) {
    return from_matrix(glm::scale(math::mat4(1.0), s));
}

Transform Transform::from_trs(math::vec3 t, math::vec3 euler_rad, math::vec3 scale) {
    // M = T * R * S, so a local point is scaled, then rotated, then translated.
    const math::mat4 rot = from_euler_xyz(euler_rad).matrix();
    math::mat4 m = glm::translate(math::mat4(1.0), t);
    m = m * rot;
    m = glm::scale(m, scale);
    return from_matrix(m);
}

namespace {

/// Gram matrix L^T L of the linear part; its eigenvalues are the squared singular values.
math::mat3 gram(const math::mat4& m) {
    const math::mat3 L(m);
    return glm::transpose(L) * L;
}

/// Largest eigenvalue of a symmetric 3x3 matrix (closed-form trigonometric solution).
double largest_symmetric_eigenvalue(const math::mat3& a) {
    // glm is column-major: a[col][row]. The matrix is symmetric so the distinction
    // only matters for readability here.
    const double p1 = a[1][0] * a[1][0] + a[2][0] * a[2][0] + a[2][1] * a[2][1];
    const double q  = (a[0][0] + a[1][1] + a[2][2]) / 3.0;
    if (p1 <= 0.0)
        return std::max({a[0][0], a[1][1], a[2][2]});

    const double p2 = (a[0][0] - q) * (a[0][0] - q)
                    + (a[1][1] - q) * (a[1][1] - q)
                    + (a[2][2] - q) * (a[2][2] - q) + 2.0 * p1;
    const double p = std::sqrt(p2 / 6.0);
    if (p <= 0.0)
        return q;

    const math::mat3 b = (a - q * math::mat3(1.0)) * (1.0 / p);
    const double r = std::clamp(glm::determinant(b) * 0.5, -1.0, 1.0);
    const double phi = std::acos(r) / 3.0;
    return q + 2.0 * p * std::cos(phi);  // The largest of the three roots.
}

} // anonymous namespace

double Transform::max_scale_factor() const {
    const double lambda = largest_symmetric_eigenvalue(gram(m_));
    return lambda > 0.0 ? std::sqrt(lambda) : 0.0;
}

bool Transform::is_uniform_scale(double tol) const {
    const math::mat3 g = gram(m_);
    const double mean = (g[0][0] + g[1][1] + g[2][2]) / 3.0;
    if (mean <= 0.0)
        return false;
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) {
            const double target = (c == r) ? mean : 0.0;
            if (std::abs(g[c][r] - target) > tol * mean)
                return false;
        }
    return true;
}

bool Transform::decompose_trs(math::vec3& t, math::vec3& euler_rad, math::vec3& scale) const {
    t = math::vec3(m_[3]);

    math::mat3 L(m_);
    math::vec3 s{glm::length(math::vec3(L[0])),
                 glm::length(math::vec3(L[1])),
                 glm::length(math::vec3(L[2]))};
    if (s.x <= 0.0 || s.y <= 0.0 || s.z <= 0.0)
        return false;

    // A negative determinant means one axis is mirrored; attribute it to x by convention.
    if (glm::determinant(L) < 0.0) {
        s.x = -s.x;
        L[0] = -L[0];
    }

    math::mat3 rot;
    rot[0] = L[0] / std::abs(s.x);
    rot[1] = L[1] / s.y;
    rot[2] = L[2] / s.z;

    // Reject shear: R must be orthonormal, i.e. R^T R == I.
    const math::mat3 rtr = glm::transpose(rot) * rot;
    constexpr double kOrthoTol = 1e-9;
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) {
            const double target = (c == r) ? 1.0 : 0.0;
            if (std::abs(rtr[c][r] - target) > kOrthoTol)
                return false;
        }

    // R = Rx(a) * Ry(b) * Rz(c); in maths (row, col) indexing that gives
    //   R(0,2) = sin b,  R(1,2) = -sin a cos b,  R(2,2) = cos a cos b,
    //   R(0,1) = -cos b sin c,  R(0,0) = cos b cos c.
    // glm stores column-major, so R(row, col) is rot[col][row].
    const double sb = std::clamp(rot[2][0], -1.0, 1.0);
    const double b  = std::asin(sb);
    double a, c;
    if (std::abs(std::cos(b)) > 1e-9) {
        a = std::atan2(-rot[2][1], rot[2][2]);
        c = std::atan2(-rot[1][0], rot[0][0]);
    } else {
        // Gimbal lock (b = +/-90 deg): fold the free rotation into a, set c = 0.
        // At the pole rot[0][1] / rot[1][1] are sin/cos of (a + sb*c), so the sb
        // factor is required: without it the b = -90 branch recovers -(a - c)
        // instead of (a - c) and the decomposition silently does not round-trip.
        a = std::atan2(sb * rot[0][1], rot[1][1]);
        c = 0.0;
    }

    euler_rad = {a, b, c};
    scale     = s;
    return true;
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
