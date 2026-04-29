#include <doctest/doctest.h>
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/core/AABB.hpp"
#include "scrt/materials/PerfectMirror.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/surfaces/CylindricalParaboloid.hpp"
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

// T_CP1 --------------------------------------------------------------------

TEST_CASE("T_CP1: CylindricalParaboloid hit point and normal for downward ray") {
    // x²=4fz with f=0.5. A ray at (x=0.4, y=0.3) downward hits z = 0.4²/(4·0.5) = 0.08.
    const double f = 0.5;
    scrt::surfaces::CylindricalParaboloid trough(f, 0.45, 1.0);

    scrt::core::Ray r;
    r.origin    = {0.4, 0.3, 100.0};
    r.direction = {0.0, 0.0,  -1.0};

    scrt::core::Hit hit;
    REQUIRE(trough.intersect(r, 1e-6, 1e9, hit));

    // t = 100 - 0.08 = 99.92
    CHECK(hit.t == doctest::Approx(99.92).epsilon(1e-10));

    // Position
    CHECK(hit.position.x == doctest::Approx(0.4).epsilon(1e-10));
    CHECK(hit.position.y == doctest::Approx(0.3).epsilon(1e-10));
    CHECK(hit.position.z == doctest::Approx(0.08).epsilon(1e-10));

    // Downward ray hits the concave (inner) face: front_face=false, so normal = −∇F/|∇F|.
    // ∇F = (2·0.4, 0, −4·0.5) = (0.8, 0, −2.0); negated → (−0.8, 0, 2.0).
    double nx_expected = -0.8 / std::sqrt(0.8*0.8 + 2.0*2.0);
    double nz_expected =  2.0 / std::sqrt(0.8*0.8 + 2.0*2.0);
    CHECK(hit.normal.x == doctest::Approx(nx_expected).epsilon(1e-10));
    CHECK(hit.normal.y == doctest::Approx(0.0).epsilon(1e-10));
    CHECK(hit.normal.z == doctest::Approx(nz_expected).epsilon(1e-10));

    // Y-shift: same x, different y must give identical z-hit and normal
    r.origin.y = -0.7;
    scrt::core::Hit hit2;
    REQUIRE(trough.intersect(r, 1e-6, 1e9, hit2));
    CHECK(hit2.position.z == doctest::Approx(0.08).epsilon(1e-10));
    CHECK(hit2.normal.x   == doctest::Approx(nx_expected).epsilon(1e-10));
    CHECK(hit2.normal.y   == doctest::Approx(0.0).epsilon(1e-10));
    CHECK(hit2.normal.z   == doctest::Approx(nz_expected).epsilon(1e-10));
}

TEST_CASE("T_CP1: CylindricalParaboloid rejects ray outside aperture") {
    scrt::surfaces::CylindricalParaboloid trough(0.5, 0.3, 0.5);
    scrt::core::Ray r;
    // x=0.4 > half_width=0.3: should miss
    r.origin    = {0.4, 0.0, 100.0};
    r.direction = {0.0, 0.0,  -1.0};
    scrt::core::Hit hit;
    CHECK_FALSE(trough.intersect(r, 1e-6, 1e9, hit));
}

// T_CP2 --------------------------------------------------------------------

TEST_CASE("T_CP2: CylindricalParaboloid PerfectMirror reflects to focal line") {
    // All downward rays, after reflection, must cross x=0 at z=f regardless of y-offset.
    const double f = 0.5;
    scrt::surfaces::CylindricalParaboloid trough(f, 0.45, 1.0);
    scrt::materials::PerfectMirror mirror;
    trough.set_material(&mirror);

    scrt::math::Rng rng(7);

    for (double x0 : {-0.3, -0.1, 0.1, 0.3}) {
        for (double y0 : {0.0, 0.2, -0.4}) {
            scrt::core::Ray ray;
            ray.origin    = {x0, y0, 100.0};
            ray.direction = {0.0, 0.0, -1.0};

            scrt::core::Hit hit;
            REQUIRE(trough.intersect(ray, 1e-6, 1e9, hit));

            auto inter = mirror.interact(ray, hit, rng);
            scrt::math::vec3 d = inter.reflected.direction;
            scrt::math::vec3 o = hit.position;

            // Find t where z = f along reflected ray
            CHECK(std::abs(d.z) > 1e-10);
            double t_focal = (f - o.z) / d.z;
            CHECK(t_focal > 0.0);

            double x_at_focal = o.x + t_focal * d.x;
            double y_at_focal = o.y + t_focal * d.y;

            // Must converge to x=0 (focal line), y unchanged (no y-deflection)
            CHECK(x_at_focal == doctest::Approx(0.0).epsilon(1e-9));
            CHECK(y_at_focal == doctest::Approx(y0).epsilon(1e-9));
        }
    }
}
