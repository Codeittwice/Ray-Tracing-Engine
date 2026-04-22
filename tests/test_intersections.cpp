#include <doctest/doctest.h>
#include "scrt/core/Transform.hpp"
#include "scrt/core/AABB.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/math/Constants.hpp"
#include <cmath>

using namespace scrt::core;
using namespace scrt::math;

// T7 -----------------------------------------------------------------------

TEST_CASE("T7: identity transform is a no-op on points") {
    Transform id;
    vec3 p{1.5, -2.3, 0.7};
    vec3 w = id.point_to_world(p);
    CHECK(w.x == doctest::Approx(p.x).epsilon(1e-15));
    CHECK(w.y == doctest::Approx(p.y).epsilon(1e-15));
    CHECK(w.z == doctest::Approx(p.z).epsilon(1e-15));
}

TEST_CASE("T7: rotation + translation round-trip within 1e-12") {
    auto rot   = Transform::from_rotation_axis_angle({0, 1, 0}, 45.0 * DEG2RAD);
    auto trans = Transform::from_translation({1.0, 2.0, 3.0});
    auto xf    = trans.compose(rot);

    vec3 p{1.0, -2.0, 0.5};
    vec3 back = xf.point_to_local(xf.point_to_world(p));
    CHECK(back.x == doctest::Approx(p.x).epsilon(1e-12));
    CHECK(back.y == doctest::Approx(p.y).epsilon(1e-12));
    CHECK(back.z == doctest::Approx(p.z).epsilon(1e-12));
}

TEST_CASE("T7: normal_to_world correct under non-uniform scale") {
    // Surface x + y = 0 has normal (1,1,0)/sqrt(2).
    // Under scale (2,1,1), it becomes x/2 + y = 0 => world normal (1,2,0)/sqrt(5).
    auto xf      = Transform::from_matrix(glm::scale(mat4(1.0), vec3{2.0, 1.0, 1.0}));
    vec3 n_local = safe_normalize(vec3{1, 1, 0});
    vec3 n_world = xf.normal_to_world(n_local);
    vec3 expected = safe_normalize(vec3{1, 2, 0});
    CHECK(n_world.x == doctest::Approx(expected.x).epsilon(1e-12));
    CHECK(n_world.y == doctest::Approx(expected.y).epsilon(1e-12));
    CHECK(n_world.z == doctest::Approx(expected.z).epsilon(1e-12));
}

TEST_CASE("T7: euler XYZ rotation round-trip") {
    auto xf = Transform::from_euler_xyz({0.3, 0.7, -0.5});
    vec3 p{-1.0, 2.5, 0.1};
    vec3 back = xf.point_to_local(xf.point_to_world(p));
    CHECK(back.x == doctest::Approx(p.x).epsilon(1e-12));
    CHECK(back.y == doctest::Approx(p.y).epsilon(1e-12));
    CHECK(back.z == doctest::Approx(p.z).epsilon(1e-12));
}

// AABB tests ---------------------------------------------------------------

TEST_CASE("AABB expand and intersect: ray hits center") {
    AABB box{vec3{-1, -1, -1}, vec3{1, 1, 1}};
    scrt::core::Ray r;
    r.origin    = {0, 0, -5};
    r.direction = {0, 0,  1};
    double tn, tf;
    CHECK(box.intersect(r, tn, tf));
    CHECK(tn == doctest::Approx(4.0).epsilon(1e-12));
    CHECK(tf == doctest::Approx(6.0).epsilon(1e-12));
}

TEST_CASE("AABB intersect: ray misses") {
    AABB box{vec3{-1, -1, -1}, vec3{1, 1, 1}};
    scrt::core::Ray r;
    r.origin    = {0, 5, -5};
    r.direction = {0, 0,  1};
    double tn, tf;
    CHECK_FALSE(box.intersect(r, tn, tf));
}
