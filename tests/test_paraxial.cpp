#include <doctest/doctest.h>
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/materials/Dielectric.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/optics/Paraxial.hpp"
#include "scrt/surfaces/Plane.hpp"
#include <cmath>
#include <vector>

using namespace scrt::math;
using namespace scrt::optics;

// T8 -----------------------------------------------------------------------

TEST_CASE("T8: thin lens ABCD imaging condition: f=0.1 s=s'=0.2") {
    // M = free_space(0.2) * thin_lens(0.1) * free_space(0.2)
    std::vector<Mat2> elems = {
        abcd_free_space(0.2),
        abcd_thin_lens(0.1),
        abcd_free_space(0.2)
    };
    Mat2 M = cascade(elems);
    // B element = M[1][0] (col-major: col 1, row 0) must equal zero for perfect imaging.
    CHECK(M[1][0] == doctest::Approx(0.0).epsilon(1e-12));
    // Lateral magnification A = M[0][0] = -1 for 2f-2f symmetric system.
    CHECK(M[0][0] == doctest::Approx(-1.0).epsilon(1e-12));
}

TEST_CASE("T8: thin lens marginal ray at 0.1 deg converges to zero height at image plane") {
    std::vector<Mat2> elems = {
        abcd_free_space(0.2),
        abcd_thin_lens(0.1),
        abcd_free_space(0.2)
    };
    Mat2 M = cascade(elems);

    // Ray from on-axis object point with angle 0.1 degrees
    ParaxialRay in{0.0, 0.1 * DEG2RAD};
    ParaxialRay out = apply(M, in);

    // Must converge to y=0 at the image plane
    CHECK(out.y == doctest::Approx(0.0).epsilon(1e-12));
}

TEST_CASE("T8: ABCD free-space and thin-lens composition is invertible") {
    // A ray from a point at height h with zero angle stays at height h after free space,
    // then thin lens changes angle but not height.
    ParaxialRay r0{0.005, 0.0};  // 5 mm off axis, parallel to axis
    ParaxialRay r1 = apply(abcd_free_space(0.2), r0);
    CHECK(r1.y == doctest::Approx(0.005).epsilon(1e-15));

    ParaxialRay r2 = apply(abcd_thin_lens(0.1), r1);
    CHECK(r2.y     == doctest::Approx(0.005).epsilon(1e-15));
    // Angle should be -y/f = -0.005/0.1 = -0.05 rad
    CHECK(r2.theta == doctest::Approx(-0.05).epsilon(1e-12));
}

// T9 -----------------------------------------------------------------------

TEST_CASE("T9: dielectric split power conservation, air->glass, 0-89 deg") {
    scrt::surfaces::Plane plane(100.0, 100.0);  // large enough for tan(89°)≈57m
    scrt::materials::Dielectric glass(1.5, 0.0);
    plane.set_material(&glass);

    Rng rng(0);

    for (int deg = 0; deg <= 89; ++deg) {
        double theta_i = deg * DEG2RAD;

        scrt::core::Ray r;
        r.power     = 1.0;
        r.origin    = {0.0, 0.0, 1.0};
        r.direction = safe_normalize(vec3{std::sin(theta_i), 0.0, -std::cos(theta_i)});

        scrt::core::Hit hit;
        REQUIRE(plane.intersect(r, 1e-9, 1e9, hit));

        auto inter = glass.interact(r, hit, rng);

        double total_power = 0.0;
        if (inter.kind == scrt::materials::InteractionKind::Split) {
            total_power = inter.reflected.power + inter.transmitted.power;
        } else {
            // TIR (shouldn't happen for air→glass, but handle gracefully)
            total_power = inter.reflected.power;
        }
        CHECK(total_power == doctest::Approx(r.power).epsilon(1e-12));
    }
}

TEST_CASE("T9: dielectric TIR at glass->air above critical angle conserves power") {
    // Glass→air: critical angle ≈ 41.8°; test at 45° should be full TIR.
    scrt::surfaces::Plane plane(100.0, 100.0);
    scrt::materials::Dielectric glass(1.5, 0.0);
    plane.set_material(&glass);

    Rng rng(0);

    // Simulate exiting glass: ray from z<0 going toward +z through the plane,
    // arriving from inside (front_face=false side).
    // To trigger front_face=false we approach from z<0 going +z:
    // plane normal for z<0 approach is (0,0,-1) (against the ray), front_face=false.
    double theta_i = 45.0 * DEG2RAD;
    scrt::core::Ray r;
    r.power     = 1.0;
    r.origin    = {0.0, 0.0, -1.0};
    r.direction = safe_normalize(vec3{std::sin(theta_i), 0.0, std::cos(theta_i)});

    scrt::core::Hit hit;
    REQUIRE(plane.intersect(r, 1e-9, 1e9, hit));
    CHECK_FALSE(hit.front_face);  // exiting glass

    auto inter = glass.interact(r, hit, rng);
    CHECK(inter.kind == scrt::materials::InteractionKind::Reflected);
    CHECK(inter.reflected.power == doctest::Approx(r.power).epsilon(1e-15));
}
